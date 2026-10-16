# Captured shader instruction inventory

A read-only export retains the shader token streams from the completed desktop
capture, without texture or framebuffer pixels. Each stream matches its retained
shader hash. The qualified parser decodes all 24 distinct pixel programs locally.
These programs account for 771 captured draws; the other 201 requests are clears.
This is CPU parsing evidence, not new NPU execution or application coverage.

Many programs have only one to six instructions, using moves, multiplies, samples,
division or a dot product. The more complex programs include derivatives, integer
operations and loops; the largest contains 224 decoded instructions. The opcode
histogram and complete operands are preserved in `raw/summary.json` and
`raw/decoded.json`.

This motivates a reusable bounded instruction interpreter for common shader
operations, with explicit resource addressing and unsupported-operation handling.
It may reduce per-program graph compilation while retaining the existing
specializations for expensive paths. An instruction inventory alone does not
establish that such a kernel fits the carrier, produces equivalent pixels or
improves performance. Those require independent component fixtures, changed-image
draw comparisons, complete replay and live integration.
