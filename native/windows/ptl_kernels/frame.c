/* Frame batch tile renderer for the twelve-row Panther Lake carriers.
 * The shared core is inlined by tools/build_ptl_kernel.py; every pixel,
 * vertex, sampling and blend operation executes here on the NPU. */
#include "frame_core.h"
NR_INLINE unsigned nr_param(const unsigned char *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
/* A static dense FP16 plane descriptor of 132096 elements: the data or output plane. */
NR_INLINE int nr_plane(const unsigned char *p) {
    const unsigned *dims;
    if (!nr_param(p) || nr_param(p + 4) || nr_param(p + 8) != 1 || nr_param(p + 12) != 4 || !nr_param(p + 16) || !nr_param(p + 20) ||
        nr_param(p + 24) != 2 || nr_param(p + 28) != 0x4321 || nr_param(p + 32) || nr_param(p + 36) != 2) return 0;
    dims = (const unsigned *)nr_param(p + 16);
    return dims[0] == 132096 && dims[1] == 1 && dims[2] == 1 && dims[3] == 1;
}
void nr_probe_kernel(const unsigned char *params) {
    unsigned output_descriptor;
    if (!params || !nr_plane(params)) return;
    /* Softmax carrier: output at descriptor 1. ScatterElementsUpdate carrier: data, indices, updates, output. */
    output_descriptor = nr_plane(params + 40) ? 40u : 120u;
    if (output_descriptor == 120u && !nr_plane(params + 120)) return;
    {
        const nr_u8 *data = (const nr_u8 *)nr_param(params);
        nr_u8 *output = (nr_u8 *)nr_param(params + output_descriptor);
        (void)nr_render_span(data, output);
    }
}
/* The target has no C runtime. Volatile stores prevent recursive lowering. */
typedef unsigned nr_uint4 __attribute__((ext_vector_type(4)));
__attribute__((noinline)) void *memset(void *destination, int value, __SIZE_TYPE__ count) {
    volatile unsigned char *p = (volatile unsigned char *)destination;
    const unsigned char byte = (unsigned char)value;
    const nr_uint4 fill = (nr_uint4)((unsigned)byte * 0x01010101u);
    while (count && ((__UINTPTR_TYPE__)p & 15)) { *p++ = byte; --count; }
    while (count >= 16) { *(volatile nr_uint4 *)p = fill; p += 16; count -= 16; }
    while (count) { *p++ = byte; --count; }
    return destination;
}
