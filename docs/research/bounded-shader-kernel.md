# Reusable bounded shader kernel

The retained capture contains 24 pixel programs. Fifteen programs of one to six
instructions account for 609 draws. The latest worker replay attributes its two
largest remaining shader groups to single-texture multiply programs: 74 draws
with 3,026 provider calls and 36 draws with 1,592 calls. These are attribution
within a diagnostic replay, not matched performance results.

The next candidate should generalize the qualified two-phase masked kernel's
contract instead of selecting another set of exact shader hashes. The initial
instruction subset is MOV, MUL, DIV and SAMPLE, with explicit input/output masks,
swizzles, scalar broadcasts and immediate constants. DOT4, more arithmetic,
derivatives and loops require subsequent semantic and performance checks. Programs
outside the accepted subset retain the qualified NPU graph path.

Phase zero should retain NPU coverage and interpolation, accepting three varying
registers with validated interpolation modes. A validated address plan admits at
most two samples whose coordinates come directly from a varying register or its
XY/Z quotient. All addressing arithmetic and point/linear clamp behavior remain
on the NPU. The host only transports the resulting addressed texel bytes.

Phase one should filter those texels, execute a bounded instruction stream and
merge the output. A proposed layout allows eight instructions, four temporary
vectors, three varying vectors and one output vector. Instruction order and
per-channel write masks must be preserved. It must reject unsupported register
uses, invalid indices, uninitialized reads, malformed programs and nonfinite
results explicitly. No host shader evaluation belongs in the runtime path.

One possible six-span carrier layout places uniform instructions/constants below
byte 2,048, keeps 96-byte vertex records for up to 2,046 vertices, and moves task
records to byte 200,704. With 112-byte plan/shading records, a span admits at most
2,048 samples, or 12,288 across six spans. Every range must be checked against
the 262,144-byte span. These are proposed limits, not an implemented or qualified
ABI; compiler output must also fit the retained code region.

Acceptance requires independent per-operation and malformed-program fixtures,
original/custom/original hardware checks, structural admission rejection tests,
every eligible captured draw with changed images, the complete worker replay,
and live interaction/recovery. Use the same shared, serialized model policy as
the qualified geometry component. The shader inventory and arithmetic design
do not establish NPU output, speed, desktop coverage or Tahoma benefit.
