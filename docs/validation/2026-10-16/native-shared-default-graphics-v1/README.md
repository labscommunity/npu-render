# Ordinary default-adapter application sharing the NPU desktop

Finite live v46 passes with worker v10 and registered UMD v2. A normally started,
non-elevated session-1 application creates its D3D11 device with no explicit
adapter and no configuration export. Automatic admission connects it to the same
session-zero worker used by DWM. Six clears and two draws execute on the NPU,
with eight matching parent receipts, child receipts and delivery acknowledgments.
DWM continues rendering after the application exits. The fixture records zero
CPU/GPU rendering executions and rejects creation of another pipe server.

The complete desktop trial records 1,209 acknowledged NPU operations, one provider
generation, no faults or retries, and 53 exact matching physical submissions with
36 distinct source hashes. Controlled mouse/keyboard input and blue/green/red
physical-output readback pass. Topology, ordinary windows, cursor confinement,
owned access lists, driver removal and task cleanup restore successfully. Memory
integrity remains enabled. The admission broker's finite lease and publication
disappear after its consumer exits.

This qualifies one bounded default-adapter fixture sharing DWM's renderer.
It does not establish continuous admission for arbitrary applications. The older
unadmitted adapter observer still fails default device creation in this trial;
its `application_graphics_offload_qualified` field remains false. The separately
correlated admitted fixture sets `shared_default_application_qualified` true.
The raw reports retain both results. Multiple applications, lease renewal,
existing-device migration, browsers, video and games remain unqualified.

Responsiveness remains inadequate. Mouse input takes 6,505.889 ms to publish its
response frame and 6,603.050 ms to finish independent physical-output pixel
readback. Keyboard confirmation takes 237.390 ms. Of the mouse interval before
publication, 5,503.955 ms overlaps serialized child rendering; 1,001.934 ms lies
outside it. Shader groups `595296ba…` and `c71638aa…` contribute 3,766.086 ms.
These are two controlled events, not a matched comparison or a p95 distribution.
Physical presentation still uses GPU copies; the independent measurement observer
adds 152 copy commands. GPU relief and Tahoma performance were not measured.

The preceding v45 trial fails in its evidence checker, despite the application
itself completing its eight operations. The checker wrongly equates a child
request's sequence with the global dispatch ticket plus one. Sequences are local
to each client and are preserved through the child transport. A read-only audit
correlates the retained operations by shared dispatch ticket, client sequence,
identity, request fields, receipts, acknowledgments and nested QPC intervals.
Mutated sequence, ABI, width and shader hash are rejected. V45 remains failed;
its restoration and memory-integrity checks pass. V46 repeats the experiment with
the corrected checker and unchanged graphics binaries.

Worker SHA-256:
`6d53376847d257c8ffa05bdc73b9319be87acf8ffb33a2f984f62dca9a1954b0`.
Installed UMD SHA-256:
`5f69fc41501fce6bde16ac4be3c91b4c7e689eb511985593d4b8d414c42f122c`.
The archive pins 196 source/evidence artifacts, retaining both trials and the
correlation audit. Private pixel packets and compiled device models are excluded.

Full desktop offload, sustained responsiveness and a Tahoma benefit remain open.
