# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Executable ABI checks and assembly checks for hosts without AVX-512."""
import argparse
from pathlib import Path
import re
import subprocess
import sys

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--host", required=True)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--isa", choices=("avx2", "avx512", "selection"), required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
args.output.mkdir(parents=True, exist_ok=True)
base = [args.cc, "-fno-eval-calls", "-fno-inline", "-mabi=ms_abi",
        f"--model={source / 'models.xm'}"]


def run(command, expected=0):
    result = subprocess.run([str(x) for x in command], capture_output=True, text=True)
    if result.returncode != expected:
        raise AssertionError(f"{' '.join(map(str, command))}\n"
                             f"exit {result.returncode}, expected {expected}\n"
                             f"{result.stdout}{result.stderr}")
    return result


def compile_source(name, path, flags, target="x86_64-unknown-linux-gnu"):
    output = args.output / (name + ".s")
    run(base + ["-S", "-target", target] + flags + [path, "-o", output])
    return output, output.read_text()


def body(assembly, name):
    match = re.search(rf"^{re.escape(name)}:\n(.*?)(?:^\.size |^\.seh_endproc)",
                      assembly, re.M | re.S)
    assert match, f"missing {name}"
    return match[1]


def split_vector(assembly, name, register):
    text = body(assembly, name)
    before, after = re.split(r"\bcall\s+[^\n]+\n", text, maxsplit=1)
    saved = re.findall(rf"v?movdqu(?:64)?\s+%{register}\d+,\s*([^\n]+)", before)
    assert any(re.search(rf"v?movdqu(?:64)?\s+{re.escape(slot)},\s*%{register}\d+", after)
               for slot in saved), f"missing full-width call split in {name}\n{text}"


if args.isa == "selection":
    for name, flags in (("sse", ["-mno-avx"]),
                        ("avx", ["-mavx"]),
                        ("avx2", ["-mavx2"]),
                        ("avx512", ["-mavx512f"]),
                        ("disabled", ["-march=x86-64-v4", "-mno-avx"]),
                        ("no512", ["-march=x86-64-v4", "-mno-avx512f"])):
        _, assembly = compile_source(name, source / "selection.x", ["-O2"] + flags)
        text = body(assembly, "narrow_selection")
        assert "%xmm6" in text, text
        assert not re.search(r"%[yz]mm", text), text
        if name in ("sse", "disabled"):
            assert not re.search(r"^\s+v\w+\s", text, re.M), text
        if name == "avx512":
            compile_source("high", source / "selection.x",
                           ["-O2", "-mavx512f", "-DHIGH_REGISTER"])
    path, assembly = compile_source("high-saves", source / "selection.x",
                                    ["-O2", "-mavx512f", "-funwind-tables", "-DHIGH_VIEW_SAVE"])
    for register in ("zmm16", "zmm17", "zmm18"):
        assert re.search(rf"movdqu(?:64)?\s+%{register},", assembly), assembly
    for register in ("xmm16", "xmm17", "xmm18"):
        assert f".cfi_offset %{register}," in assembly, assembly
    run(base + ["-c", "-O2", "-mavx512f", "-funwind-tables", "-DHIGH_VIEW_SAVE",
                source / "selection.x", "-o", path.with_suffix(".o")])
    path, assembly = compile_source("high-saves-vl", source / "selection.x",
                                    ["-O2", "-mavx512f", "-mavx512vl", "-funwind-tables", "-DHIGH_VIEW_SAVE"])
    for register in ("xmm16", "ymm17", "zmm18"):
        assert re.search(rf"vmovdqu64\s+%{register},", assembly), assembly
    run(base + ["-c", "-O2", "-mavx512f", "-mavx512vl", "-funwind-tables", "-DHIGH_VIEW_SAVE",
                source / "selection.x", "-o", path.with_suffix(".o")])
    rejected = subprocess.run(base + ["-S", "-O2", "-march=x86-64-v4", "-mno-avx512f",
                              "-DHIGH_REGISTER", str(source / "selection.x"),
                              "-o", str(args.output / "rejected.s")],
                              capture_output=True, text=True)
    assert rejected.returncode and "requires target feature 'avx512f'" in rejected.stderr
    for name, abi, type_, reg in (("gpr", "views", "u32", "eax"),
                                  ("simd", "ms_abi", "f64", "xmm6")):
        path = args.output / (name + ".x")
        for explicit in (False, True):
            clobber = f', clobber("{reg}")' if explicit else ""
            path.write_text(f'[[abi("{abi}"){clobber}]]\n'
                            f'global {type_} result() -> "{reg}" {{ return 1; }}\n')
            result = subprocess.run(base + ["-S", str(path), "-o", str(path.with_suffix(".s"))],
                                    capture_output=True, text=True)
            if explicit:
                assert result.returncode == 0, result.stderr
            else:
                assert result.returncode and "preserved by the selected ABI" in result.stderr
    rejected = subprocess.run(base + ["-S", "-O2", "-mavx512f", "-funwind-tables",
                              "-DHIGH_VIEW_SAVE", "-target", "x86_64-w64-windows-gnu",
                              str(source / "selection.x"), "-o", str(args.output / "high-seh.s")],
                              capture_output=True, text=True)
    assert rejected.returncode and "Win64 unwind cannot describe" in rejected.stderr, rejected.stderr
    unavailable = args.output / "unavailable.x"
    unavailable.write_text('[[abi("ms_abi")]] void callee();\n'
                           '[[abi("strong_views")]] global void caller() { callee(); }\n')
    rejected = subprocess.run(base + ["-S", "-O2", "-mno-avx", str(unavailable),
                                      "-o", str(unavailable.with_suffix(".s"))],
                              capture_output=True, text=True)
    assert rejected.returncode and "requires target feature 'avx'" in rejected.stderr, rejected.stderr
    unavailable.write_text('[[abi("ms_abi")]] void callee();\n'
                           '[[abi("high_views")]] global void caller() { callee(); }\n')
    rejected = subprocess.run(base + ["-S", "-O2", "-mno-avx512f", str(unavailable),
                                      "-o", str(unavailable.with_suffix(".s"))],
                              capture_output=True, text=True)
    assert rejected.returncode and "requires target feature 'avx512f'" in rejected.stderr, rejected.stderr
    tail = args.output / "tail.x"
    tail.write_text('[[abi("views")]] u64 callee();\n'
                    '[[abi("strong_views")]] global u64 caller() {\n'
                    '    [[musttail]] return callee();\n}\n')
    rejected = subprocess.run(base + ["-S", "-O2", "-mavx2", str(tail),
                                      "-o", str(tail.with_suffix(".s"))],
                              capture_output=True, text=True)
    assert rejected.returncode and "preserves fewer register bits" in rejected.stderr, rejected.stderr
    # Explicit and implicit promises follow the same encoding-based rule.
    encoding_model = args.output / "encoding.xm"
    model_text = (source / "models.xm").read_text().split('abi "views" {', 1)[1].split('abi "strong_views"', 1)[0]
    model_text = 'abi "encoding" {' + model_text
    model_text = model_text.replace('"zmm4", ', '')
    leaf = args.output / "encoding.x"
    leaf_text = ('global f64 input; global f64 output;\n'
                 '[[abi("encoding")]] global void leaf() {\n'
                 '    register f64 value "xmm4" = input * 1.5;\n'
                 '    output = value + 1.0;\n}\n')
    for explicit in (False, True):
        text = re.sub(r'call_preserved_views = \[.*?\];',
                      'call_preserved_views = ' + ('["ymm4"];' if explicit else '[];'), model_text)
        encoding_model.write_text(text)
        leaf.write_text(leaf_text)
        for flags in (["-mno-avx"], ["-mavx2"]):
            run(base + ["-S", "-O2", f"--model={encoding_model}", *flags,
                        leaf, "-o", leaf.with_suffix(".s")])
        if not explicit:
            # A private SSE body exposes low-lane writes while retaining
            # upper lanes; its stronger caller needs only an XMM save.
            leaf.write_text('global volatile u64 seed;\n'
                            '[[noinline]] u64 helper(in u64 x) {\n'
                            '    if (seed != 0) { seed = x; return x; }\n'
                            '    seed = x + 1u64; return x + 1u64;\n}\n' +
                            leaf_text.replace('output = value + 1.0;', 'output = value + (f64)helper(7u64);'))
            run(base + ["-S", "-O2", "-mno-avx", f"--model={encoding_model}", leaf,
                        "-o", leaf.with_suffix(".s")])
            assert re.search(r"movdqu\s+%xmm4,", leaf.with_suffix(".s").read_text())
        # A System V call may destroy every promised bit.
        leaf.write_text('[[abi("sysv_abi")]] void callee();\n'
                        '[[abi("encoding")]] global void bridge() { callee(); }\n')
        rejected = subprocess.run(base + ["-S", "-O2", "-mno-avx", f"--model={encoding_model}",
                                          str(leaf), "-o", str(leaf.with_suffix(".s"))],
                                  capture_output=True, text=True)
        assert rejected.returncode and "requires target feature" in rejected.stderr, rejected.stderr
        if not explicit:
            # VEX writes also clear bits 256-511, even without AVX-512 enabled.
            leaf.write_text('typedef u64 V [[vector_size(32)]]; global V a; global V b;\n' +
                            leaf_text.replace('output = value + 1.0;', 'output = value + 1.0; b = a + 1u64;'))
            rejected = subprocess.run(base + ["-S", "-O2", "-mavx2", f"--model={encoding_model}",
                                              str(leaf), "-o", str(leaf.with_suffix(".s"))],
                                      capture_output=True, text=True)
            assert rejected.returncode and "requires target feature 'avx512f'" in rejected.stderr, rejected.stderr
    # Wide save slots may exceed the SEH frame-register offset field.
    text = model_text.replace('abi "encoding"', 'abi "wide_frame"').replace('call_clobbers = [', 'call_clobbers = ["zmm4",')
    text = re.sub(r'call_preserved_views = \[.*?\];',
                  'call_preserved_views = [' + ', '.join(f'"ymm{i}"' for i in range(6, 16)) + '];', text)
    encoding_model.write_text(text)
    leaf.write_text('[[abi("sysv_abi")]] void callee();\n'
                    '[[abi("wide_frame")]] global void bridge() { callee(); }\n')
    rejected = subprocess.run(base + ["-S", "-O2", "-mavx2", "-funwind-tables", "-fno-omit-frame-pointer",
                              "-target", "x86_64-w64-windows-gnu", f"--model={encoding_model}", str(leaf),
                              "-o", str(leaf.with_suffix(".s"))], capture_output=True, text=True)
    assert rejected.returncode and "frame-register offset greater than 240" in rejected.stderr, rejected.stderr
    sys.exit(0)

wide512 = args.isa == "avx512"
isa = ["-mavx512f", "-DWIDE512"] if wide512 else ["-mavx2"]
register = "zmm" if wide512 else "ymm"
variants = (("O0", ["-O0"]), ("O2", ["-O2"]), ("O3", ["-O3"]),
            ("noipa", ["-O2", "-fno-ipa-ra"]),
            ("nora", ["-O2", "-fno-register-allocation"]),
            ("cfi", ["-O2", "-funwind-tables", "-fno-omit-frame-pointer"]))
skipped = False
for name, flags in variants:
    path, assembly = compile_source(name, source / "runtime.x",
                                    isa + flags)
    if name in ("O2", "O3", "noipa", "cfi"):
        split_vector(assembly, "keep_wide", register)
        split_vector(assembly, "__cross_group_private_keep", register)
        if not wide512:
            custom = body(assembly, "keep_custom_wide")
            before, after = re.split(r"\bcall\s+[^\n]+\n", custom, maxsplit=1)
            assert "%ymm11" in before and "%ymm11" in after, custom
            assert "vzeroupper" not in before, custom
        narrow = body(assembly, "keep_narrow")
        assert re.search(r"paddq\s+[^\n]*%xmm6", narrow), narrow
        # The fitting value is read in place before the epilogue restores XMM6.
        after = re.split(r"\bcall\s+[^\n]+\n", narrow, maxsplit=1)[1]
        first_use = re.search(r"paddq\s+[^\n]*%xmm6", after)
        assert first_use, narrow
        assert not re.search(r"movdqu\s+[^\n]+,\s*%xmm6", after[:first_use.start()]), narrow
    saved = body(assembly, "use_preserved")
    for reg in range(6, 16):
        assert re.search(rf"movdqu\s+%xmm{reg},", saved), saved
        assert re.search(rf"movdqu\s+[^\n]+,\s*%xmm{reg}\b", saved), saved
        if name == "cfi":
            assert f".cfi_offset %xmm{reg}," in saved, saved
    bridge = body(assembly, "bridge_views")
    assert re.search(r"vmovdqu\s+%ymm3,", bridge), bridge
    assert re.search(r"vmovdqu\s+[^\n]+,\s*%ymm3\b", bridge), bridge
    dynamic_flags = [f for f in flags if f != "-fno-register-allocation"]
    dynamic_path = args.output / (name + "-dynamic.s")
    run(base + ["-S", "-target", "x86_64-unknown-linux-gnu", "-mabi=views", "-mavx2"] +
        dynamic_flags + [source / "dynamic.x", "-o", dynamic_path])
    if name in ("O2", "O3", "noipa", "cfi"):
        private = body(dynamic_path.read_text(), "__cross_group_private_custom")
        assert "%rax" not in private, private
    exe = args.output / name
    run([args.host, "-O2"] + (["-DWIDE512"] if wide512 else []) +
        [path, dynamic_path, source / "callees.S", source / "canary.S", source / "driver.cpp", "-o", exe])
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    assert result.returncode in (0, 77), f"{name}: {result.stdout}{result.stderr}"
    skipped |= result.returncode == 77

# Check both fixed and RBP Win64 unwind paths and assemble the COFF objects.
for frame in ("omit", "keep"):
    flags = isa + ["-O2", "-funwind-tables", "-fno-optimize-sibling-calls",
                   "-fomit-frame-pointer" if frame == "omit" else "-fno-omit-frame-pointer"]
    path, assembly = compile_source("seh-" + frame, source / "runtime.x", flags,
                                    "x86_64-w64-windows-gnu")
    for reg in range(6, 16):
        assert f".seh_savexmm %xmm{reg}," in assembly, assembly
    run(base + ["-c", "-target", "x86_64-w64-windows-gnu"] + flags +
        [source / "runtime.x", "-o", path.with_suffix(".obj")])
print("Assembly and object checks passed; runtime " +
      ("skipped (CPU feature unavailable)" if skipped else "passed"))
sys.exit(77 if skipped else 0)
