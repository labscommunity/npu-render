# Re-enter the point-copy fast path after partial draws

A partial draw previously retired the crop executor permanently for that cached
pipeline. Later full-target copies consequently ran through general triangle
rendering. The fix proves crop eligibility again for each subsequent draw,
releasing old provider requests before allocating replacements. Only a failed
crop proof selects the general path; provider and malformed-input errors propagate.

All 28 CPU suites pass. The physical NPU matches 3,576,128 scalar-oracle components
and rejects 16 invalid inputs. Eighteen adaptive frames cover repeated full,
partial and scaled transitions, with changing source and background pixels in
both channel orders. The complete 1,438-request desktop trace preserves every
pixel and coverage count. Three later copies use **3 NPU calls instead of 1,538**
each; total calls fall from 61,822 to 57,221.

Two still-ineligible draws each add two proof calls, and model setup calls rise
from 1,711 to 1,730. Elapsed replay time is 631.781 seconds versus 641.906 seconds
for the same expanded-cache configuration without this fix. This single,
uncontrolled comparison does not establish a repeatable desktop speedup.

The GPU control rejects an unchanged captured-backdrop projection before the
adaptive tests. The original `present-umd-v229` binary fails at the same stage;
its evidence and the failed qualification are preserved. This change is qualified
on CPU and NPU; GPU qualification is not claimed.

[report.json](report.json) and the raw manifests verify the device results and
full replay. The comparison helper permits changed receipts only for the explicit
point-copy operation; it still requires exact pixels, request identities and
coverage. Sixty-three checks exercise that boundary. All other receipts remain
exact. No new live desktop or Tahoma result is established by this qualification.
