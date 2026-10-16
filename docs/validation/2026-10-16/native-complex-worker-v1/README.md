# Five-sample shader integrated into the NPU worker

Worker v12 passes all 972 retained requests with one provider generation and no
failures. Its ten `c71638aa…` draws use the new five-sample shader with one shared
model import. The other routes retain 463 program draws, 126 generic geometry
draws, six long-shader draws and the previously qualified specializations. The
worker shares a mutex-protected inference request across compatible shader
pipelines and poisons that provider after an execution failure; the existing
isolated-process recovery boundary remains responsible for recovery.

A complete independent replay audit verifies 967 outputs unchanged from worker
v11 and five exact new-profile outputs. Of the original recording, 957 hashes
match directly, fourteen match explicit qualified numerical profiles (nine prior
and five new), and one request has no original response. The new-profile images
are tickets 58, 196, 302, 363 and 820. Original captures and expected hashes remain
unchanged. These are explicit FP32 sampler-profile differences, not a claimed fix
to the old renderer and not a byte tolerance.

Before integration, forty full-image comparisons run the candidate and original
NPU renderer in alternating order. All candidate variants also match the retained
independent FP32-reference images exactly. Twenty-five of the forty variants
differ from the old renderer, with maximum component difference one. Original
baseline images match their prior worker hashes. Across thirty warm pairs,
complete draw times sum to 34,590.971 ms on the old NPU renderer and 15,640.569 ms
on the candidate; no warm pair is slower. This comparison uses the actual old NPU
renderer, unlike the CPU-reference timing in the earlier component archive.
It does not measure desktop responsiveness or Tahoma throughput.

The first selected replay renders all seventy requests correctly, but its outer
audit fails because it requires at least one draw on the former generic geometry
route. Both selected draws using that route now use the complex shader. The failed
report remains retained. A new read-only audit validates the exact expected
request-ticket sets, including an empty generic-geometry set, instead of retaining
that incorrect nonzero requirement. Full replay then checks 126 geometry draws and
ten complex-shader draws by dispatch ticket. Its 1,944 parent/child timeline
records remain monotonic and exactly correlated.

The comparison executable retains every numerical mismatch while recording
all four variants; its `passed` field means measurement completed. The enclosing
report explicitly records `byte_equivalent: false`, and promotion separately
requires agreement with the independent FP32 reference. The replay profile loader
retains the original nine-profile validator and validates five additional cases
against the immutable packet and original-response index.

Worker SHA-256:
`f8fd5f47abe54d8b4a1488849c05b65815db5fd9e2eee609ff1a5bccb8290226`.
The candidate adds four complex-shader files and changes only pipeline selection
and the CMake source list relative to v11. The shared-provider preparation modifies
the new shader adapter without changing kernel arithmetic. No CPU reference is
compiled into the worker. The archive pins 123 evidence and source artifacts;
private pixel packets and compiled device models are excluded.

Registered graphics and live desktop qualification remain subsequent gates.
Full desktop offload and a Tahoma benefit are unfinished.
