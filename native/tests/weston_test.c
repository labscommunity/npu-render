#define _POSIX_C_SOURCE 200809L
#include "npu-render-weston.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
                                  &damage, NULL, 1.0f), "eligible paint rejected");
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
    /* Fractional translation, scaling, mirror and rotation, with and without
     * a single rectangular source clip. Pixman is the independent renderer. */
    const double affine[][6] = {
        {1,0,0.25,0,1,-0.1875}, {0.5,0,1.125,0,0.75,-0.375},
        {1.25,0,-10.375,0,1.5,0.625}, {0,1,0,-1,0,W-1},
        {0.91,0.27,-3.35,-0.27,0.91,2.55}, {-1,0,W,0,1,0},
        /* Exact axis scales exercise the single-dispatch resize specialization. */
        {0.5,0,1,0,0.5,2}, {0.25,0,0,0,0.5,0}, {1,0,-2,0,1,-3},
    };
    int sampled_cases = 0;
    for (size_t variant = 0; variant < sizeof(affine)/sizeof(affine[0]); ++variant)
    for (int filtering = 0; filtering < 2; ++filtering)
    for (int clipped = 0; clipped < 2; ++clipped)
    for (int opacity_case = 0; opacity_case < 2; ++opacity_case) {
        const float opacity = opacity_case ? 0.5f : 1.f;
        pixman_transform_init_identity(&transform);
        for (int row = 0; row < 2; ++row) for (int column = 0; column < 3; ++column)
            transform.matrix[row][column] = pixman_double_to_fixed(affine[variant][row*3+column]);
        pixman_filter_t filter = filtering ? PIXMAN_FILTER_BILINEAR : PIXMAN_FILTER_NEAREST;
        pixman_region32_t clip;
        pixman_region32_init_rect(&clip, 3, 2, W-7, H-5);
        memcpy(target, original, sizeof(target)); memcpy(expected, original, sizeof(expected));
        bool handled = false;
        for (unsigned attempt = 0; attempt < 300 && !handled; ++attempt) {
            handled = nr_weston_repaint(state, PIXMAN_OP_OVER, src, dst, &transform,
                                        filter, &damage, clipped ? &clip : NULL, opacity);
            if (!handled) {
                require(memcmp(target, original, sizeof(target)) == 0, "warming sampler wrote pixels");
                const struct timespec delay = {0, 10000000}; nanosleep(&delay, NULL);
            }
        }
        require(handled, "affine sampler did not become ready");
        pixman_image_t *input = src;
        pixman_transform_t adjusted = transform;
        if (clipped) {
            input = pixman_image_create_bits(PIXMAN_a8r8g8b8, W-7, H-5, source+2*W+3, W*4);
            pixman_transform_translate(&adjusted, NULL, pixman_int_to_fixed(-3), pixman_int_to_fixed(-2));
        }
        pixman_image_set_transform(input, &adjusted);
        pixman_image_set_filter(input, filter, NULL, 0);
        pixman_image_set_repeat(input, filtering && !clipped ? PIXMAN_REPEAT_PAD : PIXMAN_REPEAT_NONE);
        pixman_color_t color = {0,0,0,(uint16_t)(65535*opacity)};
        pixman_image_t *mask = opacity < 1 ? pixman_image_create_solid_fill(&color) : NULL;
        pixman_image_composite32(PIXMAN_OP_OVER,input,mask,ref,0,0,0,0,0,0,W,H);
        for (int y = 0; y < H; ++y) for (int x = 0; x < W; ++x) {
            bool touched = pixman_region32_contains_point(&damage,x,y,NULL);
            for (int c = 0; c < 4; ++c) {
                const int index = (y*W+x)*4+c;
                int difference = abs((int)((uint8_t *)target)[index]-(int)((uint8_t *)expected)[index]);
                if (difference > (touched ? 2 : 0)) {
                    fprintf(stderr,"variant=%zu bilinear=%d clip=%d opacity=%f x=%d y=%d channel=%d actual=%d expected=%d\n",
                            variant,filtering,clipped,opacity,x,y,c,((uint8_t *)target)[index],((uint8_t *)expected)[index]);
                    require(false,"affine sampler differs from Pixman oracle");
                }
                if (difference > maximum_error) maximum_error = difference;
            }
        }
        if (mask) pixman_image_unref(mask);
        if (clipped) pixman_image_unref(input);
        pixman_region32_fini(&clip); ++sampled_cases;
    }
    memcpy(target, original, sizeof(target));
    require(!nr_weston_repaint(state, PIXMAN_OP_OVER, src, dst, &transform, PIXMAN_FILTER_CONVOLUTION,
                               &damage, NULL, 1.0f), "unsupported sampling claimed offloaded");
    require(memcmp(target, original, sizeof(target)) == 0, "rejected paint modified pixels");
    pixman_region32_fini(&damage);
    pixman_image_unref(src);
    pixman_image_unref(dst);
    pixman_image_unref(ref);
    nr_weston_destroy(state);
    printf("{\"adapter_pixman_oracle\":true,\"sampled_cases\":%d,\"max_byte_error\":%d}\n", sampled_cases, maximum_error);
    return 0;
}
