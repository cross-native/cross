# Cross test layout

- `model/` covers compiler-definition parsing, ABI rules, mangling,
  optimization presets, typed profile options, and model diagnostics.
- `language/` covers target-independent source-language behavior.
- `frontend/` covers preprocessing, diagnostics, procedural expansion, and
  lexically activated raw-capture syntax, including opaque function bodies
  behind validated direct core headers, pattern combinators, and productive
  recursive rules.
- `middle/` covers semantic expansion, evaluation, patch values, MIR,
  optimization flags, inlining, local DSE, conservative IPA purity, and
  surviving-call specialization.
- `backend/machine/` covers instruction and register metadata.
- `backend/raw/` covers naked functions and raw control flow.
- `backend/abi/` covers automatic, manual, and optimized private call
  boundaries.
- `backend/native/` covers native emission and standalone auditing.
- `target/<architecture>/` covers architecture-specific profiles, assembly,
  object metadata, diagnostics, and optional emulator execution.
  `target/mips/differential/` runs the same Cross kernels through the
  native x86-64 backend and through QEMU on MIPS, comparing results to catch
  backend divergences across byte order, CPU profile, and ABI boundary.
- `support/` contains runners shared by multiple test families.

Fixtures live beside their CMake test driver whenever they are not shared.
