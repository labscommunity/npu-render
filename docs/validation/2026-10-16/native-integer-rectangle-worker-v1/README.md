# Integer rectangle integration in worker v15

Worker v15 passes its 106 selected requests and all 972 retained requests. Every
output matches worker v14. The explicit reference profile remains unchanged:
957 original capture hashes, 14 previously qualified profile hashes, and one
request without an original reference. No new numerical exception is added.

Only the rectangle model digest changes in the worker sources. The routing policy,
linked libraries, graph contracts and other kernels remain unchanged. The new
rectangle model is
`d651d6101dbccc5796873dc2016ec9e23d262cd955e9bc4bee6d56b86756eb2d`;
the worker binary is
`f055699f18c06d4ee437859741c453ba6bbfe5e17646b67646e52db84503dc9a`.

The full replay executes 37 rectangle draws: 28 direct kernels and nine retained
NPU crops, sharing one rectangle model import. It also executes 463 program,
126 geometry, ten complex-shader and six long-shader draws. All expected request
identities and 1,944 parent/child timing records correlate. One provider generation
completes without a provider failure.

The first coordinator stops before building because the preceding paired
comparison's diagnostic build failed. Its retry waits for the repaired comparison,
requires at least five percent less summed warm component time, then builds and
replays the worker. The measured component comparison and its failures are
preserved in the [integer rectangle archive](../native-integer-rectangle-v1/README.md).

This archive pins 69 source and evidence artifacts and excludes private pixels,
model blobs, binaries and caches. The replay does not demonstrate desktop
responsiveness, GPU relief or improved Tahoma performance. Registered graphics
and a live desktop trial remain separate gates.
