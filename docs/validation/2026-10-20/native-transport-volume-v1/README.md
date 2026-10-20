# Whole-surface transport remains an architectural cost

The retained ten-second desktop animation contains 209 complete requests, each
targeting 1024 × 768 pixels. They carry 657,457,152 bytes of destination background
and return the same number of pixel bytes. Across the client-to-parent and
parent-to-child boundaries, that is **at least 2,629,828,608 logical protocol bytes**.
This lower bound excludes textures, headers, partial requests, internal copies
and NPU tensor traffic. It is not measured DRAM or PCIe bandwidth.

The separate 972-request capture contains 4,780,282,804 request bytes and
3,045,051,772 reply bytes. It is a different workload and does not predict live
frame rate. The [report](report.json) pins its sources; the
[manifest](artifact-sha256.json) retains the analysis and result.

Code inspection shows that the current driver synchronously encodes a complete
draw packet, waits for a reply and decodes the returned surface. The parent
decodes the request again before forwarding it to the rendering child.
These are candidates for measurement and redesign; this analysis establishes
no achieved speedup.

Windows supports driver-side command batching, with submission boundaries
including presentation, explicit flush, capacity and required CPU access.
[Microsoft's command-buffer documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/submitting-a-command-buffer)
and [resource-access guidance](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/copying-and-accessing-resource-data)
explain those boundaries. A Direct3D 11.1 flush must leave submitted work
independent of further application-thread actions.
[Flush contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d11_1ddi_flush)

The engineering inference is to investigate retained resource versions and
ordered command batches alongside kernel fusion. Correctness requires explicit
resource lifetimes, read/write dependencies, map/unmap synchronization,
presentation completion and provider-restart recovery. Simply delaying the
existing synchronous draw calls would not satisfy those requirements. No such
command-batch implementation or NPU throughput gain is qualified by this report.
