# x86-64 code-generation comparison

This corpus compares six paths over equivalent standalone kernels:

- `cross`: Cross HIR/MIR/Machine IR and the in-tree x86-64 backend;
- `llc`: Cross LLVM serialization, LLVM `opt`, then `llc`;
- `gcc-gimple`: optimized typed Cross MIR serialized as GCC `__GIMPLE (ssa)`,
  followed by GCC's remaining GIMPLE and RTL passes;
- `gcc-rtl`: the same serialization with `startwith("optimized")`, followed by
  GCC expansion and RTL passes only;
- `gcc`: equivalent freestanding C compiled by GCC; and
- `clang`: equivalent freestanding C compiled by Clang.

The runner measures generated-code time, executable text bytes, and complete
front-end-to-object compilation latency at `O0`, `Og`, `O1`, `O2`, `O3`, `Os`,
and `Oz`. Every kernel object must have no undefined symbols. GCC and Clang are
given `-ffreestanding -fno-builtin`; floating contraction is disabled for all
paths. Each variant is linked into the same process and checked against one
reference result before timing.

The runtime corpus separates integer, bitwise, call, reduction,
streaming-write, memory-dependency, indirect-access, pointer-chasing,
branching, selection, search, floating-throughput, and floating-dependency
use cases. Each category has multiple kernels. Scores first take a geometric
mean within each category and then weight categories equally; the robust speed
gate also has to survive removing any one category. Code size is built as six
separate scalar, call, linear-memory, indirect-memory, control, and floating
units and is checked again after removing any one unit.

Run from the repository root after building `build/cc`:

```text
python trunk/benchmark/x86_64/run.py
```

For a quick validation:

```text
python trunk/benchmark/x86_64/run.py --levels O2 --target-ms 1 --samples 1 --compile-runs 1
```

By default, results are grouped by source revision and run configuration:

```text
build/benchmark/x86_64/
  <short-commit>-<commit-subject>[-dirty]/
    <timestamp>-<levels>-<target-ms>-<samples>-<compile-runs>/
```

`--run-name NAME` replaces the timestamp/configuration leaf with a readable
name. `--output PATH` still selects an exact directory. Each run contains:

- `report.md`: Cross-relative aggregate summary;
- `measurements.csv`: per-kernel steady-state timings;
- `build.csv`: compile latency and text size;
- `metadata.json`: host, target, and toolchain identities;
- `*.gimple.c`: exact experimental GCC bridge inputs; and
- `*.disasm`: generated object disassembly for diagnosis.

The `llc` path measures the former LLVM-style production pipeline, including
Cross serialization and LLVM optimization. The two GCC bridge paths likewise
retain Cross's middle end and isolate progressively more of GCC's optimizer.
They use GCC's experimental, version-sensitive `-fgimple` interface; the
runner probes for it before building. GCC/Clang source equivalence should be
reviewed whenever a kernel changes. For publishable measurements, close
background programs, pin CPU frequency/affinity externally where the host
permits it, run several complete benchmark invocations, and report dispersion
as well as the median.

The bridge preserves model-mapped explicit C ABIs. A private dynamic Cross ABI
has no GCC spelling, so bridge-only local calls use GCC's translation-unit
convention; native Cross remains the only row that measures dynamic ABI
planning.

Metadata also records the full Git commit and dirty state, exact invocation,
Cross compiler SHA-256, and source-corpus SHA-256. Use these identities rather
than directory names alone when retaining or comparing benchmark evidence.
