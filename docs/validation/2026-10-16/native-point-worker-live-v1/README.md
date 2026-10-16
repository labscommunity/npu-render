# Point-sampling worker and finite desktop qualification

Worker v11 passes all 972 retained requests with exactly the same output hashes
as worker v9: 962 original references, nine existing qualified numerical
corrections and one request without an original response. The worker admits 463
program draws, including all 87 newly supported point-filter draws, with one
shared program import. The remaining geometry route executes 136 draws.
One provider generation completes all acknowledgments without faults. Registered
D3D11 v37 passes its eight operations, restoration and memory-integrity checks.

Finite desktop v48 passes 1,223 acknowledged NPU operations and 56 exact physical
submissions, with 36 distinct published image hashes. The new point-program route
executes 99 times among 583 program draws. A normally started non-admin application
shares DWM's renderer through its ordinary default-adapter request, completing
eight correlated NPU operations before DWM continues. This remains a bounded
fixture: continuous arbitrary-application admission is unqualified. The separate
unadmitted observer's failed device creation remains visible in the raw report.

Controlled mouse and keyboard input, independent blue/green/red physical-output
readback, foreground confinement and two pointer reacquisitions pass. Original
display topology, ordinary windows, cursor state, access lists, tasks and drivers
restore successfully. Memory integrity remains enabled. There is one provider
generation and no faults or retries.

The mouse response takes 2,948.920 ms to publish and 3,087.583 ms to complete the
independent physical-output pixel read. Keyboard response takes 102.054 ms to
publish and 251.850 ms to finish readback. These two events are not a matched A/B
comparison or a p95 distribution; they do not establish a causal improvement over
v46. Responsiveness remains inadequate. Before the mouse frame is published,
2,241.084 ms overlaps serialized child rendering and 707.836 ms falls outside it.
The rectangle shader `595296ba…` contributes 557.551 ms and the five-sample shader
`c71638aa…` contributes 690.895 ms. Those remain the immediate rendering targets.
Physical presentation still uses GPU copies. GPU relief and Tahoma are unmeasured.

Two earlier controller failures are preserved. The selected-worker replay itself
passes, but its original audit incorrectly assumes client request sequences are
globally unique. A separate read-only revalidation joins global dispatch tickets
to replay iterations and original packet tickets; full replay and the independent
972-image audit then pass. Failed live v47 encounters the non-secure ASUS OLED
screen saver before native admission. Its incomplete initial report is retained,
alongside the failed observer and a separate interruption audit confirming that
native admission never started and no graphics experiment process remained.

After ordinary input and window-close requests fail, the recovery helper verifies
the exact non-secure screen-saver image, command line, session, creation time and
process handle, then closes that process. It changes no screen-saver, lock or driver
settings. The retry keeps the readiness observer's display-awake request active
through live handoff and captures the child controller's exit code separately from
its error stream. Live v48 also removes its exactly identified observer task.

Worker SHA-256:
`c445ff2bb154e879609933508187af886100062ee60ee29fd1467f776316f12f`.
The only graphics-source change from worker v10 is the previously qualified
point-program header. The registered UMD and driver package remain unchanged.
The archive pins 243 source and evidence artifacts, including both failed audits
and successful revalidations. Private pixel packets and compiled models are excluded.
`summary.json` derives counts and timing from the retained reports.

Full desktop offload, sustained responsiveness, general application coverage and
a Tahoma benefit remain open.
