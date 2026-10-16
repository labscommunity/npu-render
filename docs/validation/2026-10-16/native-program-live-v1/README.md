# Reusable program worker: live failure and timing

Native live v43 fails after 44 submitted frames and 767 acknowledged NPU
operations. The worker completes 309 bounded-program draws with one import and
169 shared-geometry draws. It then times out on dispatch ticket 767, a 192×64
six-vertex request using the existing 181-instruction pixel shader
`ec1e65f6664c9faf5af829eebdc7799cdde111ce4bad9f730ea5b13878546804`.
This program is outside the new bounded kernel and uses the existing NPU graph
path. The failed packet is retained on Delta, with SHA-256
`46caf6546bcde449d3829a05aaedfcefc719686f25264cec6786065df7a628c3`.
No fallback succeeds or is concealed.

The relay correctly fails when its producer exits. Native cleanup and pointer
cleanup pass, owned process access rules are restored exactly, and memory
integrity remains enabled. The controller does not qualify sustained rendering,
full desktop offload or Tahoma performance. The completed replay and registered
D3D results remain valid within their separate scopes.

Two controlled input events precede the failure. The original timing analysis
uses pixel-read start timestamps, giving 5,822.53 ms for mouse and 250.86 ms for
keyboard. The separate presentation analysis preserves those observations and
adds the read-completion and matching presentation-statistics timestamps:

| Interval from input | Mouse | Keyboard |
| --- | ---: | ---: |
| Frame publication | 2,931.25 ms | 98.01 ms |
| Matching Windows presentation statistics | 2,977.14 ms | 142.50 ms |
| Independent pixel read completed | 5,869.73 ms | 266.86 ms |

The mouse frame spends 2,225.46 ms in serialized child requests before publication,
with another 705.79 ms outside those intervals. Child work after publication is
excluded from that attribution. Windows presentation statistics report the frame
about 45.89 ms after publication, while the independent observer obtains a later
matching desktop update. These are distinct observations; neither measures panel
photons, and two events do not establish p95 or a matched speedup. The analysis
checks the submitted/displayed/media counts, matching refresh counts and QPC
values; the interpretation follows Microsoft's
[frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics)
and [duplication metadata](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info).

The immediate gate is reproducing and removing the captured timeout. A faster
component does not compensate for an unsupported graph blocking the desktop.
Application admission, responsive sustained operation, residual GPU work and a
matched Tahoma benefit remain required. The archive preserves final logs,
controller sources, source hashes and the failed reports without modification.
