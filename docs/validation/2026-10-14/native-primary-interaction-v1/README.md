# Native primary mouse input and controller corrections

Full desktop offload remains unfinished. Live v19 delivers the controlled blue
window through native NPU desktop composition, receives a normal Windows mouse
event, and displays the resulting green window on the physical output. Keyboard
qualification fails. A separate ordinary-desktop test reproduces that failure in
the test's character-message validation; its correction passes independently but
has not yet been qualified in this archive's native desktop trials.

The archive retains 187 remote-SHA-256-verified text files (5,537,916 bytes), source
patches, and the new physical-output observer. It contains no captured desktop
pixel packets, model weights, compiled caches, or binaries. Local build sources
match the corresponding remote build manifests. See [report.json](report.json)
and [raw-manifest.json](raw-manifest.json).

| Trial | Result |
| --- | --- |
| Live v18 / admission v29 | A Win32 sharing violation in the controller's frame reader terminates admission while the NPU is producing blue frames. Driver cleanup and ordinary window/cursor/foreground restoration pass. |
| Shared-frame I/O qualification | A real temporary Windows file lock recovers; a persistent lock reaches its bounded deadline; access errors and invalid bounds remain rejected. Pixel and producer validation are unchanged. |
| Live v19 / admission v30 | 1,652 NPU receipts and acknowledgments, 41 observed native frames, and 38 exact physical-presenter submissions. Blue and green are observed beneath the presenter with ownership checked before and after reading. The controller retries two sharing conflicts; the relay retries one. No sharing timeout or NPU provider error occurs. |
| Original keyboard test on ordinary desktop | Tagged `VK_PACKET` key-down arrives; `TranslateMessage` produces the expected `WM_CHAR` without that tag. The old test rejects it. This reproduces the missing red transition without the NPU driver. |
| Corrected keyboard test | The expected character must follow an authenticated packet key-down, within two seconds, while the owned window retains focus. Mouse and keyboard receipts pass. A character without that preceding event is rejected, and a duplicate does not add another key receipt. The 11 frame-reader contract cases also pass. |
| DXGI output observer | On the ordinary physical desktop, all three controlled colors pass and an incorrect owner is rejected. Only two pixels are copied to CPU memory per acquired output frame. No screenshots are saved. This adds explicitly counted GPU diagnostic copy work. |
| Cache-name investigation | The small NPU fixture hits the cache despite changed generated node names, with exact synthetic output. Changed constants miss. Canonical names also work. This does not establish cache behavior for every desktop graph or justify changing the renderer. |

The eleven-minute native admission passes its finite rendering checks. This is not
30-minute stability qualification or a throughput benchmark: the input fixture
deliberately repaints every 15 seconds, request recording remains enabled, and
diagnostic copies and screen reads add work. Several individual draws and cold
compilations still take seconds. No matched Tahoma performance test is included.

The GDI screen observer confirms blue four times and green 446 times, but never
red. Some reads block for tens of seconds. Read completion timestamps and ownership
checks on both sides of each physical read prevent the earlier pre-read ownership
assumption, but these samples are not suitable for a response-latency claim. The
new DXGI observer uses a 100 ms acquisition wait and nonblocking GPU-map retries
with a two-second deadline. It still observes the output surface, not the panel
through a camera.

Live v19 also exposes a topology-helper timing defect: counting sleep iterations
overruns the wrapper's deadline. The exact intermediate topology restoration
therefore fails. After indirect-device removal, the helper verifies the physical
baseline and restores the captured ordinary window, cursor and foreground. Final
driver/package/trust cleanup passes. Helper v9 uses an elapsed-time deadline;
its build and topology contract pass, while its native recovery qualification is
outside this archive.

Observer v1's failed build is retained; a helper name collided with a Windows
typedef. The first two NPU cache probes also remain as failures: the Python NPU
property returned a string, which the probe incorrectly split into characters.
Probe v3 handles either a string or a list and verifies NPU execution. Neither
probe failure is evidence of incorrect NPU arithmetic or a device failure.

Next, combine the independently qualified keyboard, output-observation and timing
corrections in a fresh native trial. Then qualify ordinary pointer confinement and
cursor display, failure recovery, responsiveness, application coverage, and a
matched Tahoma benefit. Controlled input receipts alone do not establish a usable
desktop switch or migration of all application GPU work.
