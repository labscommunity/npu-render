# Panther Lake custom-kernel qualification

`identity.c` is our first hardware-qualified SHAVE kernel for Panther Lake. It
copies eight FP16 values per invocation on the narrowly validated add-one carrier.
It is a component for developing direct graphics kernels, not a desktop renderer.
The [hardware record](../../../docs/validation/2026-10-15/native-delivery-custom-kernel-v1/README.md)
retains the compiler/assembler/linker harnesses, source provenance and exact hashes.
Proprietary tools and compiled graph/kernel binaries are not redistributed.

`tools/validate_ptl_kernel.py` turns the experiment into a reusable validator. It
requires explicit SHA-256 pins for both caller-supplied files, checks the Panther
Lake invocation/tensor layout, rejects unresolved kernel relocations or mutable
data, and changes only the validated code extent. Without `--execute`, it performs
file checks only and reports hardware execution as untested.

```powershell
python tools/validate_ptl_kernel.py `
  --carrier PATH_TO_VERIFIED_ADD_ONE_BLOB `
  --carrier-sha256 4a6c494f103b730bea94a719a6001bb5344136dc14ec073f451e020558583446 `
  --kernel-elf PATH_TO_IDENTITY_ELF `
  --kernel-sha256 5d5217d9fffd0562becfb61604037ee2cba932efd07212c8d9063ecfbad3ba66 `
  --output results/ptl-identity-qualification --execute
```

Execution requires Windows and the exact qualified OpenVINO runtime
`2026.4.1-22982-e213a147257-releases/2026/4`; use that installation's Python with this
repository's `src` directory on `PYTHONPATH`. The ordinary package dependency is
not changed by this experiment. The validator imports only onto `NPU`, uses an
owned child with a 60-second deadline, and compares 64 changing inputs against
independent original/custom/original oracles. It changes no display or driver
settings. Timings include host request and output copying and are diagnostic.

`--operation indexed` selects the separate local-permutation/arithmetic oracle
for its corresponding pinned kernel. It does not make the identity binary an
indexed kernel. File checks cannot establish arbitrary instruction safety: use
only the intended locally built source and matching pins. This first contract
supports one specific invocation layout, not arbitrary compiled NPU graphs or
texture-wide addressing. Synthetic tests in `tests/test_ptl_carrier.py` contain
metadata and dummy bytes only and establish no hardware result.

To rebuild the included source, point `tools/build_ptl_kernel.py` at the extracted
`MVC_DEPEND` directory from the qualified toolchain acquisition. It checks the
three DLL hashes before loading them and uses an isolated 30-second compiler
child. It does not download or install the vendor package.

```powershell
python tools/build_ptl_kernel.py --toolchain PATH_TO_MVC_DEPEND `
  --source native/windows/ptl_kernels/identity.c --output results/ptl-identity-build
```

The result is `kernel.elf` plus source/toolchain hashes and diagnostics. Pass its
reported hash to the validator. A successful build proves code generation only;
`--execute` with exact independent output checks is still required for NPU proof.
