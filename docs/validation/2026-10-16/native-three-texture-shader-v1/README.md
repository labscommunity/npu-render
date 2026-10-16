# Two-stage NPU texture addressing and pixel shading

The six texture lookups in the captured 118-instruction desktop shader have
coordinates independent of previous texture samples. A conservative dependency
analysis and scalar SSA lowering separate their coordinate calculations from
color calculations. The generated coordinate function uses 94 scalar operations;
the color function uses 226. The CPU-only lowering matches 192,000 raw components
across 12,000 independent interpreter fixtures with injected sample values.

On the NPU, the first stage emits 24 integer texel indices and 12 interpolation
fractions for each fragment. The host copies the texel bytes selected by those
indices. The second stage filters those bytes and executes the complete color
shader. Both stages run in the six qualified compiler-owned spans. No host
coordinate calculation, filtering or color-shader execution enters this path.

The pinned 13,136-byte kernel passes 96 independent scalar-reference fixtures and
12 malformed payloads on Delta. Fixtures cover zero/partial/full batches, texture
sizes, RGBA/BGRA layouts, clamp edges and changing inputs. All output bits and
padding match. The original carrier also passes before and after. Host fixture
creation and the independent scalar oracle are test code only. The 2.18–11.40 ms
requests are mixed component tests, not complete draw or desktop timings.

The inventory identifies nine recorded uses of this shader. Actual draw integration
is a separate gate: its first comparison ran faster but changed 15 byte components
by one level and is rejected pending diagnosis. This component pass does not imply
exact recorded-draw output, live desktop usability, GPU relief or Tahoma benefit.

Sources, dependency analysis, toolchain/build hashes and raw execution reports are
retained. Models, binaries and private image pixels are excluded.
