# Long shader in the real worker and registered graphics path

Worker v9 passes 60 selected requests and all 972 retained requests through the
real serialized parent/child worker connection. An independent audit confirms
all 972 output hashes and lengths match qualified worker v8. This includes 962
original references, nine previously qualified coefficient corrections and one
request without an original successful reference. No new precision exception is
introduced. The provider has one generation and no failures.

The new path admits all six expected long-shader requests, correlates their exact
packet sequence numbers, and shares one model import. The full replay also admits
376 reusable-program draws, 223 shared-geometry draws, 24 four-sample draws, five
solid draws and nine three-texture draws. All 1,944 parent/child timing records
are monotonic and correlate by request identity. The offline CPU shader oracle
is absent from the worker's source and build inputs.

The exact request that stopped live v43 is then replayed separately through this
worker. It completes in a 274.090 ms client round trip and produces hash
`61da2570eecf77357d3ed52b454df91c3b46b734613a192f777407a2bdb54b43`,
matching both separately qualified complete-image references. This time includes
worker transport and setup; it is not a pure NPU kernel latency or a desktop
response measurement. The ordinary desktop is unchanged during replay.

Registered D3D11 qualification v34 passes six clears and two draws with eight
NPU operation receipts, eight delivery acknowledgments, eight child receipts and
no CPU/GPU rendering executions. The existing manually leased fixture explicitly
selects the registered adapter. Driver removal, ordinary display restoration and
memory-integrity checks pass. It does not establish automatic interactive
application admission or default-adapter selection.

Worker executable SHA-256:
`14c978f8e97c41814969fc3285b83b2f74d9df55cbcae2e088847cefcdea485c`.
Long-shader model SHA-256:
`895831be2695bc151454833249996608f38d35880c635900bb731515bc2bcda1`.
The source manifest pins all 27 worker files; the rendering libraries remain
those from qualified native-routing-v45. The long-shader provider serializes
access to its shared request and permanently rejects further use after a failure.

The 96 archived source/evidence artifacts have recorded byte lengths and SHA-256
values. Private pixel packets, model blobs and binaries are not committed. A new
live desktop trial is a separate gate. This archive proves worker consistency
and the registered fixture, not responsive sustained desktop use, full application
coverage, reduced physical GPU load or improved Tahoma performance.
