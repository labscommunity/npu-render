# Complex-shader batch: rejected and interrupted attempts

Neither attempt here establishes a performance improvement. The original
failures and incomplete reports are preserved without rewriting their status.
The machine was using its normal desktop configuration during these off-screen
tests.

Worker v28 enlarges only the complex shader's transport batch, retaining the
existing kernel blobs and a conservative 4,096-pixel region scheduling budget.
Its build passes, but selected replay rejects original ticket 20 after eight
successful images. The protocol helper accepts capacities 4,096, 8,192 and
16,384; requesting 12,288 directly violates that contract. The error is
`invalid indexed NPU exchange packet`. No alternative rendering backend runs.
The dependent recovery controller stops before executing recovery tests.

Worker v29 requests the existing 16,384 protocol capacity and then bounds the
custom path to 12,288. Its binary SHA-256 is
`68ee41f06082135693d0d8df748244d7ee1970030450e803cfbbcdbf5ef018a4`.
The original-model health check passes eight exact NPU fixtures, the build passes,
and all 174 selected replay images pass. The full replay records only 55 completed
images before the machine restarts. Its report remains incomplete.

Windows System events identify two planned Windows servicing restarts:
`MoUsoCoreWorker.exe` requests the first at 10:59:09 UTC, with the event log
stopping at 11:00:23; `TrustedInstaller.exe` requests the second at 11:01:52.
The subsequent boot is 11:02:28 UTC. At inspection no experimental renderer
remains running. The separate recovery controller also stops during the first
shutdown, before any recovery execution. These records identify the interruption
as OS servicing; they are not evidence of an NPU crash.

A fresh, separately named qualification reruns the unchanged worker after the
update. Its one-off local scheduled task does not depend on the SSH connection.
The new comparison must measure both baseline and candidate after the update;
pre-update timing is not substituted for either side. Those results are outside
this archive.

`artifact-sha256.json` records 155 artifacts, with 1,709,872 remote source bytes.
Sources, builds, partial replay, original health check and Windows restart
evidence are included. Large text files use lossless gzip compression, with
both stored and uncompressed source hashes retained. Executables, models and
device caches are excluded.
