# First compact-program image comparison rejected

The selected comparison does not qualify. All observed candidate images match
the preceding NPU program renderer, including the original captured images, but
three cases disagree with the additional CPU shader reference. Cases 18, 280
and 334 report eight, five and 44 differing components respectively. The latter
two stop after their first variant. The reference combines a CPU shader with
the earlier NPU geometry and merger path. This run does not establish the cause
of those disagreements, and does not report them as exact reference passes.

The timings also expose regressions on small draws. Case 17 takes 115.328 ms
across its three warm candidate runs versus 32.664 ms for the preceding renderer.
Both make six provider calls per image. The new host allocates larger batches
and fills their unused entries with synthetic offscreen samples; the NPU still
processes those entries. A subsequent candidate must test submitting only
filled entries. That hypothesis is not validated by this archive.

The full image comparison and planned desktop admission do not start. The
desktop coordinator records the failed prerequisite and exits before registration.
The first two comparison coordinators also stop before building, because their
preceding worker qualification gates had failed. All these attempts are retained.

The archive pins 62 source and evidence files. Private pixel artifacts remain
on Delta; their recorded hashes are retained. No image tolerance, new numerical
profile, desktop speedup or Tahoma benefit is claimed. The next comparison must
keep previous NPU image preservation separate from independent-reference accuracy
and record any pre-existing reference discrepancies explicitly.
