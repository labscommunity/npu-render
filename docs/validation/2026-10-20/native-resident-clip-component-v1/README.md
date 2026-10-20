# Combined clipping kernel: device component qualification

On Delta, the third kernel source passes **144 exact full-span fixtures on the
Panther Lake NPU**. The same process also passes eight original-model requests
before and eight afterward. Execution devices are explicitly NPU. The code is
11,184 bytes, within the existing 18,256-byte allocation; the carrier metadata
and allocation are preserved.

The component combines triangle coverage, interpolation, point/linear sampling,
the exact two-texture clipping shader, blending and output packing in one request
per batch. Complete small textures travel with the batch. It admits one pinned
shader and bounded resources; this is not a general GPU instruction translator.
Host-side local tests also pass 144 fixtures, but those are separate CPU evidence.

| Identity | SHA-256 |
| --- | --- |
| Kernel source v3 | `24bbffae0015ead8546076464193a4459fdd2c49afc8690503d4eec40dbffb65` |
| Linked ELF | `2b2a2ebd9dcdd1a267092d5752b2501bb8605876bad9f19edb5437d75631f1f0` |
| Qualified patched carrier | `a5c39dbb825fdd4bd334d8bcbef966e9404132d9b642375665a0892adc48c45c` |

The retained attempts explain three failures before device execution:

- V1 failed linking because the target has no `memset` implementation.
- V2 supplied that helper but placed it before the required kernel entry.
- V3 linked correctly, but its first coordinator read the v1 source manifest.
  The corrected coordinator verifies the v3 manifest and existing ELF identity,
  then runs device fixtures without rebuilding or relaxing either check.

The successful evidence is
[the device report](raw/ptl-six-span-resident-clip-v3/report.json) and
[the corrected coordinator](raw/ptl-resident-clip-v3-qualification-v2/report.json).
The artifact manifest pins all 67 retained files, including failed attempts and
local qualification sources. Binary models and toolchains are not committed.

Worker integration, full recorded-request correctness, matched timing, recovery
and desktop qualification are separate gates. This component result establishes
neither desktop responsiveness nor GPU relief nor a Tahoma improvement.
