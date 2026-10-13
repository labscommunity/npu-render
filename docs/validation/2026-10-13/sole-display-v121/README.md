# Expanded-cache live NPU desktop trial

All six scripted VM visual checks pass with `desktop-worker-v6`: desktop,
Run, Notepad, exact text `npu desktop offload v121`, maximize, and close.
All 1,419 rendering requests execute on Delta's physical NPU and receive
successful acknowledgements; none receive negative acknowledgements. The collector
receives 41 frames with the sole-indirect-display gate active. These counts are
not an FPS or frame-completeness measurement.

The worker uses the qualified larger pipeline cache, pinned common-queue option,
and point-crop reentry fix. The signed guest package remains v120/0.0.105.0.
Actual hardware queue count is not measured. The full 4,416,524,196-byte request
trace stays on Delta with a consecutive SHA-256 index. All 37 guest archive
members are hash verified.

The [original manual gates](report.json) retain the frames admitted during the
live run. They overstate latency where review was delayed. The
[post-review](post-review.json) identifies earlier complete frames retained by
the bounded witness recorder. Notepad appears after 150.725 seconds, exact text
after 66.565 seconds, maximize after 122.983 seconds, and close after 78.018
seconds. The Run gate is 252.873 seconds and initial desktop gate 90.202 seconds.
These remain input-to-received-frame upper bounds with VM, transport, rendering
and collection overhead. Earlier frames were visually checked; no exhaustive
first-displayed-frame or controlled improvement claim is made.

All requests expand to 53,466 provider submissions. Host draw intervals total
277,460.532 ms and setup-plus-draw intervals 479,712 ms. There are 1,955 model
setups, including 1,953 disk-cache hits. The preceding v120 trial recorded 61,822
submissions and 745,397 ms setup-plus-draw, but its request count and interaction
timing differ. Use the identical-request replay comparison for a more controlled
cache experiment; these live totals are not an isolated speedup measurement.

The original display topology is restored, both owned guest devices removed,
stream configuration removed and input admission revoked. Two incomplete guest
draw captures and HRESULT `88760870` rejections appear during adapter removal,
after the stop-file timestamp. The early complete-failure watcher records no
captures, and the rendering worker has no negative acknowledgements. The numeric
HRESULT is preserved without assigning an unverified cause. The bounded worker
exits with the recorded socket-close/timeout message. The owned test document is
cleared only after ordinary display restoration; the VM then shuts down cleanly.

Full desktop offload remains unfinished. This trial does not route Delta's
physical desktop, qualify arbitrary applications, establish usable interaction,
or demonstrate a Tahoma benefit. Delta's graphics driver and NPU driver remain
unchanged.
