#!/usr/bin/env python3
"""Compare Cross and peer MIPS III/o32 code generation."""

from __future__ import annotations

import argparse
import asyncio
import csv
import hashlib
import io
import json
import math
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time
from collections import defaultdict
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import asdict, dataclass
from pathlib import Path


class BenchmarkError(RuntimeError):
    """Report an invalid setup, compilation, or benchmark result."""


@dataclass(frozen=True)
class Kernel:
    """One independently validated and scored generated-code workload."""

    name: str
    category: str
    unit: str
    kind: str
    work_units: int


@dataclass(frozen=True)
class Tools:
    """Resolved compiler, object, linker, and emulator executables."""

    cross_cc: Path
    gcc: Path
    clang: Path
    opt: Path
    llc: Path
    llvm_mc: Path
    llvm_objcopy: Path
    llvm_size: Path
    llvm_nm: Path
    llvm_objdump: Path
    target_ld: Path
    lld: Path
    qemu: Path


@dataclass(frozen=True)
class Configuration:
    """Immutable benchmark paths and policy."""

    source_dir: Path
    kernel_dir: Path
    output_dir: Path
    tools: Tools
    levels: tuple[str, ...]
    samples: int
    timeout_seconds: int
    revision: str
    revision_directory: str
    libgcc: Path
    libgcc_symbols: frozenset[str]


@dataclass(frozen=True)
class CommandResult:
    """Captured subprocess completion."""

    command: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str


@dataclass(frozen=True)
class BuildRecord:
    """One compiler/level/source-unit object measurement."""

    compiler: str
    level: str
    unit: str
    object_path: Path
    text_bytes: int
    load_bytes: int
    object_bytes: int
    kernel_sizes: tuple[tuple[str, int], ...]
    runtime_symbols: tuple[str, ...]


@dataclass(frozen=True)
class ImageRecord:
    """One linked image with the common benchmark harness removed from sizes."""

    compiler: str
    level: str
    elf_path: Path
    linked_text_bytes: int
    linked_load_bytes: int
    benchmark_text_bytes: int
    benchmark_load_bytes: int
    support_text_bytes: int
    support_load_bytes: int
    runtime_symbols: tuple[str, ...]


@dataclass(frozen=True)
class RuntimeRecord:
    """Median QEMU virtual-count result for one kernel."""

    compiler: str
    level: str
    category: str
    kernel: str
    work_units: int
    median_ticks: int
    ticks_per_unit: float
    checksum: str


UNITS: tuple[str, ...] = (
    "scalar",
    "calls",
    "memory",
    "indirect",
    "control",
    "floating",
)
BASE_COMPILERS: tuple[str, ...] = (
    "cross",
    "gcc-gimple",
    "gcc-rtl",
    "gcc",
)
PIPELINES: Mapping[str, str] = {
    "cross": "Cross MIR -> common Machine IR -> native MIPS backend",
    "gcc-gimple": "Cross MIR -> GCC GIMPLE SSA -> GCC GIMPLE + RTL",
    "gcc-rtl": "optimized Cross MIR -> GCC GIMPLE SSA -> GCC RTL",
    "gcc": "equivalent freestanding C -> GCC GIMPLE + RTL",
    "clang": "equivalent freestanding C -> Clang LLVM -> MIPS",
    "llc": "Cross MIR -> LLVM IR -> opt -> llc MIPS",
}


def kernel(name: str, category: str, unit: str, kind: str, work_units: int) -> Kernel:
    """Construct a compact immutable kernel descriptor."""

    return Kernel(name, category, unit, kind, work_units)


KERNELS: tuple[Kernel, ...] = (
    kernel("mix", "integer", "scalar", "scalar", 1024),
    kernel("divide", "integer", "scalar", "scalar", 64),
    kernel("narrow", "integer", "scalar", "scalar", 4096),
    kernel("rotate", "bitwise", "scalar", "scalar", 512),
    kernel("bitswap", "bitwise", "scalar", "scalar", 1024),
    kernel("call_leaf", "calls", "calls", "scalar", 256),
    kernel("call_chain", "calls", "calls", "scalar", 128),
    kernel("reduce", "reduction", "memory", "memory", 2048),
    kernel("xor_reduce", "reduction", "memory", "memory", 2048),
    kernel("transform", "streaming", "memory", "transform", 2048),
    kernel("blend", "streaming", "memory", "transform", 2048),
    kernel("scan", "memory-dependency", "memory", "transform", 2048),
    kernel("prefix_xor", "memory-dependency", "memory", "transform", 2048),
    kernel("gather", "indirect", "indirect", "gather", 2048),
    kernel("gather_pair", "indirect", "indirect", "gather", 2048),
    kernel("pointer_chase", "pointer-chasing", "indirect", "gather", 4096),
    kernel("pointer_chase_pair", "pointer-chasing", "indirect", "gather", 4096),
    kernel("branch", "branching", "control", "memory", 2048),
    kernel("classify4", "branching", "control", "memory", 2048),
    kernel("classify", "branching", "control", "memory", 2048),
    kernel("classify16", "branching", "control", "memory", 2048),
    kernel("select", "selection", "control", "memory", 2048),
    kernel("clamp", "selection", "control", "memory", 2048),
    kernel("search", "search", "control", "search", 512),
    kernel("linear_search", "search", "control", "search", 512),
    kernel("polynomial", "fp-throughput", "floating", "floating", 1024),
    kernel("dot", "fp-throughput", "floating", "floating_pair", 1024),
    kernel("axpy", "fp-throughput", "floating", "floating_pair", 1024),
    kernel("quotient", "fp-throughput", "floating", "floating_pair", 512),
    kernel("recurrence", "fp-dependency", "floating", "floating", 1024),
    kernel("iir", "fp-dependency", "floating", "floating", 1024),
)


def resolve_tool(value: str, description: str) -> Path:
    """Resolve an explicit executable path or a PATH command."""

    supplied = Path(value).expanduser()
    if supplied.is_absolute() or supplied.parent != Path("."):
        candidate = supplied.resolve()
        if candidate.is_file():
            return candidate
    found = shutil.which(value)
    if found is None:
        raise BenchmarkError(f"cannot find {description}: {value}")
    return Path(found).resolve()


def parse_levels(value: str) -> tuple[str, ...]:
    """Parse stable, duplicate-free optimization preset names."""

    levels = [item.strip().removeprefix("-") for item in value.split(",")]
    levels = [level for level in levels if level]
    if not levels:
        raise BenchmarkError("at least one optimization level is required")
    unknown = [level for level in levels if level not in {"O3", "Oz"}]
    if unknown:
        raise BenchmarkError(
            "MIPS benchmark currently accepts O3 and Oz only: " + ", ".join(unknown)
        )
    return tuple(dict.fromkeys(levels))


def render_template(template: str, replacements: Mapping[str, str]) -> str:
    """Render the benchmark's deliberately small placeholder language."""

    rendered = template
    for name, replacement in replacements.items():
        rendered = rendered.replace(f"@{name}@", replacement)
    unresolved = sorted(set(re.findall(r"@[A-Z_]+@", rendered)))
    if unresolved:
        raise BenchmarkError(f"unresolved template fields: {', '.join(unresolved)}")
    return rendered


def write_rendered(
    template_path: Path,
    destination: Path,
    replacements: Mapping[str, str],
) -> None:
    """Render one UTF-8 template into the generated-output tree."""

    destination.write_text(
        render_template(template_path.read_text(encoding="utf-8"), replacements),
        encoding="utf-8",
        newline="\n",
    )


async def run_command(
    command: Sequence[str | Path],
    *,
    timeout: int | None = None,
    check: bool = True,
) -> CommandResult:
    """Execute one tool with captured UTF-8 diagnostics."""

    normalized = tuple(str(item) for item in command)
    try:
        completed: subprocess.CompletedProcess[str] = await asyncio.to_thread(
            subprocess.run,
            normalized,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
            check=False,
        )
    except subprocess.TimeoutExpired as error:
        raise BenchmarkError(
            f"command timed out after {timeout}s: {' '.join(normalized)}"
        ) from error
    except OSError as error:
        raise BenchmarkError(
            f"cannot execute {' '.join(normalized)}: {error}"
        ) from error
    result = CommandResult(
        normalized,
        completed.returncode,
        completed.stdout,
        completed.stderr,
    )
    if check and result.returncode != 0:
        raise BenchmarkError(
            f"command failed ({result.returncode}): {' '.join(normalized)}\n"
            f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def parse_size_sections(output: str) -> tuple[int, int]:
    """Return executable text and linked load-image bytes from llvm-size."""

    text = 0
    load = 0
    load_prefixes = (".text", ".rodata", ".data", ".sdata", ".lit4", ".lit8")
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2 or not fields[0].startswith("."):
            continue
        try:
            size = int(fields[1], 0)
        except ValueError as error:
            raise BenchmarkError(f"invalid llvm-size row: {line}") from error
        if fields[0].startswith(".text"):
            text += size
        if fields[0].startswith(load_prefixes):
            load += size
    if text == 0:
        raise BenchmarkError("llvm-size output contains no executable text")
    return text, load


def parse_nm_symbols(output: str, symbol_type: str | None = None) -> frozenset[str]:
    """Parse symbol names from LLVM nm's portable POSIX output."""

    symbols: set[str] = set()
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 2 or fields[0].endswith(":"):
            continue
        if symbol_type is None or fields[1] == symbol_type:
            symbols.add(fields[0])
    return frozenset(symbols)


def parse_nm_function_sizes(output: str, prefix: str = "bench_") -> dict[str, int]:
    """Parse decimal POSIX nm sizes for externally visible benchmark code."""

    sizes: dict[str, int] = {}
    for line in output.splitlines():
        fields = line.split()
        if len(fields) < 4 or not fields[0].startswith(prefix):
            continue
        if fields[1].upper() != "T":
            continue
        try:
            size = int(fields[3], 10)
        except ValueError as error:
            raise BenchmarkError(f"invalid llvm-nm size row: {line}") from error
        name = fields[0].removeprefix(prefix)
        if size <= 0 or name in sizes:
            raise BenchmarkError(f"invalid or duplicate function size row: {line}")
        sizes[name] = size
    return sizes


async def measure_sections(config: Configuration, path: Path) -> tuple[int, int]:
    """Measure executable text and load-image sections in an object or ELF."""

    sized = await run_command((config.tools.llvm_size, "--format=sysv", path))
    return parse_size_sections(sized.stdout)


def declaration(prefix: str, value: Kernel) -> str:
    """Return the freestanding runner declaration for one kernel kind."""

    signatures = {
        "scalar": "u64, uptr",
        "memory": "const u64 *, uptr, uptr",
        "transform": "const u64 *, u64 *, uptr, uptr",
        "gather": "const u64 *, const u64 *, uptr, uptr",
        "search": "const u64 *, uptr, u64, uptr",
        "floating": "const double *, uptr, uptr",
        "floating_pair": "const double *, const double *, uptr, uptr",
    }
    result = "double" if value.kind.startswith("floating") else "u64"
    return f"extern {result} {prefix}{value.name}({signatures[value.kind]});"


def invocation(prefix: str, value: Kernel) -> str:
    """Return a fixed-work runner call for one kernel."""

    if value.kind == "scalar":
        return f"{prefix}{value.name}(0x123456789abcdef0ull, {value.work_units}u)"
    if value.kind == "memory":
        return f"{prefix}{value.name}(data_values, 64u, {value.work_units // 64}u)"
    if value.kind == "transform":
        return (
            f"{prefix}{value.name}(data_values, destination_values, 64u, "
            f"{value.work_units // 64}u)"
        )
    if value.kind == "gather":
        return (
            f"{prefix}{value.name}(data_values, index_values, 64u, "
            f"{value.work_units // 64}u)"
        )
    if value.kind == "search":
        return (
            f"{prefix}{value.name}(ordered_values, 64u, "
            f"0x123456789abcdef0ull, {value.work_units}u)"
        )
    if value.kind == "floating":
        return f"{prefix}{value.name}(floating_left, 64u, {value.work_units // 64}u)"
    if value.kind == "floating_pair":
        return (
            f"{prefix}{value.name}(floating_left, floating_right, 64u, "
            f"{value.work_units // 64}u)"
        )
    raise BenchmarkError(f"unknown kernel kind: {value.kind}")


def runner_fragments(prefix: str) -> tuple[str, str, str]:
    """Generate declarations, checksum wrappers, and measurement calls."""

    declarations = [declaration(prefix, value) for value in KERNELS]
    wrappers: list[str] = []
    runs: list[str] = []
    for value in KERNELS:
        call = invocation(prefix, value)
        if value.kind.startswith("floating"):
            body = f"FloatingBits result; result.value = {call}; return result.bits;"
        else:
            body = f"return {call};"
        wrappers.append(f"static u64 invoke_{value.name}(void) {{ {body} }}")
        runs.append(
            f'    if (measure("{value.name}", invoke_{value.name}) != 0) return 1;'
        )
    return "\n".join(declarations), "\n".join(wrappers), "\n".join(runs)


def gcc_flags(level: str) -> tuple[str, ...]:
    """Return the Ultra64 toolchain's standalone VR4300/o32 policy."""

    return (
        f"-{level}",
        "-G0",
        "-fomit-frame-pointer",
        "-fno-PIC",
        "-mips3",
        "-march=vr4300",
        "-mtune=vr4300",
        "-mabi=32",
        "-mlong32",
        "-mgp32",
        "-mhard-float",
        "-mno-shared",
        "-mno-abicalls",
        "-mfix4300",
        "-mno-check-zero-division",
        "-mno-memcpy",
        "-mbranch-likely",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-common",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-fno-ident",
        "-ffp-contract=off",
        "-fno-lto",
    )


def clang_flags(level: str) -> tuple[str, ...]:
    """Return Clang's closest available standalone MIPS III/o32 policy."""

    return (
        f"-{level}",
        "--target=mips-unknown-elf",
        "-march=mips3",
        "-mtune=mips3",
        "-mabi=o32",
        "-mfp32",
        "-G0",
        "-fomit-frame-pointer",
        "-mno-abicalls",
        "-fno-pic",
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-common",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
        "-fno-ident",
        "-ffp-contract=off",
        "-fno-lto",
    )


def cross_flags(level: str) -> tuple[str, ...]:
    """Return the native Cross VR4300/o32 policy."""

    return (
        f"-{level}",
        "-mprofile=vr4300-o32",
        "-ffp-contract=off",
        "-fno-unwind-tables",
        "-fno-asynchronous-unwind-tables",
    )


async def inspect_object(
    config: Configuration, compiler: str, level: str, unit: str, path: Path
) -> BuildRecord:
    """Validate runtime dependencies and record code/load/object sizes."""

    undefined = await run_command(
        (config.tools.llvm_nm, "--undefined-only", "--format=posix", path)
    )
    symbols = parse_nm_symbols(undefined.stdout, "U")
    if symbols and compiler == "cross":
        raise BenchmarkError(
            f"{path.name} violates Cross's standalone contract: "
            + ", ".join(sorted(symbols))
        )
    unknown = symbols - config.libgcc_symbols
    if unknown:
        raise BenchmarkError(
            f"{path.name} has unresolved non-libgcc symbols: "
            + ", ".join(sorted(unknown))
        )
    text_bytes, load_bytes = await measure_sections(config, path)
    defined = await run_command(
        (
            config.tools.llvm_nm,
            "--defined-only",
            "--format=posix",
            "--print-size",
            "--radix=d",
            path,
        )
    )
    kernel_sizes = parse_nm_function_sizes(defined.stdout)
    expected = {value.name for value in KERNELS if value.unit == unit}
    if kernel_sizes.keys() != expected:
        missing = sorted(expected - kernel_sizes.keys())
        unexpected = sorted(kernel_sizes.keys() - expected)
        raise BenchmarkError(
            f"{path.name} function-size symbols disagree with {unit}: "
            f"missing={missing}, unexpected={unexpected}"
        )
    disassembly = await run_command(
        (config.tools.llvm_objdump, "--disassemble", "--no-show-raw-insn", path)
    )
    path.with_suffix(".disasm").write_text(
        disassembly.stdout, encoding="utf-8", newline="\n"
    )
    return BuildRecord(
        compiler,
        level,
        unit,
        path,
        text_bytes,
        load_bytes,
        path.stat().st_size,
        tuple(sorted(kernel_sizes.items())),
        tuple(sorted(symbols)),
    )


async def build_unit(
    config: Configuration, compiler: str, level: str, unit: str
) -> BuildRecord:
    """Build one equivalent source unit through a selected pipeline."""

    stem = f"{compiler}-{level}-{unit}"
    object_path = config.output_dir / f"{stem}.o"
    prefix = "bench_"
    if compiler in {"cross", "gcc-gimple", "gcc-rtl", "llc"}:
        source = config.output_dir / f"{stem}.x"
        write_rendered(
            config.kernel_dir / f"{unit}.x.in",
            source,
            {"ABI": "o32", "PREFIX": prefix},
        )
    else:
        source = config.output_dir / f"{stem}.c"
        write_rendered(config.kernel_dir / f"{unit}.c.in", source, {"PREFIX": prefix})

    if compiler == "cross":
        await run_command(
            (
                config.tools.cross_cc,
                "-c",
                *cross_flags(level),
                source,
                "-o",
                object_path,
            )
        )
    elif compiler in {"gcc-gimple", "gcc-rtl"}:
        gimple = config.output_dir / f"{stem}.gimple.c"
        emit = "-emit-gimple=rtl" if compiler == "gcc-rtl" else "-emit-gimple"
        await run_command(
            (
                config.tools.cross_cc,
                emit,
                *cross_flags(level),
                source,
                "-o",
                gimple,
            )
        )
        tree_dump = config.output_dir / f"{stem}.optimized.gimple"
        rtl_dump = config.output_dir / f"{stem}.final.rtl"
        await run_command(
            (
                config.tools.gcc,
                "-c",
                "-fgimple",
                *gcc_flags(level),
                f"-fdump-tree-optimized={tree_dump}",
                f"-fdump-rtl-final={rtl_dump}",
                gimple,
                "-o",
                object_path,
            )
        )
    elif compiler == "gcc":
        tree_dump = config.output_dir / f"{stem}.optimized.gimple"
        rtl_dump = config.output_dir / f"{stem}.final.rtl"
        await run_command(
            (
                config.tools.gcc,
                "-std=c11",
                "-c",
                *gcc_flags(level),
                f"-fdump-tree-optimized={tree_dump}",
                f"-fdump-rtl-final={rtl_dump}",
                source,
                "-o",
                object_path,
            )
        )
    elif compiler == "clang":
        await run_command(
            (
                config.tools.clang,
                "-std=c11",
                "-c",
                *clang_flags(level),
                source,
                "-o",
                object_path,
            )
        )
    elif compiler == "llc":
        input_ir = config.output_dir / f"{stem}.ll"
        optimized_ir = config.output_dir / f"{stem}.opt.ll"
        await run_command(
            (
                config.tools.cross_cc,
                "-emit-llvm",
                *cross_flags(level),
                source,
                "-o",
                input_ir,
            )
        )
        await run_command(
            (
                config.tools.opt,
                f"-{level}",
                input_ir,
                "-S",
                "-o",
                optimized_ir,
            )
        )
        llc_level = "O3" if level == "O3" else "O2"
        await run_command(
            (
                config.tools.llc,
                f"-{llc_level}",
                "-mtriple=mips-unknown-elf",
                "-mcpu=mips3",
                "-target-abi=o32",
                "-mattr=+noabicalls",
                "-filetype=obj",
                optimized_ir,
                "-o",
                object_path,
            )
        )
    else:
        raise BenchmarkError(f"unknown compiler pipeline: {compiler}")
    return await inspect_object(config, compiler, level, unit, object_path)


def first_diagnostic(result: CommandResult) -> str:
    """Return one concise line describing a failed optional probe."""

    def stable(line: str) -> str:
        return re.sub(r"0x[0-9a-fA-F]+", "<node>", line.strip())

    lines = tuple(result.stderr.splitlines()) + tuple(result.stdout.splitlines())
    priorities = ("LLVM ERROR", "Cannot select", "not supported", "error:")
    for marker in priorities:
        for line in lines:
            stripped = line.strip()
            if marker.lower() in stripped.lower():
                return stable(stripped)
    for line in lines:
        stripped = line.strip()
        if stripped:
            return stable(stripped)
    return f"exit status {result.returncode}"


async def probe_llc(config: Configuration) -> tuple[bool, str]:
    """Probe the complete representative Cross-LLVM-MIPS path."""

    source = config.output_dir / "llc-probe.x"
    input_ir = config.output_dir / "llc-probe.ll"
    optimized_ir = config.output_dir / "llc-probe.opt.ll"
    output = config.output_dir / "llc-probe.o"
    write_rendered(
        config.kernel_dir / "scalar.x.in",
        source,
        {"ABI": "o32", "PREFIX": "probe_"},
    )
    commands: tuple[tuple[str | Path, ...], ...] = (
        (
            config.tools.cross_cc,
            "-emit-llvm",
            *cross_flags("O3"),
            source,
            "-o",
            input_ir,
        ),
        (config.tools.opt, "-O3", input_ir, "-S", "-o", optimized_ir),
        (
            config.tools.llc,
            "-O3",
            "-mtriple=mips-unknown-elf",
            "-mcpu=mips3",
            "-target-abi=o32",
            "-mattr=+noabicalls",
            "-filetype=obj",
            optimized_ir,
            "-o",
            output,
        ),
    )
    for command in commands:
        result = await run_command(command, check=False)
        if result.returncode != 0:
            return False, first_diagnostic(result)
    return True, "representative u64 MIPS III/o32 lowering succeeded"


async def probe_clang(config: Configuration) -> tuple[bool, str]:
    """Probe direct Clang lowering of a representative full-width C unit."""

    source = config.output_dir / "clang-probe.c"
    output = config.output_dir / "clang-probe.o"
    write_rendered(
        config.kernel_dir / "scalar.c.in",
        source,
        {"PREFIX": "probe_"},
    )
    result = await run_command(
        (
            config.tools.clang,
            "-std=c11",
            "-c",
            *clang_flags("O3"),
            source,
            "-o",
            output,
        ),
        check=False,
    )
    if result.returncode != 0:
        return False, first_diagnostic(result)
    return True, "representative u64 MIPS III/o32 lowering succeeded"


async def build_runner(config: Configuration) -> tuple[Path, Path]:
    """Build the shared o32 benchmark runner and startup objects."""

    declarations, wrappers, runs = runner_fragments("bench_")
    source = config.output_dir / "runner.c"
    runner_object = config.output_dir / "runner.o"
    startup_object = config.output_dir / "startup.o"
    write_rendered(
        config.source_dir / "runner.c.in",
        source,
        {
            "DECLARATIONS": declarations,
            "WRAPPERS": wrappers,
            "RUNS": runs,
            "SAMPLES": str(config.samples),
        },
    )
    await run_command(
        (
            config.tools.gcc,
            "-std=c11",
            "-c",
            *gcc_flags("O2"),
            source,
            "-o",
            runner_object,
        )
    )
    await run_command(
        (
            config.tools.llvm_mc,
            "--filetype=obj",
            "--triple=mips-unknown-elf",
            "--mcpu=mips3",
            "--mattr=+noabicalls",
            config.source_dir / "startup.s",
            "-o",
            startup_object,
        )
    )
    return runner_object, startup_object


def parse_runtime_output(
    output: str, compiler: str, level: str, samples: int
) -> list[RuntimeRecord]:
    """Validate UART CSV and reduce samples to per-kernel medians."""

    header = "kernel,sample,ticks,checksum"
    lines = output.splitlines()
    try:
        start = lines.index(header)
    except ValueError as error:
        raise BenchmarkError("QEMU output contains no benchmark CSV header") from error
    reader = csv.DictReader(io.StringIO("\n".join(lines[start:])))
    grouped: defaultdict[str, list[tuple[int, str]]] = defaultdict(list)
    try:
        for row in reader:
            grouped[row["kernel"]].append(
                (int(row["ticks"], 16), row["checksum"].lower())
            )
    except (KeyError, TypeError, ValueError) as error:
        raise BenchmarkError("QEMU emitted an invalid benchmark CSV row") from error
    expected_names = {value.name for value in KERNELS}
    if set(grouped) != expected_names:
        missing = sorted(expected_names - set(grouped))
        extra = sorted(set(grouped) - expected_names)
        raise BenchmarkError(
            f"QEMU kernel set mismatch; missing={missing}, extra={extra}"
        )
    result: list[RuntimeRecord] = []
    by_name = {value.name: value for value in KERNELS}
    for name, measurements in grouped.items():
        if len(measurements) != samples:
            raise BenchmarkError(
                f"{name} emitted {len(measurements)} samples instead of {samples}"
            )
        ticks = [entry[0] for entry in measurements]
        checksums = {entry[1] for entry in measurements}
        if any(value <= 0 for value in ticks) or len(checksums) != 1:
            raise BenchmarkError(f"{name} emitted invalid ticks or unstable checksums")
        descriptor = by_name[name]
        median_ticks = int(statistics.median(ticks))
        result.append(
            RuntimeRecord(
                compiler,
                level,
                descriptor.category,
                name,
                descriptor.work_units,
                median_ticks,
                median_ticks / descriptor.work_units,
                checksums.pop(),
            )
        )
    return sorted(result, key=lambda value: value.kernel)


async def run_variant(
    config: Configuration,
    compiler: str,
    level: str,
    builds: Sequence[BuildRecord],
    runner_object: Path,
    startup_object: Path,
    harness_size: tuple[int, int],
) -> tuple[list[RuntimeRecord], ImageRecord]:
    """Link, wrap, and execute one compiler/level bare-metal image."""

    stem = f"{compiler}-{level}"
    elf32 = config.output_dir / f"{stem}.elf32"
    image = config.output_dir / f"{stem}.bin"
    wrapper_source = config.output_dir / f"{stem}.wrapper.s"
    wrapper_object = config.output_dir / f"{stem}.wrapper.o"
    elf64 = config.output_dir / f"{stem}.elf64"
    link_map = config.output_dir / f"{stem}.map"
    runtime_symbols = tuple(
        sorted({symbol for build in builds for symbol in build.runtime_symbols})
    )
    support_archives: tuple[Path, ...] = (config.libgcc,) if runtime_symbols else ()
    await run_command(
        (
            config.tools.target_ld,
            "-m",
            "elf32ebmip",
            "-T",
            config.source_dir / "bare.ld",
            startup_object,
            runner_object,
            *(build.object_path for build in builds),
            *support_archives,
            "-Map",
            link_map,
            "-o",
            elf32,
        )
    )
    undefined = await run_command(
        (config.tools.llvm_nm, "--undefined-only", "--format=posix", elf32)
    )
    unresolved = parse_nm_symbols(undefined.stdout, "U")
    if unresolved:
        raise BenchmarkError(
            f"{elf32.name} remains unresolved after linking: "
            + ", ".join(sorted(unresolved))
        )
    linked_text, linked_load = await measure_sections(config, elf32)
    unit_text = sum(build.text_bytes for build in builds)
    unit_load = sum(build.load_bytes for build in builds)
    benchmark_text = linked_text - harness_size[0]
    benchmark_load = linked_load - harness_size[1]
    support_text = benchmark_text - unit_text
    support_load = benchmark_load - unit_load
    if min(benchmark_text, benchmark_load, support_text, support_load) < 0:
        raise BenchmarkError(
            f"{elf32.name} has inconsistent linked-size accounting: "
            f"benchmark=({benchmark_text}, {benchmark_load}), "
            f"support=({support_text}, {support_load})"
        )
    image_record = ImageRecord(
        compiler,
        level,
        elf32,
        linked_text,
        linked_load,
        benchmark_text,
        benchmark_load,
        support_text,
        support_load,
        runtime_symbols,
    )
    await run_command((config.tools.llvm_objcopy, "-O", "binary", elf32, image))
    write_rendered(
        config.source_dir / "wrapper.s.in",
        wrapper_source,
        {"IMAGE": image.resolve().as_posix()},
    )
    await run_command(
        (
            config.tools.llvm_mc,
            "--filetype=obj",
            "--triple=mips64-unknown-elf",
            "--mcpu=mips3",
            wrapper_source,
            "-o",
            wrapper_object,
        )
    )
    await run_command(
        (
            config.tools.lld,
            "-m",
            "elf64btsmip",
            "-T",
            config.source_dir / "wrapper.ld",
            wrapper_object,
            "-o",
            elf64,
        )
    )
    executed = await run_command(
        (
            config.tools.qemu,
            "-M",
            "malta",
            "-cpu",
            "R4000",
            "-m",
            "64M",
            "-bios",
            "none",
            "-kernel",
            elf64,
            "-display",
            "none",
            "-serial",
            "none",
            "-serial",
            "none",
            "-serial",
            "stdio",
            "-monitor",
            "none",
            "-no-reboot",
            "-semihosting",
            "-icount",
            "shift=0,align=off,sleep=off",
        ),
        timeout=config.timeout_seconds,
        check=False,
    )
    (config.output_dir / f"{stem}.uart.csv").write_text(
        executed.stdout, encoding="utf-8", newline="\n"
    )
    if executed.returncode != 0:
        raise BenchmarkError(
            f"QEMU returned {executed.returncode} for {compiler}/{level}\n"
            f"stdout:\n{executed.stdout}\nstderr:\n{executed.stderr}"
        )
    return (
        parse_runtime_output(executed.stdout, compiler, level, config.samples),
        image_record,
    )


def verify_checksums(records: Sequence[RuntimeRecord]) -> None:
    """Require bit-exact results across every compiler and preset."""

    grouped: defaultdict[str, set[str]] = defaultdict(set)
    for record in records:
        grouped[record.kernel].add(record.checksum)
    mismatched = {name: values for name, values in grouped.items() if len(values) != 1}
    if mismatched:
        details = ", ".join(
            f"{name}={sorted(values)}" for name, values in sorted(mismatched.items())
        )
        raise BenchmarkError(f"compiler results disagree: {details}")


def geometric_mean(values: Iterable[float]) -> float:
    """Return the geometric mean of a non-empty positive sequence."""

    materialized = tuple(values)
    if not materialized or any(value <= 0.0 for value in materialized):
        raise BenchmarkError("geometric mean requires positive values")
    return math.exp(sum(math.log(value) for value in materialized) / len(materialized))


def runtime_score(records: Sequence[RuntimeRecord], compiler: str, level: str) -> float:
    """Return the equal-category-weighted ticks-per-work score."""

    selected = [
        value
        for value in records
        if value.compiler == compiler and value.level == level
    ]
    categories: defaultdict[str, list[float]] = defaultdict(list)
    for value in selected:
        categories[value.category].append(value.ticks_per_unit)
    if not categories:
        raise BenchmarkError(f"no runtime records for {compiler}/{level}")
    return geometric_mean(geometric_mean(values) for values in categories.values())


def category_score(
    records: Sequence[RuntimeRecord], compiler: str, level: str, category: str
) -> float:
    """Return one category's equal-kernel geometric mean."""

    return geometric_mean(
        value.ticks_per_unit
        for value in records
        if value.compiler == compiler
        and value.level == level
        and value.category == category
    )


def write_csv_files(
    config: Configuration,
    builds: Sequence[BuildRecord],
    images: Sequence[ImageRecord],
    runtimes: Sequence[RuntimeRecord],
) -> None:
    """Write stable machine-readable build and runtime tables."""

    with (config.output_dir / "build.csv").open(
        "w", newline="", encoding="utf-8"
    ) as file:
        writer = csv.writer(file)
        writer.writerow(
            (
                "compiler",
                "level",
                "unit",
                "text_bytes",
                "load_bytes",
                "object_bytes",
                "runtime_symbols",
            )
        )
        for build in builds:
            writer.writerow(
                (
                    build.compiler,
                    build.level,
                    build.unit,
                    build.text_bytes,
                    build.load_bytes,
                    build.object_bytes,
                    ";".join(build.runtime_symbols),
                )
            )
    with (config.output_dir / "code_size.csv").open(
        "w", newline="", encoding="utf-8"
    ) as file:
        writer = csv.writer(file)
        writer.writerow(
            ("compiler", "level", "category", "kernel", "unit", "function_bytes")
        )
        kernels = {value.name: value for value in KERNELS}
        for build in builds:
            for name, size in build.kernel_sizes:
                value = kernels[name]
                writer.writerow(
                    (
                        build.compiler,
                        build.level,
                        value.category,
                        name,
                        build.unit,
                        size,
                    )
                )
    with (config.output_dir / "image.csv").open(
        "w", newline="", encoding="utf-8"
    ) as file:
        field_names = tuple(ImageRecord.__dataclass_fields__)
        image_writer = csv.DictWriter(file, fieldnames=field_names)
        image_writer.writeheader()
        for image in images:
            row = asdict(image)
            row["elf_path"] = image.elf_path.name
            row["runtime_symbols"] = ";".join(image.runtime_symbols)
            image_writer.writerow(row)
    with (config.output_dir / "runtime.csv").open(
        "w", newline="", encoding="utf-8"
    ) as file:
        writer = csv.writer(file)
        writer.writerow(
            (
                "compiler",
                "level",
                "category",
                "kernel",
                "work_units",
                "median_ticks",
                "ticks_per_unit",
                "checksum",
            )
        )
        for runtime in runtimes:
            writer.writerow(
                (
                    runtime.compiler,
                    runtime.level,
                    runtime.category,
                    runtime.kernel,
                    runtime.work_units,
                    runtime.median_ticks,
                    f"{runtime.ticks_per_unit:.9f}",
                    runtime.checksum,
                )
            )


def generate_report(
    config: Configuration,
    compilers: Sequence[str],
    builds: Sequence[BuildRecord],
    images: Sequence[ImageRecord],
    runtimes: Sequence[RuntimeRecord],
    clang_probe: str,
    llc_probe: str,
) -> str:
    """Render the human-readable comparison report."""

    lines = [
        "# MIPS III/o32 code-generation comparison",
        "",
        f"Revision: `{config.revision}`  ",
        "Target: VR4300 / MIPS III / big-endian o32  ",
        (
            "Runtime metric: median QEMU `-icount` CP0 Count ticks; lower is "
            "better. This is a deterministic dynamic-execution proxy, not a "
            "VR4300 hardware-cycle claim."
        ),
        "",
        "## Pipelines",
        "",
    ]
    for compiler in compilers:
        lines.append(f"- `{compiler}`: {PIPELINES[compiler]}.")
    lines.extend(
        (
            f"- Direct Clang probe: {clang_probe}.",
            f"- LLVM `llc` probe: {llc_probe}.",
            "",
        )
    )
    for level in config.levels:
        lines.extend((f"## {level}", "", "### Aggregate", ""))
        lines.append(
            "| pipeline | ticks/work | Cross / row | text bytes | Cross / row | load bytes |"
        )
        lines.append("|---|---:|---:|---:|---:|---:|")
        cross_runtime = runtime_score(runtimes, "cross", level)
        level_images = {
            value.compiler: value for value in images if value.level == level
        }
        cross_text = level_images["cross"].benchmark_text_bytes
        for compiler in compilers:
            score = runtime_score(runtimes, compiler, level)
            image = level_images[compiler]
            text = image.benchmark_text_bytes
            load = image.benchmark_load_bytes
            lines.append(
                f"| {compiler} | {score:.4f} | {cross_runtime / score:.3f}x | "
                f"{text} | {cross_text / text:.3f}x | {load} |"
            )
        lines.extend(
            ("", "`Cross / row > 1` means native Cross is slower or larger.", "")
        )
        lines.extend(("### Runtime support", ""))
        lines.append(
            "| pipeline | referenced compiler-runtime symbols | support/padding text | support/padding load |"
        )
        lines.append("|---|---|---:|---:|")
        for compiler in compilers:
            image = level_images[compiler]
            symbols = ", ".join(image.runtime_symbols) or "none"
            lines.append(
                f"| {compiler} | {symbols} | {image.support_text_bytes} | "
                f"{image.support_load_bytes} |"
            )
        lines.append("")
        categories = tuple(dict.fromkeys(value.category for value in KERNELS))
        lines.extend(("### Runtime categories", ""))
        lines.append("| category | " + " | ".join(compilers) + " |")
        lines.append("|---|" + "---:|" * len(compilers))
        for category in categories:
            cross = category_score(runtimes, "cross", level, category)
            ratios = [
                f"{cross / category_score(runtimes, compiler, level, category):.3f}x"
                for compiler in compilers
            ]
            lines.append(f"| {category} | " + " | ".join(ratios) + " |")
        lines.extend(("", "Each cell is `Cross ticks / pipeline ticks`.", ""))
        if level == "O3":
            lines.extend(("### Kernel runtime", ""))
            lines.append(
                "| kernel | " + " | ".join(compilers) +
                " | Cross / fastest peer |"
            )
            lines.append("|---|" + "---:|" * (len(compilers) + 1))
            for kernel_value in KERNELS:
                scores = {
                    compiler: next(
                        value.ticks_per_unit
                        for value in runtimes
                        if value.compiler == compiler
                        and value.level == level
                        and value.kernel == kernel_value.name
                    )
                    for compiler in compilers
                }
                peers = {
                    compiler: score
                    for compiler, score in scores.items()
                    if compiler != "cross"
                }
                best_peer = min(peers.values())
                lines.append(
                    f"| {kernel_value.name} | " +
                    " | ".join(f"{scores[name]:.4f}" for name in compilers) +
                    f" | {scores['cross'] / best_peer:.3f}x |"
                )
            lines.extend(
                ("", "Runtime cells are ticks per declared work unit.", "")
            )
        if level == "Oz":
            size_rows = {
                (build.compiler, name): size
                for build in builds
                if build.level == level
                for name, size in build.kernel_sizes
            }
            lines.extend(("### Kernel function size", ""))
            lines.append(
                "| kernel | " + " | ".join(compilers) +
                " | Cross / smallest peer |"
            )
            lines.append("|---|" + "---:|" * (len(compilers) + 1))
            for kernel_value in KERNELS:
                sizes = {
                    compiler: size_rows[(compiler, kernel_value.name)]
                    for compiler in compilers
                }
                best_peer = min(
                    size for compiler, size in sizes.items()
                    if compiler != "cross"
                )
                lines.append(
                    f"| {kernel_value.name} | " +
                    " | ".join(str(sizes[name]) for name in compilers) +
                    f" | {sizes['cross'] / best_peer:.3f}x |"
                )
            lines.extend(
                ("", "Function-size cells are symbol bytes; whole-image totals above also charge shared runtime support and linker padding.", "")
            )
    lines.extend(
        (
            "## Interpretation limits",
            "",
            "- Native Cross objects must be strictly standalone. Peer compiler-runtime references are resolved from GCC's target libgcc, named in the report, and charged to linked benchmark size.",
            "- `support/padding` is the final linked benchmark image minus the common harness and measured source-unit objects; it therefore includes extracted runtime members and linker alignment.",
            "- Per-kernel function sizes measure each public symbol. Shared compiler-runtime members cannot be attributed to one function and remain charged only in the whole-image totals.",
            "- All images use the same GCC-built runner, startup, linker scripts, data, and checksums.",
            "- GCC is tuned for `vr4300`; LLVM exposes only generic `mips3` here.",
            "- CP0 Count under QEMU `-icount` is useful for relative dynamic work but does not model VR4300 cache and pipeline timing.",
            "- Exact generated GIMPLE, optimized-GIMPLE dumps, final RTL dumps, objects, and disassemblies accompany this report.",
            "",
        )
    )
    return "\n".join(lines)


async def tool_versions(tools: Tools) -> dict[str, str]:
    """Capture concise compiler and emulator identities."""

    versions: dict[str, str] = {}
    for name, executable in (
        ("cross", tools.cross_cc),
        ("gcc", tools.gcc),
        ("clang", tools.clang),
        ("opt", tools.opt),
        ("llc", tools.llc),
        ("target-ld", tools.target_ld),
        ("qemu", tools.qemu),
    ):
        result = await run_command((executable, "--version"), check=False)
        lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
        if not lines:
            lines = [
                line.strip() for line in result.stderr.splitlines() if line.strip()
            ]
        versions[name] = lines[0] if lines else f"exit status {result.returncode}"
    return versions


def sha256_file(path: Path) -> str:
    """Return one file's SHA-256 identity."""

    digest = hashlib.sha256()
    with path.open("rb") as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def slugify(value: str, limit: int = 64) -> str:
    """Return a bounded portable directory component."""

    result = re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")
    return (result or "unnamed")[:limit].rstrip("-")


async def discover_revision(trunk: Path) -> tuple[str, str]:
    """Return full revision identity and a commit-addressed directory name."""

    git = ("git", "-C", trunk)
    commit = (await run_command((*git, "rev-parse", "HEAD"))).stdout.strip()
    subject = (
        await run_command((*git, "show", "-s", "--format=%s", "HEAD"))
    ).stdout.strip()
    dirty = bool(
        (await run_command((*git, "status", "--porcelain"))).stdout.strip()
    )
    directory = f"{commit[:8]}-{slugify(subject)}" + ("-dirty" if dirty else "")
    return commit + ("-dirty" if dirty else ""), directory


async def discover_libgcc(gcc: Path, llvm_nm: Path) -> tuple[Path, frozenset[str]]:
    """Find the selected o32 libgcc and inventory its resolvable symbols."""

    located = await run_command(
        (gcc, "-mabi=32", "-march=vr4300", "-print-libgcc-file-name")
    )
    output = located.stdout.strip()
    if not output:
        raise BenchmarkError("MIPS GCC did not report an o32 libgcc path")
    libgcc = Path(output).resolve()
    if not libgcc.is_file():
        raise BenchmarkError(f"MIPS GCC reported a missing libgcc: {libgcc}")
    inventory = await run_command((llvm_nm, "--defined-only", "--format=posix", libgcc))
    symbols = parse_nm_symbols(inventory.stdout)
    if not symbols:
        raise BenchmarkError(f"libgcc contains no visible definitions: {libgcc}")
    return libgcc, symbols


def default_gcc(source_dir: Path) -> str:
    suffix = ".exe" if sys.platform == "win32" else ""
    return f"mips64-elf-gcc{suffix}"


def default_cross_cc(source_dir: Path) -> str:
    """Return the conventional in-workspace Cross compiler path."""

    suffix = ".exe" if sys.platform == "win32" else ""
    return str(source_dir.parents[2] / "build" / f"cc{suffix}")


def parse_arguments(argv: Sequence[str]) -> argparse.Namespace:
    """Parse the benchmark command line."""

    source_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cross-cc", default=default_cross_cc(source_dir))
    parser.add_argument("--gcc", default=default_gcc(source_dir))
    parser.add_argument("--clang", default="clang")
    parser.add_argument("--opt", default="opt")
    parser.add_argument("--llc", default="llc")
    parser.add_argument("--llvm-mc", default="llvm-mc")
    parser.add_argument("--llvm-objcopy", default="llvm-objcopy")
    parser.add_argument("--llvm-size", default="llvm-size")
    parser.add_argument("--llvm-nm", default="llvm-nm")
    parser.add_argument("--llvm-objdump", default="llvm-objdump")
    parser.add_argument("--target-ld")
    parser.add_argument("--lld", default="ld.lld")
    parser.add_argument("--qemu", default="qemu-system-mips64")
    parser.add_argument("--levels", default="O3,Oz")
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--timeout-seconds", type=int, default=120)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--run-name")
    return parser.parse_args(argv)


async def configuration_from_arguments(
    arguments: argparse.Namespace,
) -> Configuration:
    """Validate arguments and resolve all external tools."""

    if arguments.output is not None and arguments.run_name is not None:
        raise BenchmarkError("--output and --run-name are mutually exclusive")
    if arguments.samples <= 0 or arguments.samples % 2 == 0:
        raise BenchmarkError("--samples must be a positive odd number")
    if arguments.timeout_seconds <= 0:
        raise BenchmarkError("--timeout-seconds must be positive")
    source_dir = Path(__file__).resolve().parent
    trunk = source_dir.parents[1]
    revision, revision_directory = await discover_revision(trunk)
    levels = parse_levels(arguments.levels)
    if arguments.output is not None:
        output_dir = arguments.output.resolve()
    else:
        run_name = arguments.run_name or time.strftime("%Y%m%d-%H%M%S")
        output_dir = (
            source_dir.parents[2]
            / "build"
            / "benchmark"
            / "mips32"
            / revision_directory
            / slugify(run_name)
        )
    gcc = resolve_tool(arguments.gcc, "Ultra64 MIPS GCC")
    if arguments.target_ld is not None:
        target_ld = resolve_tool(arguments.target_ld, "MIPS GNU linker")
    else:
        reported_ld = (await run_command((gcc, "-print-prog-name=ld"))).stdout.strip()
        if not reported_ld:
            raise BenchmarkError("MIPS GCC did not report its target linker")
        target_ld = resolve_tool(reported_ld, "MIPS GNU linker")
    tools = Tools(
        cross_cc=resolve_tool(arguments.cross_cc, "Cross compiler"),
        gcc=gcc,
        clang=resolve_tool(arguments.clang, "Clang"),
        opt=resolve_tool(arguments.opt, "LLVM optimizer"),
        llc=resolve_tool(arguments.llc, "LLVM code generator"),
        llvm_mc=resolve_tool(arguments.llvm_mc, "LLVM assembler"),
        llvm_objcopy=resolve_tool(arguments.llvm_objcopy, "LLVM objcopy"),
        llvm_size=resolve_tool(arguments.llvm_size, "LLVM size"),
        llvm_nm=resolve_tool(arguments.llvm_nm, "LLVM nm"),
        llvm_objdump=resolve_tool(arguments.llvm_objdump, "LLVM objdump"),
        target_ld=target_ld,
        lld=resolve_tool(arguments.lld, "LLD"),
        qemu=resolve_tool(arguments.qemu, "QEMU MIPS64 system emulator"),
    )
    libgcc, libgcc_symbols = await discover_libgcc(tools.gcc, tools.llvm_nm)
    return Configuration(
        source_dir=source_dir,
        kernel_dir=source_dir.parent / "x86_64" / "kernels",
        output_dir=output_dir,
        tools=tools,
        levels=levels,
        samples=arguments.samples,
        timeout_seconds=arguments.timeout_seconds,
        revision=revision,
        revision_directory=revision_directory,
        libgcc=libgcc,
        libgcc_symbols=libgcc_symbols,
    )


async def async_main(argv: Sequence[str]) -> int:
    """Run compilation, execution, validation, and reporting."""

    arguments = parse_arguments(argv)
    config = await configuration_from_arguments(arguments)
    config.output_dir.mkdir(parents=True, exist_ok=True)
    versions = await tool_versions(config.tools)
    clang_supported, clang_message = await probe_clang(config)
    llc_supported, llc_message = await probe_llc(config)
    optional_compilers = (
        *(("clang",) if clang_supported else ()),
        *(("llc",) if llc_supported else ()),
    )
    compilers = (*BASE_COMPILERS, *optional_compilers)
    print(f"Direct Clang probe: {clang_message}", flush=True)
    print(f"LLVM llc probe: {llc_message}", flush=True)

    builds: list[BuildRecord] = []
    for level in config.levels:
        for compiler in compilers:
            print(f"building {compiler}/{level} ...", flush=True)
            for unit in UNITS:
                builds.append(await build_unit(config, compiler, level, unit))

    runner_object, startup_object = await build_runner(config)
    runner_size = await measure_sections(config, runner_object)
    startup_size = await measure_sections(config, startup_object)
    harness_size = (
        runner_size[0] + startup_size[0],
        runner_size[1] + startup_size[1],
    )
    runtimes: list[RuntimeRecord] = []
    images: list[ImageRecord] = []
    for level in config.levels:
        for compiler in compilers:
            print(f"running {compiler}/{level} ...", flush=True)
            selected_builds = [
                value
                for value in builds
                if value.compiler == compiler and value.level == level
            ]
            variant_runtimes, image = await run_variant(
                config,
                compiler,
                level,
                selected_builds,
                runner_object,
                startup_object,
                harness_size,
            )
            runtimes.extend(variant_runtimes)
            images.append(image)
    verify_checksums(runtimes)
    write_csv_files(config, builds, images, runtimes)
    clang_probe_text = (
        "supported and included" if clang_supported else f"excluded: {clang_message}"
    )
    probe_text = (
        "supported and included" if llc_supported else f"excluded: {llc_message}"
    )
    report = generate_report(
        config,
        compilers,
        builds,
        images,
        runtimes,
        clang_probe_text,
        probe_text,
    )
    (config.output_dir / "report.md").write_text(report, encoding="utf-8", newline="\n")
    (config.output_dir / "metadata.json").write_text(
        json.dumps(
            {
                "host": platform.platform(),
                "generated_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
                "revision": config.revision,
                "levels": config.levels,
                "samples": config.samples,
                "compilers": compilers,
                "pipelines": {name: PIPELINES[name] for name in compilers},
                "llc_probe": {
                    "supported": llc_supported,
                    "message": llc_message,
                },
                "clang_probe": {
                    "supported": clang_supported,
                    "message": clang_message,
                },
                "tools": versions,
                "libgcc": {
                    "path": str(config.libgcc),
                    "sha256": sha256_file(config.libgcc),
                },
                "cross_compiler_sha256": sha256_file(config.tools.cross_cc),
                "kernels": [asdict(value) for value in KERNELS],
            },
            indent=2,
        )
        + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"report: {config.output_dir / 'report.md'}", flush=True)
    return 0


def main() -> int:
    """Translate benchmark failures into a concise CLI diagnostic."""

    try:
        return asyncio.run(async_main(sys.argv[1:]))
    except BenchmarkError as error:
        print(f"benchmark: error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
