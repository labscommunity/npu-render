# Five-sample desktop shader on the NPU

The `c71638aa…` shader is one of the two largest contributors to the measured
mouse-response delay. A generated kernel implements its 111 instructions and
five texture samples in two phases across six compiler-owned NPU spans. The host
transports the addressed packed texels; the NPU computes sample coordinates,
filtering and shader arithmetic. Existing NPU geometry and blending remain in use.
The compiled kernel occupies 12,784 of the qualified 18,256 code bytes.

All 164 independent CPU and NPU fixtures pass: 128 valid and 36 invalid payloads.
The 28 six-span invocations include four padding repeats. Eight original-carrier
checks pass before and after the custom kernel. Model metadata, bounds and the
original carrier tail remain unchanged.

The first complete-image comparison fails against the old NPU renderer: ten
bytes differ by one in a 3,145,728-byte image. The first independent scalar shader
reference also fails, with two bytes differing by one. Those failed reports and
references are retained unchanged. Inspection finds that this reference performs
texture coordinates and interpolation in double precision before the FP32 shader.
The candidate explicitly rounds those operations to FP32.

A diagnostic captures the actual NPU phase inputs and outputs. A separate NumPy
interpreter of the shader IR exactly matches all 180 captured spans, including
34,816 coordinate records and 34,816 color records. This checks actual device
results, rather than comparing two generated implementations. A second independent
full-image reference retains its own scalar interpreter and integer tap addressing,
while explicitly rounding coordinate and interpolation operations to FP32 under
`/fp:strict`. All four variants of all ten recorded draws match that reference
exactly: 40 images, with no byte tolerance.

Five original images retain the old renderer's hash. Five change: tickets 58,
196, 302, 363 and 820. They are a distinct numerical profile, not an unchanged
renderer or an established correction to the old renderer. The Direct3D
specification separates arithmetic precision and texture-filter requirements;
these checks alone do not establish complete Direct3D conformance or a blanket
permission for one-byte differences. See the [Microsoft functional specification,
sections 3.1 and 7.18.16](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).

The retained full-image comparison uses a CPU shader reference with NPU geometry
and blending. Its timing must not be reported as an NPU-versus-NPU speedup.
A matched comparison against the old NPU renderer and explicit replay references
for the five changed images are separate gates before worker integration.
No desktop or Tahoma performance claim follows from this component result.

Model SHA-256:
`47f69b5861732a03014b58160116ebbb928fd6e818a11b7f553e423a4cf3c43a`.
Independent-reference executable SHA-256:
`44add80f829c52577bb113cbc03d03facedc3223196566d3efbbb20e35607d38`.
The archive pins 117 source and evidence artifacts. Private pixel packets, captured
span payloads, compiled binaries and device models remain excluded; their hashes
are retained. `summary.json` derives counts from the original reports.
