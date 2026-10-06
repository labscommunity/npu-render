#define _POSIX_C_SOURCE 200809L
#include "npu-render-weston.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void require(bool condition, const char *message)
{
    if (!condition) { fprintf(stderr, "%s\n", message); exit(1); }
}

int main(int argc, char **argv)
{
    require(argc == 2, "provide the native shared library path");
    setenv("NPU_RENDER_NATIVE_LIBRARY", argv[1], 1);
    setenv("NPU_RENDER_BACKEND", "CPU", 1);
    struct nr_weston *state = nr_weston_create();
    require(state != NULL, "cannot create adapter");
    enum { W = 71, H = 39 };
    uint32_t source[W * H], target[W * H], expected[W * H], original[W * H];
    for (int i = 0; i < W * H; ++i) {
        uint32_t a = i % 256;
        source[i] = (a << 24) | ((i * 13 % (a + 1)) << 16) |
                    ((i * 7 % (a + 1)) << 8) | (i * 3 % (a + 1));
        original[i] = 0xff000000u | ((i * 3 % 256) << 16) | ((i * 9 % 256) << 8) | (i % 256);
    }
    pixman_image_t *src = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, source, W * 4);
    pixman_image_t *dst = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, target, W * 4);
    pixman_image_t *ref = pixman_image_create_bits(PIXMAN_a8r8g8b8, W, H, expected, W * 4);
    pixman_region32_t damage;
    pixman_region32_init_rect(&damage, 5, 4, 20, 11);
    pixman_region32_union_rect(&damage, &damage, 40, 22, 17, 10);
    pixman_transform_t transform;
    pixman_transform_init_identity(&transform);
    transform.matrix[0][2] = pixman_int_to_fixed(3);
    transform.matrix[1][2] = pixman_int_to_fixed(-2);
    pixman_image_set_transform(src, &transform);
    pixman_image_set_filter(src, PIXMAN_FILTER_NEAREST, NULL, 0);
    pixman_image_set_clip_region32(ref, &damage);
    int maximum_error = 0;
    for (int mode = 0; mode < 2; ++mode) {
        pixman_op_t op = mode ? PIXMAN_OP_SRC : PIXMAN_OP_OVER;
        memcpy(target, original, sizeof(target));
        memcpy(expected, original, sizeof(expected));
        require(nr_weston_repaint(state, op, src, dst, &transform, PIXMAN_FILTER_NEAREST,
                                  &damage, false, 1.0f), "eligible paint rejected");
        pixman_image_composite32(op, src, NULL, ref, 0, 0, 0, 0, 0, 0, W, H);
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                bool touched = pixman_region32_contains_point(&damage, x, y, NULL);
                for (int c = 0; c < 4; ++c) {
                    int difference = abs((int)((uint8_t *)target)[(y * W + x) * 4 + c] -
                                         (int)((uint8_t *)expected)[(y * W + x) * 4 + c]);
                    require(difference <= (touched ? 1 : 0), "adapter differs from Pixman oracle");
                    if (difference > maximum_error) maximum_error = difference;
                }
            }
        }
    }
    memcpy(target, original, sizeof(target));
    require(!nr_weston_repaint(state, PIXMAN_OP_OVER, src, dst, &transform, PIXMAN_FILTER_BILINEAR,
                               &damage, false, 1.0f), "unsupported sampling claimed offloaded");
    require(memcmp(target, original, sizeof(target)) == 0, "rejected paint modified pixels");
    pixman_region32_fini(&damage);
    pixman_image_unref(src);
    pixman_image_unref(dst);
    pixman_image_unref(ref);
    nr_weston_destroy(state);
    printf("{\"adapter_pixman_oracle\":true,\"max_byte_error\":%d}\n", maximum_error);
    return 0;
}
