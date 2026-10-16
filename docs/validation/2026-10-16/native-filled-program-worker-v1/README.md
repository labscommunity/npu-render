# Filled program batches

Worker v18 passes 106 selected requests and all 972 retained requests. Every
preceding worker v15 output is preserved: 957 original capture matches, fourteen
previously qualified profile matches and one request without a captured image.
The full replay completes with one provider generation and no faults or retries.
No new numerical exception is introduced.

The compact program transport now submits only the filled portion of a batch.
It no longer fills spare capacity with synthetic negative coordinates, and it
removes an unused per-sample copy of three projected vertices. The compact NPU
kernel, pixel arithmetic and address scheduler policy are unchanged. The worker
binary is `d75e2fb82ab038f02054931c9cc2e862c08546e2dfe363a23dd9997394b239a6`.

Across the full replay, recorded provider requests fall from 19,431 with worker
v15 to 17,208 with worker v18. This is a request count, not a matched timing result.
The replay covers 463 generic-program draws, 126 geometry draws, ten complex
draws, six long-shader draws and 37 rectangles, retaining the policy of 28 direct
rectangle kernels and nine NPU crops.

The manifest pins 66 source and evidence files. Private pixel captures, model
blobs, caches and compiled binaries remain outside the archive. Complete-image
comparisons, live desktop responsiveness, GPU relief and Tahoma performance
require separate qualification. Full desktop offload remains unfinished.
