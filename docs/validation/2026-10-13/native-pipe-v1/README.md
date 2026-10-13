# Local Windows NPU transport

A standalone Windows named-pipe worker completed all **1,438 recorded desktop
requests**, with exact pixels and rendering receipts and **1,438 delivery
acknowledgments**. It uses Delta's real NPU without the VM relay or request/reply
files. This qualifies a local transport boundary; it does not route the physical
Windows desktop or change the registered graphics driver.

The replay took **427.421 seconds**. Summed setup plus draw was 387.192 seconds,
including 268.499 seconds of drawing. The compiled-program cache reused 1,173
programs and made 557 backend compile/load calls. The preceding filesystem
replay took 530.356 seconds, but these are different worker executables and
harnesses in sequential, uncontrolled runs. Their difference is not a matched
estimate of transport savings or a Tahoma measurement.

The worker serves four local pipe connections, with one FIFO-serialized NPU
pipeline. Its protected access list permits the creating user and SYSTEM;
remote pipe clients are rejected. The server checks the connecting process ID
against the request, and the diagnostic client checks the server ID against its
owned worker. Rendering errors stop the worker and poison the compiled-program
cache; a departing client can release its connection without corrupting another
client's exchange. Every overlapped timeout drains cancellation before freeing
the operation state. Execution has finite time and request budgets.

Protocol qualification passed eight negative cases: missing arguments, invalid
pipe name, empty request budget, invalid duration, wrong server identity, spoofed
client identity, undersized declared packet and pending-read timeout/cancellation.
Two simultaneously open connections then received exact NPU results on distinct
serving threads. The full replay exercised all four serving threads. Rendering
and delivery are separate receipts; successful rendering alone does not establish
that a client received its reply. The replay acknowledges each reply only after
checking pixels and every rendering receipt against the saved reference.

The clean Windows build verifies all renderer sources are identical to the
qualified `memory-cache-v2` parent, which passed 29 CPU contracts and four NPU
test stages. Only the worker and its CMake target were added. The replay's
comparison helper passed 59 boundary cases. Original trace files were verified
against the retained source manifest; the wire uses the real client process ID,
while the canonical request digest preserves the established reference identity.

Thirteen downloaded artifacts were checked against remote SHA-256 and size
receipts and preserved losslessly under `raw/`. The source patch and executed
harnesses accompany them. Existing guest-only driver admission remains intact.
The DWM account boundary, process integrity levels, native adapter admission,
physical presentation, input routing and recovery still require qualification.
No physical display driver was installed and no Tahoma benchmark was run.
