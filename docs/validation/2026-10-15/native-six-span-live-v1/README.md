# Six-span renderer in the native desktop path

The qualified six-span component now passes the complete 972-request recording,
registered Direct3D qualification and a finite live Windows desktop trial. The
live trial delivers 42 matching physical-output submissions over a 523.93-second
span, with 28 distinct source hashes and four fresh owned output observations.
All ordinary topology, window, foreground and pointer restoration checks pass.
The worker has one provider generation, no provider fault and 1,268 acknowledged
NPU operations. The new four-sample specialization is actually admitted three times.
No OS boot setting or memory-integrity setting changes.

**This is integration and liveness evidence, not usable full desktop offload.**
The diagnostic uses a 60-second freshness bound and a sparse controlled stimulus.
The first independently observed green after the mouse receipt takes 27.2093 s;
red after the keyboard receipt takes 45.77 ms. These two observations include
sampling cadence and are not a latency distribution or photon timing. The
[derived timing record](controlled-response-analysis.json) identifies the raw QPC
values. The earlier renderer's thirty-minute qualification is not transferred to
this new renderer.

The presenter makes 84 GPU copy commands, including 42 validation readbacks. The
measurement-only output observer adds 254 copies. Scanout mode and physical-panel
photons are not independently established. Remaining GPU cost and Tahoma benefit
are unmeasured. Normal unleased application admission still fails; browser,
video and game migration remain open.

## Why the next work changes focus

The complete replay finishes all 972 requests and matches all 971 available original
pixel references. One request has no original reference; its successful replay is
not an additional original-reference match. It still
makes 33,174 provider calls. Summed draw time is 180.53 s, with 239.83 s including
preparation; these are single-run diagnostic sums, not a matched baseline/offload
benchmark. The slowest remaining recorded draw takes 5.03 s with 322 calls and no
compilation. The live worker likewise has multi-second draws and model preparation.

The largest shader groups in the replay are a three-texture compositor shader
(21.84 s drawing), a two-texture shader (21.64 s), and several other groups. The
four-sample shader appears 24 times, but the current exact admission covers only
three of those requests. Thus the successful specialization does not remove most
of the desktop cost. The next renderer work must cover broader geometry and
additional measured shader groups, while reducing scheduling and per-pixel costs.

The registered Direct3D test passes eight operations with the new worker and
restores its temporary adapter. The full replay and live trial use the same pinned
worker and model as the guarded component qualification. Raw source, source
manifests, build references, receipts, failures if any, and recovery logs are
retained with byte hashes. Compiled models, drivers and private pixel payloads
remain excluded.

[Acceptance plan](../../../research/desktop-offload-plan.md): responsive sustained
interaction, full application coverage, matched Tahoma A/B and a usable switch
are still required. The repository remains private.
