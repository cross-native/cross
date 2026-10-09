# Building Cross

Building produces two executables: `cc`, the compiler, and `cpp`, the
standalone preprocessor. Both are self-contained; the shipped compiler models
are built into them.

## Requirements

- CMake 3.20 or later and a build tool it supports, such as Ninja or Make.
- A C++20 compiler: GCC or Clang.
- `llvm-mc` on `PATH` to produce object files with `cc -c`. Assembly output
  (`cc -S`) needs no other tool.

The most tested host is x86-64 Windows with the MSYS2 MINGW64 environment:

```sh
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-gcc \
          mingw-w64-x86_64-llvm-tools mingw-w64-x86_64-lld
```

Add `mingw-w64-x86_64-clang` to build with Clang, and `mingw-w64-x86_64-qemu`
and `mingw-w64-x86_64-python-pytest` for the complete test suite. Use the
MINGW64 tools: MSYS2's own `/usr/bin/g++` produces programs that depend on
the MSYS runtime. On Linux, install the distribution's CMake, Ninja, GCC or
Clang, LLVM, and LLD packages.

## Configure and build

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The executables are written to `build/bin/` (`cc.exe` and `cpp.exe` on
Windows). Add `-G Ninja` to choose a generator, or
`-DCMAKE_CXX_COMPILER=clang++` to choose a compiler. GCC needs several
gigabytes of memory per job on the largest sources, so on a machine with many
cores limit the parallelism, for example `cmake --build build -j 6`.

| Option | Default | Effect |
| --- | --- | --- |
| `CROSS_DEFAULT_TARGET` | host | Target triple used when neither `-target` nor `-mprofile` is given. |
| `CROSS_STATIC_TOOLS` | `ON` | Link `cc` and `cpp` without shared compiler runtime libraries. |
| `CROSS_ENABLE_LLVM_TEXT` | `ON` | Include the `-emit-llvm` output mode. |
| `CROSS_ENABLE_GCC_GIMPLE_TEXT` | `ON` | Include the `-emit-gimple` output modes. |
| `CROSS_PPSSPP_HEADLESS` | unset | PPSSPPHeadless executable for Allegrex runtime tests. |
| `CMAKE_RUNTIME_OUTPUT_DIRECTORY` | `build/bin` | Directory for the executables. |
| `BUILD_TESTING` | `ON` | Build the test programs. |

The host default target is `x86_64-w64-windows-gnu` on Windows,
`ARCH-unknown-linux-gnu` on Linux, and `ARCH-apple-darwin` on macOS. When the
host architecture has no Cross backend, compiling without `-target` or
`-mprofile` fails with "default target ... is not implemented"; set
`CROSS_DEFAULT_TARGET` or pass a target. `cc --version` prints the configured
default and `cc --print-targets` lists the compiled-in architectures.

There is no install step. Copy `cc` and `cpp` to any directory. They do not
depend on their file names, so they can be renamed to avoid clashing with the
host's C compiler and preprocessor.

## Testing

```sh
ctest --test-dir build --output-on-failure -j 8
```

The tests compile Cross programs, check diagnostics and generated code, and
run the output natively or under emulation. They use these external tools:

| Tool | Used for |
| --- | --- |
| `llvm-mc`, `llvm-as`, `llvm-readobj`, `llvm-readelf`, `llvm-dwarfdump`, `llvm-objcopy`, `llc`, `ld.lld` | Assembling, linking, and inspecting objects. |
| The host C++ compiler and `gcc` | Linking generated x86-64 code into host test programs. |
| `qemu-system-mips64`, `qemu-system-mips64el` | MIPS runtime tests; skipped when missing. |
| Python 3.11 or later with pytest | Benchmark-driver tests; omitted when Python is missing. |
| PPSSPPHeadless | Allegrex runtime tests, with `CROSS_PPSSPP_HEADLESS`. |

Native runtime tests execute generated x86-64 code on the build host. Each QEMU
run reserves 1 GiB for its translation cache; if MIPS runtime tests fail to
allocate memory, lower `-j`.

Many tests carry labels for selection; `ctest --print-labels` lists them, for
example `ctest -L arch.mips` or `ctest -L feature.syntax`. `ctest -R REGEX`
selects tests by name. [test/README.md](../test/README.md) describes the test
layout and labels.
