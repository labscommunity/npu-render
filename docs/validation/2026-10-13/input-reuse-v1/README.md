# Prepare immutable shader inputs once per draw

This isolated prototype copies a draw's constants and textures into owned NPU
input tensors once, then changes only fragment registers for subsequent tiles.
The complete 1,438-request desktop replay preserves every pixel and execution
receipt. Elapsed time is 638.532 seconds versus 641.906 seconds with the same
expanded-cache reference: **0.526% lower in one uncontrolled comparison**. This
does not establish a meaningful speedup. The API is not promoted to the renderer
or used in the subsequent desktop trial.

All 29 CPU suites pass. NPU and GPU each pass 6,912 independent values, including
caller mutation/release, new-draw refresh, integer bit patterns, invalid preparation
and failed-upload poisoning. NPU texture checks pass. The corrected loop test
produces all 77 expected NPU results, including captured shader and raster cases.

Two controller problems are retained. First, a PowerShell collection-scope error
prevents recording the second successful device test. Second, a later audit finds
the loop test accepts positional arguments; `--backend NPU` silently runs no cases
and returns zero. That empty output is **not test evidence**, despite the earlier
report's success field. The corrected `NPU all` invocation and explicit result
counts supersede that loop qualification. Original logs and reports remain intact.

[report.json](report.json) verifies the corrected qualification and replay. Stage
wrappers report approximately 122 seconds for shader/interpolation, 87 seconds for
coverage, and 71 seconds for merger operations. These include host tensor work
and synchronous inference/readback; they are not pure hardware times. The merger
label also contains existing specialized image operations. Model setup still
occurs 1,711 times, all reporting disk-cache hits. Physical DMA savings, live
desktop acceleration and a Tahoma benefit are not measured here.

[experiment.patch](experiment.patch) contains the isolated changes relative to
`queue-worker-v4`; raw hashes and normalized evidence accompany it.
