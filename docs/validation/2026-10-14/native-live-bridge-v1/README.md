# Live native desktop bridge: partial delivery, failed sustained trial

The `native-live-v2` experiment submitted two different NPU-composed indirect
desktop frames to the physical Arc output. Both source sequence identities matched
the relay, and both uploaded backbuffers were byte-exact. The second frame contained
the expected green pixel from the controlled window on the indirect desktop.
The **overall trial failed**. This is not usable desktop offload.

The sources were 62,027.6308 ms apart. Source publication to successful `Present`
return took 717.6745 ms and 112.4997 ms, including presenter startup for the first
frame. These measurements do not establish physical scanout or input latency.
The bridge issued two uploads, two presentation copies and two diagnostic readback
copies on the GPU; its own shader dispatch count was zero. DWM's residual GPU work
and the actual display presentation mode remain unmeasured.

DWM completed 368 NPU operations and received 368 delivery acknowledgments before
the worker failed with `ZE_RESULT_ERROR_DEVICE_LOST` in `zeFenceHostSynchronize`.
Three earlier full-resolution, 24-vertex draw packets were rejected by the transport
contract. The captured first rejected draw identifies a capacity mismatch; it is
not evidence identifying the later device-loss request or its cause.

Independent cleanup removed the indirect device before the topology helper could
restore the original clone, so that intermediate restoration returned status 87.
The final physical display baseline matched exactly, process ACLs were restored,
and memory integrity remained enabled. All owned driver and trust cleanup passed.

The preceding `native-live-v1` stopped before driver installation because the
admission process guard rejected the read-only topology observer. The revised guard
permits only the qualified observer executable with its exact `--inspect-topology`
command; live presenters still block admission.

## Evidence and scope

[report.json](report.json) records the assertions and explicit incomplete gates.
The archive preserves 79 remotely SHA-256-verified text files (761,584 raw bytes),
including failed trials. [raw-manifest.json](raw-manifest.json) hashes both original
and deterministic gzip artifacts. `sources/` contains the isolated presenter,
reader qualification, relay and controller candidates. Desktop pixels, binaries,
models and caches are excluded. These candidates are not promoted to the default
runtime.

The presenter rejects clone layouts, route/producer changes, regressing frame
identity, locked sessions and invalid packets. Its packet reader passed eleven
positive/negative cases. The experiment retained a finite duration and emergency
exit. Input forwarding, primary desktop migration, stable frame pacing, application
GPU migration and Tahoma benefit were not tested.
