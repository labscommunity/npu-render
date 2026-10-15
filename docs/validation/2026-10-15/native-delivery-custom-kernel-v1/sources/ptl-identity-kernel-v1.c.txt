/* Bounded experimental entry for one validated 8-element FP16 invocation.
 * MemRefData offsets and void* entry come from Intel's common_types.h and
 * vpu_nnrt_shavert.h. The host validates every descriptor before admission.
 * No graph has been patched or executed by merely compiling this file.
 */
static __attribute__((always_inline)) inline unsigned load32(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) |
           ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

void nr_probe_kernel(const unsigned char *params) {
    if (!params) return;
    /* Three descriptors: eight input values, a scalar, eight output values. */
    for (unsigned record = 0; record < 3; ++record) {
        const unsigned char *p = params + record * 40;
        if (!load32(p) || load32(p + 4) || load32(p + 8) != 1 ||
            load32(p + 12) != 4 || load32(p + 24) != 2 ||
            load32(p + 28) != 0x4321 || load32(p + 32) || load32(p + 36) != 2)
            return;
    }
    const unsigned short *input = (const unsigned short *)load32(params);
    unsigned short *output = (unsigned short *)load32(params + 80);
    if ((unsigned)input == (unsigned)output) return;
    /* Host contract fixes all six invocations to exactly eight dense elements. */
    #pragma clang loop unroll(full)
    for (unsigned i = 0; i < 8; ++i) output[i] = input[i];
}
