# Exact texture normalization and broader geometry inventory

Two scalar-division replacements are exact for every byte value on Delta's NPU.
Each probe checks 3,145,728 float32 results (6,291,456 raw uint16 words), including
all 256 inputs in every plane, and passes the original carrier before and after.
Both renderer variants also pass 96 source-window fixtures and three comparisons
against the existing NPU renderer for recorded request 41. No desktop changes
occur in these experiments.

The first variant multiplies by the rounded float32 reciprocal of 255 and corrects
one ULP for the 126 byte inputs where multiplication otherwise differs from exact
float32 division. The second also replaces integer-to-float conversion with an
exact bit construction and subtraction. CPU exhaustive derivations accompany
the independent NPU bit checks. All kernel arithmetic remains on the NPU.

Warm full-draw times are 358.20/357.83 ms for variant 1 and 359.79/363.50 ms for
variant 2. The immediately preceding coordinate kernel measured roughly 364–376 ms
on separate changing-image trials. These are not a paired comparison with that
predecessor; no precise causal improvement is claimed. Removing the normalization
divisions therefore has not resolved the remaining desktop performance gap.
Neither candidate is promoted into the live worker.

A read-only inventory of the complete 972-request replay finds 24 uses of the
same four-sample pixel shader, all with six vertices. The current live fast path
admits three. The others have different output sizes, source regions, triangle
order, or constant color. The inventory uses CPU reference geometry only and
exports no pixel data; it is not proof that these cases execute in the new NPU
fast path. It supplies the next broader geometry qualification.

Raw reports, source, assembly and byte hashes are retained. Models, executables
and captured pixels remain excluded. This evidence establishes component
correctness, not responsive desktop offload, reduced GPU load or Tahoma benefit.
