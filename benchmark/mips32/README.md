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
An opt-in pipeline layer builds an isolated two-invocation runner for every
kernel, records QEMU's executed translation blocks, and weights LLVM-MCA's
generic MIPS3 dependency/resource costs by those dynamic counts. Candidate
objects and any linked `libgcc` helpers are included; runner, startup, and
wrapper code are excluded by link-map origin. This is a better operation-cost
estimate than raw instruction count, but LLVM's generic one-wide MIPS3 model is
not a calibrated VR4300: cross-block dependencies, branch prediction, caches,
and TLB behavior remain unmodeled. Repeating each block independently can also
overstate dependencies for calls and other blocks that are not actual
self-loops, so the layer is a comparative diagnostic rather than a cycle oracle.
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

Restrict runtime, pipeline, and per-function reporting to selected kernels.
Each source unit containing one of them is still compiled as a whole, so linked
image totals include companion functions from those units:

```text
python trunk/benchmark/mips32/run.py --kernels narrow,branch,pointer_chase,pointer_chase_pair \
  --levels O3 --samples 3 --pipeline-timing --run-name narrow-branch-pointer
```

Kernel names are comma-separated; unknown or empty names are rejected. The
default is all kernels.

Repeat `--cross-flag` to append independent Cross options to every Cross-based
pipeline. This is useful for controlled pass ablations without changing a
model, for example:

```text
python trunk/benchmark/mips32/run.py --levels O3 --samples 3 \
  --cross-flag=-fno-tree-loop-rotate --run-name no-rotation
```

To add the path-weighted LLVM-MCA estimate (the driver auto-discovers QEMU's
contributed hotblocks plugin beside the emulator when installed):

```text
python trunk/benchmark/mips32/run.py --levels O3 --samples 3 \
  --pipeline-timing --run-name pipeline
```

Use `--qemu-hotblocks-plugin <path>` when the plugin is installed elsewhere.
`--mca-iterations` controls the repeated-block simulation length and defaults
to 100. Pipeline timing is deliberately opt-in because it links and executes a
separate image for every kernel and compiler path.

Results are grouped by source revision under:

```text
build/benchmark/mips32/<commit>-<subject>[-dirty]/<run-name>/
```

The output includes `report.md`, `build.csv`, `code_size.csv`, `image.csv`,
`runtime.csv`, metadata, exact generated GIMPLE, GCC optimized-GIMPLE and
final-RTL dumps, link maps, objects, linked images, and disassembly. A modeled
run also emits `pipeline.csv` plus each hotblock trace, MCA input, and MCA JSON.
