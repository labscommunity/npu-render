# Large texture addressing on the native NPU path

The third native desktop trial captures the next failure: DWM samples a
2912×1824 BGRA texture into a 91×64 target. Its source exceeds the affine
sampler's four-million-pixel limit. The trial executes 19 NPU operations but
exits before frame readback; cleanup and exact permission restoration pass.
The expanded, bounded diagnostic captures the complete operation. Desktop
pixels remain on Delta and are excluded from this repository.

Raising the capacity alone is insufficient. CPU replay passes, but NPU replay
returns **17,472 incorrect RGB components**, with a maximum byte error of 67.
Vertex results, projected coordinates, integer texel coordinates and coverage
match. The incorrect values arise at the texture read.

## Independent address-boundary reproduction

Small synthetic OpenVINO graphs isolate scalar FP32 `Gather` on Delta's NPU
under the tested 2026.4.1 runtime and driver 5540. The one- and two-million-element
cases pass. Reads at index **2,097,152** and above wrap to earlier elements in
the tested larger inputs. For example, index 2,097,152 returns element zero.
The captured desktop's wrong values also match reads at wrapped addresses.
This is an observation about the tested compiler/driver/device stack, not a
claim about an architectural limit of every Intel NPU.

A first segmented graph still fails when its range mask uses `LogicalAnd`.
Replacing that mask with bitwise AND of converted integer comparison results
matches the existing workaround in the shader executor and passes the independent
tests. The failed graphs and harness errors are retained rather than discarded.

## Tested workaround

The native-routing-v8 candidate separates larger NPU texture channels into input
ports of at most 1,048,576 elements. Each gather uses a bounded local index;
integer masks select the correct input on the NPU. Sampling, interpolation,
modulation and blending remain device operations. Host work separates packed
channels into those input buffers. Small textures and CPU execution retain their
previous model layout.

ABI 17 admits larger sources for the existing six-vertex affine sampler. It
retains the 32 MiB packet ceiling, existing target-size limits and explicit
rejection of unrelated operation shapes. Twelve malformed/legacy-packet cases
are rejected by the captured-packet test. This does not expand general shaders,
other texture executors or every graphics operation.

Validation on Delta passes:

- Independent segmented NPU gathers on 2,097,160- and 5,311,488-element sources.
- The captured desktop draw on CPU and NPU with all 23,296 pixel bytes identical.
- The same captured draw through the C++ transport and NPU worker, with a device
  execution receipt, delivery ACK and exact reference pixels.
- Eight existing sampling regression runs: CPU and NPU, each with linear, point,
  opaque point and opaque linear modes.

The [report](report.json), [manifest](raw-manifest.json), source patches and
diagnostic sources preserve 184 remotely hash-verified text artifacts. Model
files, caches, desktop pixel captures and executables are excluded. The correction
remains an isolated candidate; these tests do not establish full desktop offload,
interactive performance or a Tahoma benefit.
