# Cross

Cross is a low-level, ahead-of-time compiled language with C99-style syntax.
A Cross program gets no implicit library, runtime, startup code, or entry
point, and the compiler inserts no call the source does not write. Types have
explicit sizes, parameters declare their dataflow (`in`, `out`, `inout`), and
every compiler facility is spelled `$::name` or `[[attribute]]`. Ordinary
functions can run during compilation, and macros and syntax extensions are
written in Cross itself.

```x
static u64 square(u64 value) {
    return value * value;
}

global u64 answer = square(6) + 6;   // computed during compilation: 42

global void divide(u32 a, u32 b, out u32 quotient, out u32 remainder) {
    quotient = a / b;
    remainder = a % b;
}
```

This repository contains the compiler `cc`, the preprocessor `cpp`, the
compiler models, tests, and benchmarks. One `cc` build targets x86-64 (ELF,
COFF, and Mach-O) and MIPS (MIPS I to MIPS64, including the VR4300 and
Allegrex). Cross is a pre-1.0 draft: the compiler implements part of the
current draft, and drafts do not promise source or binary compatibility with
one another.

## Quick start

You need CMake 3.20 or later and a C++20 compiler. `cc -c` also needs
`llvm-mc` on `PATH`. With the example above saved as `example.x`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/cc -S -O2 example.x -o example.s
build/bin/cc -c -O2 -target x86_64-unknown-linux-gnu example.x -o example.o
```

`cc` produces assembly or object files and does not link. Without `-target`,
it compiles for the host it was built on.

## Documentation

- [doc/install.md](doc/install.md): building, configuration, and tests.
- [doc/invoke.md](doc/invoke.md): `cc` and `cpp` options.
- [doc/language.md](doc/language.md): the language, for C programmers.
- [doc/metaprogramming.md](doc/metaprogramming.md): macros, syntax
  extensions, and embedded files.
- [doc/targets.md](doc/targets.md): targets, profiles, and ABIs.
- [benchmark/x86_64](benchmark/x86_64/README.md) and
  [benchmark/mips32](benchmark/mips32/README.md): code-generation comparisons
  with GCC and Clang.

## Credits

Cross is created and maintained by xcmp0, and most of its development is done
by large language models. In no particular order, we credit the following for
their invaluable contributions:

- Astra 6
- Sol 5.6, 6, and 6.1
- GPT 5.5
- Fable 5.1
- Opus 5.5

## Support

Developing Cross with language models costs a lot of tokens. If Cross is
useful to you, consider supporting its development.

## License

Cross is free software: you can redistribute it and modify it under the terms
of the GNU General Public License, version 3 or later. See [LICENSE](LICENSE).

The copyright holders read the license as follows:

- Using Cross carries no obligation. Programs written in Cross, with or without
  AI assistance, are not works based on Cross, whether or not the manual, the
  examples, the models, or the test programs were used as reference, and the
  compiler's output carries no obligation either: `cc` adds no runtime code to
  your program.
- Reusing the compiler does. If you give the compiler sources (`src/`,
  `model/`, `benchmark/`, or `test/`) to an AI model as reference, in a prompt,
  as context, or through retrieval, and use what it writes as part of another
  compiler, tool, or library, that code is a work based on Cross. Convey it
  only under the GNU General Public License, version 3 or later, with its
  complete corresponding source.
