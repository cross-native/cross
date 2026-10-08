# Tests

The tests are registered with CTest by the top-level `CMakeLists.txt`. Build
Cross as described in [doc/install.md](../doc/install.md), then run them from
the repository root:

```sh
ctest --test-dir build --output-on-failure -j 8
```

## Layout

| Directory | Covers |
| --- | --- |
| `common/` | Shared compiler utilities, such as option parsing and integer and floating semantics. |
| `model/` | Compiler-model parsing, ABI rules, mangling, optimization presets, and profiles. |
| `language/` | Source-language behavior independent of the target; `doc_examples.cmake` compiles every example in `README.md` and `doc/`. |
| `frontend/` | Preprocessing, diagnostics, compile-time evaluation, procedural macros, and syntax extensions. |
| `middle/` | Semantic expansion, IR, and target-independent optimizations. |
| `backend/` | Machine IR, register allocation, ABI boundaries, naked and raw code, and object emission. |
| `target/<architecture>/` | Architecture-specific profiles, assembly, object metadata, and emulator execution. |
| `support/` | Runners shared by several test families. |

A test's fixtures (`.x` sources, assembly startup files, linker scripts, and
models) live beside the CMake script that drives it. Most tests compile Cross
sources and check the output or the diagnostics. Runtime tests link generated
code with a small host runner or run it under QEMU and compare the result with
an expected value.

## Selecting tests

`ctest -R REGEX` selects tests by name. Tests also carry labels, listed by
`ctest --test-dir build --print-labels`:

| Label | Meaning |
| --- | --- |
| `arch.x86_64`, `arch.mips`, `arch.independent` | Target architecture, or none. |
| `feature.NAME` | Language or compiler feature, such as `feature.syntax` or `feature.abi`. |
| `check.unit`, `check.compile`, `check.runtime`, `diagnostics` | Kind of check. |
| `frontend`, `middle`, `backend.native`, `backend.llvm`, `backend.gcc` | Compiler layer or output path exercised. |
| `endian.big`, `endian.little`, `isa.NAME`, `profile.NAME`, `opt.LEVEL`, `opt.noalloc` | Target mode or optimization setting. |

Several `-L` options select the intersection, for example
`ctest --test-dir build -L arch.mips -L check.runtime`.

The external tools the tests use are listed in
[doc/install.md](../doc/install.md#testing).

## Adding tests

Put a new test in the directory of the feature it covers, with its fixtures
beside its script, and register it in `CMakeLists.txt` with `feature.*` and
`arch.*` labels and an output path of its own under the build tree. Prefer a
test that runs generated code over one that only inspects assembly, and cover
each affected target, byte order, and ABI.
