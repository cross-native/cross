"""Unit tests for the MIPS32 benchmark orchestrator."""

from __future__ import annotations

import importlib.util
import sys
from pathlib import Path
from types import ModuleType

import pytest


def load_benchmark_module() -> ModuleType:
    """Load the adjacent CLI module without requiring a package."""

    path = Path(__file__).with_name("run.py")
    specification = importlib.util.spec_from_file_location("cross_mips_benchmark", path)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    sys.modules[specification.name] = module
    specification.loader.exec_module(module)
    return module


benchmark = load_benchmark_module()


def test_parse_levels_is_stable_and_rejects_non_objectives() -> None:
    """Only the speed and minimum-size objectives belong in this corpus."""

    assert benchmark.parse_levels("O3,-Oz,O3") == ("O3", "Oz")
    with pytest.raises(benchmark.BenchmarkError, match="O3 and Oz only"):
        benchmark.parse_levels("O2")


def test_render_template_rejects_unresolved_fields() -> None:
    """Generated benchmark sources may not retain placeholders."""

    with pytest.raises(benchmark.BenchmarkError, match="unresolved"):
        benchmark.render_template("@FIRST@ @SECOND@", {"FIRST": "done"})


def test_parse_size_sections_excludes_metadata() -> None:
    """Load bytes include image data but exclude MIPS object metadata."""

    output = """object.o  :
section          size   addr
.text              40      0
.text.hot           8      0
.rodata            12      0
.data               4      0
.pdr               64      0
.MIPS.abiflags     24      0
Total             152
"""
    assert benchmark.parse_size_sections(output) == (48, 64)


def test_parse_nm_symbols_handles_objects_and_archive_headers() -> None:
    """Runtime dependency accounting must ignore LLVM nm archive headings."""

    output = """udivdi3.o:
__udivdi3 T 0 540

kernel.o:
__umoddi3 U 0 0
"""
    assert benchmark.parse_nm_symbols(output) == {"__udivdi3", "__umoddi3"}
    assert benchmark.parse_nm_symbols(output, "U") == {"__umoddi3"}


def test_parse_nm_function_sizes_uses_decimal_symbol_bytes() -> None:
    """Per-kernel size accounting ignores local labels and helper symbols."""

    output = """.Lblock t 0 0
bench_mix T 0 364
helper T 364 24
bench_narrow T 388 264
"""
    assert benchmark.parse_nm_function_sizes(output) == {
        "mix": 364,
        "narrow": 264,
    }


def test_parse_hotblocks_normalizes_mips32_addresses() -> None:
    """QEMU sign-extended PCs must map back to linked ELF32 addresses."""

    output = """collected 3 entries in the hash table
pc, tcount, icount, ecount
0xffffffff80101234, 1, 4, 7
0xffffffff80102000, 1, 2, 0
0xffffffffbfc00000, 1, 1, 3
"""
    assert benchmark.parse_hotblocks(output) == (
        benchmark.HotBlock(0x80101234, 4, 7),
        benchmark.HotBlock(0xBFC00000, 1, 3),
    )
    with pytest.raises(benchmark.BenchmarkError, match="duplicate PC"):
        benchmark.parse_hotblocks(
            "pc,tcount,icount,ecount\n0x1000,1,1,1\n0x1000,1,1,1\n"
        )


def test_link_map_ranges_select_candidate_and_runtime_text() -> None:
    """Trace attribution excludes startup/runner sections and linker fill."""

    output = """ .text          0x80100000       0x40 C:\\out\\startup.o
 .text          0x80100040       0x80 C:\\out\\candidate.o
 .text.__udivdi3
                0x801000c0       0x30 C:\\tools\\libgcc.a(_udivdi3.o)
 *fill*         0x801000f0        0x8
"""
    assert benchmark.parse_link_map_code_ranges(
        output, ("C:/out/candidate.o", "libgcc.a")
    ) == (
        benchmark.CodeRange(0x80100040, 0x801000C0, "C:\\out\\candidate.o"),
        benchmark.CodeRange(
            0x801000C0,
            0x801000F0,
            "C:\\tools\\libgcc.a(_udivdi3.o)",
        ),
    )


def test_disassembly_selection_and_mca_render_preserve_delay_slots() -> None:
    """Every QEMU-counted instruction, including a zero NOP, reaches MCA."""

    disassembly = """Disassembly of section .text:
80100040: addiu $2, $2, 0x1
80100044: bne $2, $4, 0x80100040 <loop>
80100048: nop
"""
    block = benchmark.HotBlock(0x80100040, 3, 8)
    selected = benchmark.select_candidate_blocks(
        (block,),
        (benchmark.CodeRange(0x80100040, 0x8010004C, "candidate.o"),),
        benchmark.parse_disassembly(disassembly),
    )
    source, counts = benchmark.render_mca_source(selected)
    assert counts == {"tb_0": 3}
    assert "bne $2, $4, .Lmca_target_0" in source
    assert "        nop" in source


def test_mca_cost_parsing_and_hotness_weighting() -> None:
    """Dependency and resource costs use the same dynamic TB weights."""

    document = {
        "CodeRegions": [
            {
                "Name": "tb_0",
                "SummaryView": {
                    "Iterations": 100,
                    "Instructions": 200,
                    "TotalCycles": 301,
                    "BlockRThroughput": 2,
                },
            },
            {
                "Name": "tb_1",
                "SummaryView": {
                    "Iterations": 100,
                    "Instructions": 100,
                    "TotalCycles": 101,
                    "BlockRThroughput": 1,
                },
            },
        ]
    }
    costs = benchmark.parse_mca_costs(
        benchmark.json.dumps(document), {"tb_0": 2, "tb_1": 1}, 100
    )
    descriptor = benchmark.Kernel("probe", "integer", "scalar", "scalar", 5)
    blocks = (
        (benchmark.HotBlock(0x1000, 2, 4), ("addu $2, $2, $3",) * 2),
        (benchmark.HotBlock(0x2000, 1, 2), ("nop",)),
    )
    record = benchmark.aggregate_pipeline_record(
        "cross", "O3", descriptor, blocks, costs, invocations=2
    )
    assert record.dynamic_instructions == 5
    assert record.instructions_per_unit == pytest.approx(1.0)
    assert record.modeled_cycles == pytest.approx(7.03)
    assert record.throughput_cycles == pytest.approx(5.0)


def test_pipeline_timing_cli_is_explicit_and_bounded() -> None:
    """The expensive modeled metric remains opt-in and reproducible."""

    defaults = benchmark.parse_arguments([])
    assert not defaults.pipeline_timing
    assert defaults.mca_iterations == 100
    enabled = benchmark.parse_arguments(["--pipeline-timing", "--mca-iterations", "64"])
    assert enabled.pipeline_timing
    assert enabled.mca_iterations == 64


def test_first_diagnostic_prefers_fatal_lowering_failure() -> None:
    """An optional LLVM probe should report its cause, not an earlier warning."""

    result = benchmark.CommandResult(
        ("llc",),
        1,
        "",
        "warning: generic CPU\nLLVM ERROR: Cannot select: store (s64)\n",
    )
    assert benchmark.first_diagnostic(result) == (
        "LLVM ERROR: Cannot select: store (s64)"
    )

    addressed = benchmark.CommandResult(
        ("llc",), 1, "", "LLVM ERROR: Cannot select: 0x123abc: truncate\n"
    )
    assert benchmark.first_diagnostic(addressed) == (
        "LLVM ERROR: Cannot select: <node>: truncate"
    )


def test_runner_fragments_cover_every_kernel() -> None:
    """The bare-metal runner must declare, wrap, and invoke the full corpus."""

    declarations, wrappers, runs = benchmark.runner_fragments("bench_")
    for value in benchmark.KERNELS:
        assert f"bench_{value.name}" in declarations
        assert f"invoke_{value.name}" in wrappers
        assert f'"{value.name}"' in runs


def test_parse_runtime_output_reduces_odd_samples() -> None:
    """UART hexadecimal samples should reduce to typed medians."""

    lines = ["firmware noise", "kernel,sample,ticks,checksum"]
    for value in benchmark.KERNELS:
        for sample, ticks in enumerate((9, 5, 7)):
            lines.append(f"{value.name},{sample:08x},{ticks:08x},0123456789abcdef")
    records = benchmark.parse_runtime_output("\n".join(lines), "cross", "O3", 3)
    assert len(records) == len(benchmark.KERNELS)
    assert all(record.median_ticks == 7 for record in records)
    assert all(record.checksum == "0123456789abcdef" for record in records)


def test_verify_checksums_rejects_pipeline_disagreement() -> None:
    """A speed result is invalid unless every pipeline computes the same bits."""

    records = [
        benchmark.RuntimeRecord("cross", "O3", "integer", "mix", 1, 1, 1.0, "a"),
        benchmark.RuntimeRecord("gcc", "O3", "integer", "mix", 1, 1, 1.0, "b"),
    ]
    with pytest.raises(benchmark.BenchmarkError, match="disagree"):
        benchmark.verify_checksums(records)


def test_geometric_mean_requires_positive_values() -> None:
    """Ratio aggregation must not silently accept missing or invalid scores."""

    assert benchmark.geometric_mean((1.0, 4.0)) == pytest.approx(2.0)
    with pytest.raises(benchmark.BenchmarkError, match="positive"):
        benchmark.geometric_mean((1.0, 0.0))
