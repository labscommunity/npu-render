# Direct solid-color rectangle groups on the NPU

Five expensive recorded desktop draws now match exactly using five NPU calls each,
compared with 595–643 calls in the existing renderer. The final component takes
about 13–23 ms per tested draw versus approximately two seconds in the baseline.
All 20 captured/changing-color comparisons match all pixel bytes. These are
component comparisons, not desktop frame rates or evidence of a Tahoma benefit.

The initial direct kernel takes about 78–98 ms. The revised kernel quantizes
constant colors once per rectangle on the NPU and processes four adjacent pixels
in one vector operation for source-copy blending. Other blending uses the exact
scalar path. An alignment guard sends unaligned and tail pixels through that
scalar path. The intermediate vector build is retained but was not executed: code
review identified the missing vector-load alignment condition before qualification.

Both executed kernels pass 96 independent rendering fixtures and 12 malformed
payload cases, with the original carrier checked before and after. Fixtures cover
partial batches, scissor, channel masks, source-copy/source-over, RGBA/BGRA,
constant colors, rectangle overlap and raw output padding. Output receipts include
actual NPU coverage counts. Malformed payloads produce a cleared result that the
host rejects.

The host admits the exact vertex/pixel shader bytes and model hash, then validates
NPU-projected rectangle pairs and constant vertex color. Vertex work, projection,
coverage, pixel color, blending and quantization run on the NPU. The host copies
inputs and completed byte outputs. Unsupported geometry remains with the existing
NPU renderer and is reported explicitly in worker integration.

The read-only geometry inventory includes 115 solid-color/two-texture requests.
The five admitted multi-rectangle draws account for about 10.8 seconds of drawing
in the earlier diagnostic replay. This inventory uses CPU reference geometry only;
the actual five-draw comparisons separately verify NPU execution and exact output.
Models, executables and captured pixels remain excluded. The combined native worker
and live desktop must still be qualified before this component can claim those
integration results.
