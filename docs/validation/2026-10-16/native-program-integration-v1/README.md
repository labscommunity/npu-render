# Bounded shader integration: precision attribution and batching rejection

The structural lowerer admits fourteen of the 24 observed pixel programs,
representing 605 captured draws. It implements bounded MOV, MUL, DIV and SAMPLE
without a shader-hash allowlist. This archive preserves the host source versions,
failed and successful builds, admission inventories, selected comparisons and the
independent arithmetic investigation. The custom kernel itself is qualified in
[the separate component archive](../native-program-kernel-v1/README.md).

The first complete selected suite uses integration v3. Ten of twelve cases match
all four original/changed-image variants. Two fail strict comparison:

- Ticket 18 matches its original image and two variants. Its randomized variant
  differs in 856 byte components, all by one. This is traced below.
- Ticket 772 differs in 1,374 original byte components and takes 22.38 seconds
  versus 3.74 seconds for the old NPU path. Candidate requests increase from 362
  to 5,066. This request uses the existing linear rectangle path; replacing it
  with generic triangle bounds loses its specialized scheduling and changes
  interpolation arithmetic. This case remains rejected for generic replacement.

Ticket 18 uses SAMPLE followed by alpha replication into all output channels.
Diagnostic v5 preserves the complete synthetic per-pixel inputs and both output
images on Delta, with hashes in the analysis report. Across all 10,200 covered
pixels, original and candidate XY inputs agree in all 20,400 components. Every
old graph color equals the independently computed FP32 filter rounded to FP16;
only eight equal the unrounded FP32 value. The candidate's 40,800 covered byte
components match the independent FP32 filter and ties-to-even conversion exactly.
All 856 differences from the original image are explained by its FP16 reduction;
pixels outside the covered region agree. The [analysis report](raw/ptl-program-rounding-analysis-v1/report.json)
contains the formulas, hashes and examples. This establishes the filtering cause
for this synthetic capture, not arbitrary geometry or every shader program.

For example, pixel 108 filters to `0.2608461081981659`, which scales to about
`66.51575` and rounds to 67. The old graph returns `0.2607421875`, the FP16-rounded
value, which scales to `66.48926` and rounds to 66. Input coordinates are identical.
No byte-error tolerance is introduced and the original failed comparison stays
preserved.

Integration v4 keeps the existing NPU rectangle path and uses up to 12,288
samples across all six spans for the general triangle path, independent of the
old pixel graph's batch limit. Its CPU-only admission inventory reports:

| Result | Draws |
| --- | ---: |
| Accepted | 381 |
| Preserve existing rectangle path | 63 |
| Texture extent outside the candidate bound | 20 |
| Unsupported filter | 87 |
| Unsupported blend mode | 54 |

These reasons are ordered first-failure categories within the 605 structurally
lowerable draws. They do not imply that other requests use the GPU; the existing
qualified NPU paths remain available.

Integration v5 adds a test-only independent alpha-filter reference. It uses the
original NPU geometry/interpolation and merger with explicit scalar FP32 filtering
between them. The candidate still performs all rendering on the NPU. The reference
source is excluded from the proposed worker. Its synthetic ticket-18 image must
match the independently audited hash above. Later complete comparison and worker
qualification are separate evidence; no new live desktop or Tahoma benefit is
established here. Diagnostic runs include instrumentation overhead and are not
performance benchmarks.
