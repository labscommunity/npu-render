# Short duration integration and default application routing

Live v28 completes controlled desktop rendering and ordinary restoration, but
delivers only 31 correlated physical submissions over 512 seconds. An actual NPU
device loss takes 63 seconds to recover. Ordinary unleased applications cannot
create a default Direct3D device while the native adapter is primary. This is
progress in bounded integration, **not usable full desktop offload**.

The archive contains 278 remotely SHA-256-verified text artifacts (4,999,551
bytes), source patches, and an independently checked summary in
[report.json](report.json). Captured pixel buffers and compiled models are not
included. No Tahoma workload ran in these trials.

## Native desktop result

The qualified worker is routing v41, with trace/UMD v19 and package v17. The
bounded lease ceiling, incremental evidence reader, notification-driven relay,
presenter v20, output observer v4 and cursor watchdog v2 pass component checks
before registered v23 and native admission v38. The renderer retains the existing
4096 generic batch, quad path, isolated provider, and disabled memory sharing.
The scoped-upload and scalar-expression candidates are not enabled.

| Observation | Result |
| --- | --- |
| Acknowledged native NPU operations | 1,536, with 1,536 matching hardware child receipts |
| Provider generations / real faults / retries | 2 / 1 / 1; no CPU/GPU rendering fallback |
| Retried request | Ticket 991, 63,000 ms including child transport and recovery |
| Correlated physical submissions | 31; all backbuffer copies exact |
| Presenter lifetime / first-to-last submission | 512.000 / 503.484 seconds |
| Submission interval median / p95 / maximum | 10.081 / 86.555 / 98.797 seconds |
| Mouse sent → received / first green output observation | 9.853 / 37.375 seconds |
| Key sent → received / first red output observation | 0.065 / 105.508 seconds |
| Presenter GPU work | 31 source uploads, 62 copy commands including 31 diagnostic readbacks |
| Restoration | Exact topology, ordinary windows, foreground and pointer; two confinement reacquisitions |

Intervals use QPC and nearest-rank p95 over 30 successive submissions. The
stimulus repaints every 15 seconds, so these intervals are not a saturated frame
rate measurement. Input timings are two controlled observations, not a
response-time distribution. After the key event, the stimulus cycles colors;
the first observed red can correspond to a later red repaint. Its 105.508-second
interval is therefore an observation delay, not a uniquely identified frame's
input latency. A future stimulus must hold the initial response until observed
before starting animation.
Output duplication verifies controlled colors with presenter ownership checked
before and after the read; it is not an external measurement of panel photons.
The observer also performs GPU copies. These counts do not establish GPU time,
power savings, or released capacity for Tahoma.

The 63-second retry follows `ZE_RESULT_ERROR_DEVICE_LOST` from the first child's
`zeFenceHostSynchronize`. The same request succeeds in generation two. Recovery
is functionally correct and far too slow for the responsiveness target.

## Ordinary application gap

On the ordinary desktop, both default `D3D11CreateDevice` requests select Intel
Arc. During native primary routing, adapter zero becomes the native renderer and
Arc moves to index one. Both default creation requests then fail with
`DXGI_ERROR_UNSUPPORTED`, including a request restricted to feature level 10.0.
The observation probe submits no draws; its own successful exit means the
observation completed, not that application rendering worked.

The current UMD admission contract requires a lease identifying the consumer
process. The desktop trial provides compositor/indirect-host leases, but does not
admit arbitrary applications. A separately leased application using default
Direct3D selection is the next component test. Automatic admission, application
capability coverage, browser/video/game behavior, and session lifecycle remain
open even if that component passes.

## Scalar-expression diagnosis

A captured 2880×1800 draw reproduces both the previous NPU baseline and the failed
candidate. All 20,736,000 output components match between the CPU baseline and
CPU candidate. On NPU, scalar constant-buffer lookup changes three components,
each by one byte. Scalar conditional selection alone matches the previous NPU
output. An immediate broadcast variant changes 65,413 components and is rejected.

The previous NPU output itself differs from CPU in 141 components; the scalar
candidate differs in 140. These results narrow the mismatch to provider lowering
or numerical behavior for this draw. They do not prove general shader equivalence,
authorize a blanket one-byte tolerance, or demonstrate a speedup. The uniform
candidate stays disabled.

## Preserved failures and next gates

The first notification fixture fails because its ordinary file reader conflicts
with atomic replacement. The revised fixture uses the existing shared-delete
production reader and passes replacement, coalescing, timeout and closed-wait
checks. The first helper trial fails on the ASUS non-secure screen saver. A
fresh identity-checked dismissal allows the second helper trial to pass; lock
settings are unchanged. The first short controller exits before admission because
its helper prerequisite failed. These failures remain in the raw evidence.

The longer controller and the ordinary default-adapter application proof are
separate pending trials. Thirty-minute stability, 30 fps interaction, p95 visible
response ≤100 ms, general application admission, residual GPU cost, and a matched
Tahoma benefit remain unqualified.

Intel's current download still lists driver 32.0.100.5540, the installed version.
[Intel driver](https://www.intel.com/content/www/us/en/download/794734/intel-npu-driver-windows.html).
An independent report on Lunar Lake with the same driver describes intermittent
device loss. Its later results contradict the initial turbo/keep-alive workaround:
failures also occur during continuous submissions. That is a useful diagnostic
lead, not an established explanation for Delta's Panther Lake failure.
[OpenVINO issue 38403](https://github.com/openvinotoolkit/openvino/issues/38403).
