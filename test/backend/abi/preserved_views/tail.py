# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
"""Sibling-call contracts, including adapters and manual endpoints."""
import argparse
from pathlib import Path
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument("--cc", required=True)
parser.add_argument("--host", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
source = Path(__file__).resolve().parent
args.output.mkdir(parents=True, exist_ok=True)
base = [args.cc, "-target", "x86_64-unknown-linux-gnu", "-fno-eval-calls"]

def run(command, expected=0):
    result = subprocess.run(list(map(str, command)), capture_output=True, text=True)
    assert result.returncode == expected, f"{command}\n{result.stdout}{result.stderr}"
    return result

variants = {"O2": ["-O2"], "O3": ["-O3"], "Os": ["-Os"], "Oz": ["-Oz"],
            "noipa": ["-O2", "-fno-ipa-ra"], "nopriv": ["-O2", "-fno-private-abi"],
            "cfi": ["-O2", "-funwind-tables", "-fno-omit-frame-pointer"],
            "sse": ["-O2", "-mno-avx"], "disabled": ["-O2", "-fno-optimize-sibling-calls"]}
skipped = False
for abi in ("x", "ms_abi"):
    for name, flags in variants.items():
        tag = f"{abi}-{name}"
        assembly = args.output / (tag + ".s")
        run(base + ["-S", *([] if name == "sse" else ["-mavx2"]), f"-mabi={abi}", *flags,
                    source / "tail.x", "-o", assembly])
        text = assembly.read_text()
        if name not in ("disabled", "cfi"):
            allowed = re.search(r"^allowed_tail:\n(.*?)^\.size", text, re.M | re.S).group(1)
            assert re.search(r"jmp\s+hostile", allowed), allowed
        if abi == "ms_abi" and name not in ("nopriv", "sse"):
            helper = re.search(r"^__cross_group_helper:\n(.*?)^\.size", text, re.M | re.S).group(1)
            assert not re.search(r"movdqu\s+%xmm(?:[6-9]|1[0-5]),", helper), helper
        exe = args.output / tag
        run([args.host, "-O2", assembly, source / "tail_driver.cpp", source / "tail_canary.S",
             source / "tail_callees.S", "-o", exe])
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        assert result.returncode in (0, 77), f"{tag}: {result.stdout}{result.stderr}"
        skipped |= result.returncode == 77

for manual in (False, True):
    for explicit in (False, True):
        path = args.output / f"musttail-{explicit}-{manual}.x"
        endpoint = ' "r10"' if manual else ''
        output = ' -> "rax"' if manual else ''
        clobber = ', clobber("xmm6")' if explicit else ''
        path.write_text(f'''[[abi("ms_abi"), clobber("xmm6")]] u64 callee(in u64 x{endpoint}){output};
[[abi("ms_abi"){clobber}]] global u64 caller(in u64 x{endpoint}){output} {{
    [[musttail]] return callee(x);
}}
''')
        result = subprocess.run(base + ["-S", "-O2", str(path), "-o", str(path.with_suffix(".s"))],
                                capture_output=True, text=True)
        if explicit:
            assert result.returncode == 0, result.stderr
            assert re.search(r"jmp\s+callee", path.with_suffix(".s").read_text())
        else:
            assert result.returncode and "callee clobber contract exceeds" in result.stderr, result.stderr

for flags in (["-O2"], ["-O3"], ["-O2", "-fno-ipa-ra"], ["-O2", "-fno-register-allocation"],
              ["-O2", "-DNO_CALL"], ["-O3", "-DNO_CALL"]):
    assembly = args.output / "phi.s"
    run(base + ["-S", "-mavx2", *flags, source / "loop_phi.x", "-o", assembly])
    exe = args.output / "phi"
    run([args.host, "-O2", assembly, source / "loop_phi_driver.cpp", "-o", exe])
    result = subprocess.run([str(exe)])
    assert result.returncode in (0, 77)
    skipped |= result.returncode == 77
print("Sibling-call canaries, musttail diagnostics, and vector PHI cycles passed")
raise SystemExit(77 if skipped else 0)
