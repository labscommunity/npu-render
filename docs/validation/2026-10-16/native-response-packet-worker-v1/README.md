# Validated response packet worker

Worker v19 passes the 106-request selected replay and all 972 retained requests.
Every output from workers v15 and v18 is preserved: 957 original captures,
fourteen existing precision-profile references and one request without a captured
image. No numerical exception is added. The full run uses one provider generation,
with no provider faults and 1,944 correlated parent/child timing records.

The isolation client validates the response envelope without copying all pixels.
The parent validates the child's receipt, restores the caller's process identity
in the response header, validates against that caller's request, and forwards the
packet. It no longer decodes and re-encodes a complete pixel vector at that
boundary. Rendering kernels, arithmetic, input-packet handling and NPU retry
policy remain unchanged. The binary is
`d4ddc1238bf91e94190b873f7f985def21c64c85f57fde18b6456f60c175f96c`.

The first coordinator stops on its unfinished CPU-component prerequisite, before
building. The second builds and passes the selected replay, then its PowerShell
report write fails under concurrent reads and leaves an empty status file. That
empty file is retained. The third coordinator verifies the identical selected
binary, resumes only the full replay, and uses the repository's atomic report
writer. It records its continuation in both its own report and the predecessor's
status so queued consumers can observe completion. No selected replay or captured
measurement is rewritten.

The manifest pins 76 source and evidence files. This qualifies recorded output
preservation. Fault recovery, paired timing, live responsiveness and GPU/Tahoma
benefit require separate evidence. Full desktop offload remains unfinished.
