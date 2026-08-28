# MIPS III/o32 code-generation comparison

This corpus compares equivalent standalone kernels through:

- `cross`: Cross MIR, common Machine IR, and the in-tree native MIPS backend;
- `gcc-gimple`: Cross MIR serialized as GCC `__GIMPLE (ssa)`, followed by
  GCC's remaining GIMPLE and RTL passes;
- `gcc-rtl`: optimized Cross MIR serialized with `startwith("optimized")`,
  followed by GCC expansion and RTL passes;
- `gcc`: equivalent freestanding C through GCC's normal GIMPLE and RTL
  pipeline; and
- `clang`: equivalent freestanding C through Clang and LLVM.

The driver capability-probes both direct Clang lowering and the representative
Cross LLVM-IR-to-`llc` path. It adds their rows only when complete MIPS III/o32
`u64` probes succeed; target registration alone is not considered support.

The Cross and C sources reuse the paired scalar, call, memory, indirect,
control, and floating corpus in `benchmark/x86_64/kernels`. Every generated
native Cross object must have no undefined symbols. GCC and LLVM competitors
may use target compiler-runtime helpers from the selected o32 `libgcc`; the
driver validates and names every reference, links only the required archive
members, and charges their bytes to that pipeline. Each candidate is linked
with the same GCC-built freestanding runner and executed on QEMU's R4000 Malta
model. Checksums must agree bit-for-bit across every pipeline and preset.

Runtime results use the median CP0 Count delta under deterministic QEMU
`-icount`. This is a dynamic-execution proxy useful for compiler comparisons;
it is not a claim about VR4300 hardware cycles, caches, or pipeline stalls.
Text and load-image bytes are reported separately after subtracting the common
runner/startup footprint. Linker padding and extracted runtime support remain
charged to the candidate. The intended objectives are `O3` for speed and `Oz`
for minimum size.

After building `build/cc`, run from the repository root:

```text
python trunk/benchmark/mips32/run.py
```

For a shorter validation run:

```text
python trunk/benchmark/mips32/run.py --levels O3 --samples 3 --run-name quick
```

Results are grouped by source revision under:

```text
build/benchmark/mips32/<commit>-<subject>[-dirty]/<run-name>/
```

The output includes `report.md`, `build.csv`, `image.csv`, `runtime.csv`,
metadata, exact generated GIMPLE, GCC optimized-GIMPLE and final-RTL dumps,
link maps, objects, linked images, and disassembly.
