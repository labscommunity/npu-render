# Custom SHAVE kernels for graphics

The [sampling profile](../validation/2026-10-06/sampler-profile/README.md)
justifies investigating programmable NPU kernels: indexed fetch dominates the
tested graph, while reduction and source-over arithmetic are comparatively cheap.
This is a concrete software bottleneck, not evidence that graphics arithmetic
cannot execute on an NPU.

## What the source establishes

Intel's [software-layer guide](https://github.com/openvinotoolkit/npu_compiler/blob/0b38f7d42113ff329ac2bdd33583d123de4ccf2f/src/vpux_compiler/docs/sw_layer_enabling.md)
describes lowering operations to software kernels and registering kernel entries.
Its [kernel build configuration](https://github.com/openvinotoolkit/npu_compiler/blob/0b38f7d42113ff329ac2bdd33583d123de4ccf2f/sw_runtime_kernels/kernels/CMakeLists.txt)
includes distinct 3720, 4000 and 5000 toolchain/library targets. The public tree
contains corresponding prebuilt Gather kernels. Offline ELF inspection found
different binaries with matching 32-bit header identifiers and entry addresses;
the [metadata and hashes](../validation/2026-10-06/sampler-profile/kernel-metadata.json)
are retained. Matching headers do not prove instruction or invocation ABI
compatibility.

The public checkout does not contain the Gather kernel source, descriptor
directory, or the artifact revision file referenced by the MoviTools downloader
at this commit. The default compiler build consumes prebuilt kernels. Enabling
a CMake flag therefore does not, by itself, give this project a complete public
toolchain for replacing the kernel. This conclusion comes from inspection of
the pinned tree and [downloader](https://github.com/openvinotoolkit/npu_compiler/blob/0b38f7d42113ff329ac2bdd33583d123de4ccf2f/sw_runtime_kernels/kernels/cmake/mv_tools.cmake),
not an attempted full compiler build.

Intel also has a [Gather DMA eligibility check](https://github.com/openvinotoolkit/npu_compiler/blob/0b38f7d42113ff329ac2bdd33583d123de4ccf2f/src/vpux_compiler/src/dialect/VPU/utils/gather_dma_utils.cpp).
It constrains dimensions before the gather axis, index count and element size.
Our planar image has four channels before that axis. This suggests a distinct
data-movement route worth understanding, but does not establish how the installed
compiler lowered every experimental layout. In particular, the interleaved
device-loss result must not be attributed to DMA without further evidence.

## Existing custom-kernel work

The independent [npunlock project](https://github.com/hsfzxjy/npunlock/tree/c23eded3d1d9eec88924f5709b3487640bd796d4)
reports custom C kernels on Windows Meteor Lake / NPU3720, using an external
proprietary MoviTools compiler and replacement of compatible graph kernels.
Its [limits](https://github.com/hsfzxjy/npunlock/blob/c23eded3d1d9eec88924f5709b3487640bd796d4/wiki/LIMITATIONS.md)
matter for texture sampling: kernels operate within invocation-local tensor
spans, with no validated graph-global addressing or cross-chunk neighborhoods.
Its [porting plan](https://github.com/hsfzxjy/npunlock/blob/c23eded3d1d9eec88924f5709b3487640bd796d4/wiki/PORTING.md)
treats newer generations as unverified. Our initial investigation only inspected
source. The later Panther Lake experiment below now supplies direct hardware
evidence for our own narrowly validated kernel; it does not qualify the upstream
parser or all of its features on this generation.

The newer public compiler at commit
[`20e7912`](https://github.com/openvinotoolkit/npu_compiler/tree/20e7912fbc230725e1ca7394762348d4f480aa65)
also exposes a disabled-by-default `enable-shave-code-gen` option. Its
[NPU50XX pipeline](https://github.com/openvinotoolkit/npu_compiler/blob/20e7912fbc230725e1ca7394762348d4f480aa65/src/vpux_compiler/src/NPU50XX/dialect/VPU/transforms/pipelines.cpp)
includes kernel fusion and code generation. This is a concrete route to investigate,
but its [binary-generation implementation](https://github.com/openvinotoolkit/npu_compiler/blob/20e7912fbc230725e1ca7394762348d4f480aa65/src/vpux_compiler/src/utils/llvm_to_binary.cpp)
requires `MV_TOOLS_DIR` and `MV_TOOLS_VERSION`, invoking external Linux MoviTools
compiler, assembler and linker paths with 5000-specific libraries. The source does
not establish a self-contained Windows compiler switch. Availability in Delta's
pinned compiler and execution of generated kernels remain untested.

## Panther Lake execution result (2026-10-15)

The [hardware evidence](../validation/2026-10-15/native-delivery-custom-kernel-v1/README.md)
now establishes custom kernel execution on Delta with installed NPU driver 5540
unchanged. An extracted vendor compiler accepts target `5000xx`; our own 288-byte
identity kernel replaces only the code extent in a validated software graph.
All six invocation spans are checked against Intel's NPU50XX definitions and
MemRefData ABI. The upstream generic inspector rejects this graph, so its older
layout was not assumed compatible.

Original add-one, custom identity, and original add-one again each match 128
components across changing inputs, reporting NPU execution only. This clears the
bounded identity execution gate. Proprietary tools and graph binaries remain
outside the repository; redistribution terms remain unresolved. The coarse
recorded timer cannot support a latency claim. Indexed arithmetic, larger texture
spans, fused sampling performance and provider integration remain unqualified.

## Next experiment and acceptance gates

A fused sampler could read four BGRA texels, interpolate, apply opacity and blend
directly into a packed output. That removes generic Gather expansion and multiple
intermediate tensors in principle. It still needs efficient indexed loads,
correct memory spans and enough aggregate throughput; custom code alone does not
establish a speedup.

1. Identify a legally obtainable 5000-capable toolchain and its redistribution
   terms, or establish compatibility of an available toolchain without assuming
   it from a shared ELF identifier. Keep proprietary components outside this repo.
2. Characterize a tiny Panther Lake graph's kernel invocation, tensor spans and
   synchronization offline. Require generation-specific bounds checks before
   considering any replacement kernel.
3. **Qualified for the narrow carrier:** bounded identity with changing inputs,
   strict NPU reporting, an independent oracle, process deadline and known-good
   original execution before and after. Extend next to indexed arithmetic.
4. Establish arbitrary source addressing within an explicitly supplied texture
   span. Invocation-local unary success is insufficient for a sampler.
5. Compare a fused sampler against the existing graph and GPU/Pixman baselines,
   including source preparation, transfers, output conversion and damage commit.
   Only then connect it to the provider behind the existing switch.

This track could expand general graphics computation. It does not automatically
provide Direct3D/Vulkan shader translation, application interception, codec
implementations, system composition or display scanout. Those remain separate
items in the [coverage ledger](../coverage.md).

## Direct3D route

Before depending on a private kernel ABI, inspect the existing driver interface.
The repository now builds `nr_dxcore_probe` to distinguish runtime-independent NPU
enumeration, Direct3D generic-ML attributes and Core compute support. The
[driver-activation evidence](../validation/2026-10-06/driver-activation/README.md)
records the first queries and their pre-restart limits. This route has not yet
executed a shader on the NPU; a shipped Direct3D DLL alone establishes no such
capability.

The [post-restart results](../validation/2026-10-07/README.md) now confirm loaded
driver 5540 and still find no NPU in either DXCore query. Explicit driver-compiler
selection also produced incorrect pixels for a grouped layout, so it is not a
general remedy. An exact-coordinate GridSample fetch alternative passed the pixel
oracle but was slower than Gather at the matched 128×128 output size. These results
leave the fused-kernel/toolchain investigation relevant; neither alternative was
promoted into the native provider.
