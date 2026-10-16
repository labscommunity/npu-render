# Long desktop shader: exact NPU execution and complete draw comparisons

The 181-instruction, sixteen-sample shader that stopped live v43 now runs in a
bounded NPU component. The previously failed complete request matches both the
original NPU renderer and an independent CPU shader interpreter for the captured
image and three generated images. All six earlier captured uses also pass both
references: 48 complete comparisons, 2,359,296 compared byte components, and all
six original output hashes preserved. No tolerance or precision exception is used.
The CPU interpreter is confined to the offline test executable.

The original failed packet is identified by SHA-256
`46caf6546bcde449d3829a05aaedfcefc719686f25264cec6786065df7a628c3`.
Its existing 17,057-node graph eventually compiles in 94.210 seconds during the
separate offline comparison; total first draw takes 95.712 seconds. The new path
sets up in 136.106 ms and renders the first draw in 64.812 ms. Three subsequent
complete draws take 30.067–32.405 ms, versus 107.724–148.828 ms on the old path.
The identical original image hash is
`61da2570eecf77357d3ed52b454df91c3b46b734613a192f777407a2bdb54b43`.
The offline deadline is 300 seconds; the desktop deadline is unchanged.

Across the six earlier captures, the eighteen warm original-NPU comparisons sum
to 2,273.212 ms on the old path and 529.274 ms on the candidate. All eighteen are
faster in this component experiment. The independent CPU reference timings are
not NPU rendering measurements. These small component results are not a matched
desktop benchmark, a latency distribution, or evidence of Tahoma improvement.

The kernel executes address generation, sixteen bilinear samples, and the
captured shader's color arithmetic on six compiler-owned NPU spans. The host
copies NPU-addressed texture bytes between the two phases. Existing qualified
NPU stages perform vertex shading, projection, coverage, interpolation and
output merging. The adapter pins the exact shader bytecode and model hash;
unsupported programs and bindings are rejected rather than silently executed
on another device. Runtime compilation of arbitrary shaders is not implemented.

The first generated kernel occupies 19,168 bytes and exceeds the 18,256-byte code
capacity. Making the repeated floor helper out-of-line reduces the clamp version
to 15,328 bytes. Full-request inspection then reveals mirror/mirror sampling,
which the initial clamp-only adapter correctly rejects. The final mirror-capable
revision occupies 16,160 bytes and passes all 120 independent full-span NPU
fixtures, including 24 invalid-input cases, with the original carrier checked
before and after. A separate local C execution passes the same fixtures; it is
not the evidence of NPU execution.

Final kernel source: `ptl-six-span-long-shader-v3.c`, SHA-256
`f62f3bf4c524510d1256f0ffc36a1f36d6c6ece866c2e1e50d39394972be0029`.
Final ELF SHA-256:
`9369f46b566a5f33560754af0837b46e2c87d65f30831a7618dd69d6471cc47c`.
Final model SHA-256:
`895831be2695bc151454833249996608f38d35880c635900bb731515bc2bcda1`.
Complete comparison executable v4 SHA-256:
`e586063fbb737ff49c452755fec20d205382b68f82c54693328eea47f08f9709`.

The archive preserves early capacity/admission failures and a capture-harness
failure caused by a missing metadata path, followed by the corrected successful
qualification. Its 141 source/evidence artifacts are byte-counted and hashed in
`artifact-sha256.json`. Compiled models, binaries and captured desktop pixels
remain outside Git. Full worker integration, live desktop stability, responsive
interaction, application coverage, physical GPU relief and Tahoma A/B remain
separate gates; this archive does not qualify them.
