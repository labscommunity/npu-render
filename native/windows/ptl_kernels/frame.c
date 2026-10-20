/* Frame batch tile renderer for the twelve-row Panther Lake carrier.
 * The shared core is inlined by tools/build_ptl_kernel.py; every pixel,
 * vertex, sampling and blend operation executes here on the NPU. */
#include "frame_core.h"
NR_INLINE unsigned nr_param(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
void nr_probe_kernel(const unsigned char *params) {
    unsigned n;
    if (!params) return;
    for (n = 0; n < 2; ++n) {
        const unsigned char *p = params + n * 40;
        const unsigned *dims;
        if (!nr_param(p) || nr_param(p + 4) || nr_param(p + 8) != 1 || nr_param(p + 12) != 4 || !nr_param(p + 16) || !nr_param(p + 20) ||
            nr_param(p + 24) != 2 || nr_param(p + 28) != 0x4321 || nr_param(p + 32) || nr_param(p + 36) != 2) return;
        dims = (const unsigned *)nr_param(p + 16);
        if (dims[0] != 132096 || dims[1] != 1 || dims[2] != 1 || dims[3] != 1) return;
    }
    {
        const nr_u8 *data = (const nr_u8 *)nr_param(params);
        nr_u8 *output = (nr_u8 *)nr_param(params + 40);
        (void)nr_render_span(data, output);
    }
}
/* The target has no C runtime. Volatile stores prevent recursive lowering. */
void *memset(void *destination, int value, __SIZE_TYPE__ count) {
    volatile unsigned char *p = (volatile unsigned char *)destination;
    __SIZE_TYPE__ i;
    for (i = 0; i < count; ++i) p[i] = (unsigned char)value;
    return destination;
}
