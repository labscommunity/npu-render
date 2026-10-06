/* Restricted Weston/Pixman adapter. Native source-over and source-copy perform
 * actual pixel work. Unsupported paths stay on Pixman and are counted explicitly.
 */
#define _POSIX_C_SOURCE 200809L
#include "npu-render-weston.h"
#include "npu_render.h"

#include <ctype.h>
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct nr_weston {
    void *library;
    nr_context *context;
    nr_context *(*create)(const char *, uint32_t, char *, size_t);
    void (*destroy)(nr_context *);
    int (*change)(nr_context *, const char *, char *, size_t);
    int (*compose)(nr_context *, const nr_surface *, nr_surface *, const nr_region *,
                   size_t, nr_operator, float, nr_frame_stats *, char *, size_t);
    int (*prepare_sampling)(nr_context *, uint32_t, uint32_t, char *, size_t);
    int (*sample)(nr_context *, const nr_surface *, nr_surface *, const nr_damage_rect *,
                  size_t, const nr_sample_point *, size_t, nr_operator, float,
                  nr_frame_stats *, char *, size_t);
    char config[PATH_MAX];
    char last_config[256];
    pthread_t watcher;
    pthread_mutex_t stop_mutex;
    bool watching, stopping, pinned;
    struct { uint32_t width, height; int status; } sampling[16];
    size_t sampling_count;
    uint64_t native_calls, native_pixels, sampling_calls, fallback_calls, failed_calls;
};

/* Accept the two canonical key orders produced by version-1 selection files.
 * No substring extraction: malformed/extra keys never become a device choice.
 */
static bool parse_selection(const char *text, char backend[4])
{
    char compact[128], expected[128];
    size_t n = 0;
    bool quoted = false;
    for (; *text; ++text) {
        if (*text == '\\') return false;
        if (*text == '"') quoted = !quoted;
        if (!quoted && isspace((unsigned char)*text)) continue;
        if (n + 1 >= sizeof(compact)) return false;
        compact[n++] = *text;
    }
    compact[n] = 0;
    const char *choices[] = {"CPU", "GPU", "NPU"};
    for (size_t i = 0; i < 3; ++i) {
        snprintf(expected, sizeof(expected), "{\"version\":1,\"backend\":\"%s\"}", choices[i]);
        bool match = strcmp(compact, expected) == 0;
        snprintf(expected, sizeof(expected), "{\"backend\":\"%s\",\"version\":1}", choices[i]);
        if (match || strcmp(compact, expected) == 0) {
            memcpy(backend, choices[i], 4);
            return true;
        }
    }
    return false;
}

static int read_selection(const char *path, char text[256])
{
    FILE *stream = fopen(path, "rb");
    if (!stream) return errno == ENOENT ? 0 : -1;
    size_t count = fread(text, 1, 255, stream);
    bool valid = !ferror(stream) && feof(stream);
    fclose(stream);
    text[count] = 0;
    if (!valid || memchr(text, 0, count)) return -1;
    return 1;
}

static void *watch_selection(void *argument)
{
    struct nr_weston *state = argument;
    const struct timespec interval = {0, 250000000};
    while (true) {
        pthread_mutex_lock(&state->stop_mutex);
        bool stopping = state->stopping;
        pthread_mutex_unlock(&state->stop_mutex);
        if (stopping) break;
        char text[256], backend[4], error[2048];
        int result = state->pinned ? 0 : read_selection(state->config, text);
        if (result == 1 && strcmp(text, state->last_config)) {
            memcpy(state->last_config, text, strlen(text) + 1);
            if (!parse_selection(text, backend)) {
                fprintf(stderr, "npu-render: rejected invalid selection; active backend retained\n");
            } else if (state->change(state->context, backend, error, sizeof(error))) {
                fprintf(stderr, "npu-render: switch to %s failed: %s\n", backend, error);
            } else {
                pthread_mutex_lock(&state->stop_mutex);
                for (size_t i = 0; i < state->sampling_count; ++i)
                    if (state->sampling[i].status < 0) state->sampling[i].status = 0;
                pthread_mutex_unlock(&state->stop_mutex);
                fprintf(stderr, "npu-render: activated %s for eligible compositor operations\n", backend);
            }
        }
        /* A newly attached surface can keep using explicit Pixman fallback
         * while its source extent is compiled on this control thread. */
        for (size_t i = 0; i < 16; ++i) {
            pthread_mutex_lock(&state->stop_mutex);
            bool pending = i < state->sampling_count && state->sampling[i].status == 0;
            uint32_t width = pending ? state->sampling[i].width : 0;
            uint32_t height = pending ? state->sampling[i].height : 0;
            pthread_mutex_unlock(&state->stop_mutex);
            if (!pending) continue;
            int prepared = state->prepare_sampling(state->context, width, height, error, sizeof(error));
            pthread_mutex_lock(&state->stop_mutex);
            state->sampling[i].status = prepared ? -1 : 1;
            pthread_mutex_unlock(&state->stop_mutex);
            if (prepared) fprintf(stderr, "npu-render: sampling extent %ux%u rejected: %s\n", width, height, error);
            else fprintf(stderr, "npu-render: sampling extent %ux%u prepared\n", width, height);
        }
        nanosleep(&interval, NULL);
    }
    return NULL;
}

struct nr_weston *nr_weston_create(void)
{
    const char *library = getenv("NPU_RENDER_NATIVE_LIBRARY");
    if (!library) return NULL;
    struct nr_weston *state = calloc(1, sizeof(*state));
    if (!state) return NULL;
    pthread_mutex_init(&state->stop_mutex, NULL);
    state->library = dlopen(library, RTLD_NOW | RTLD_LOCAL);
    if (!state->library) {
        fprintf(stderr, "npu-render: cannot load native library: %s\n", dlerror());
        goto fail;
    }
    *(void **)(&state->create) = dlsym(state->library, "nr_create");
    *(void **)(&state->destroy) = dlsym(state->library, "nr_destroy");
    *(void **)(&state->change) = dlsym(state->library, "nr_switch");
    *(void **)(&state->compose) = dlsym(state->library, "nr_compose_regions");
    *(void **)(&state->prepare_sampling) = dlsym(state->library, "nr_prepare_sampling");
    *(void **)(&state->sample) = dlsym(state->library, "nr_sample_regions");
    if (!state->create || !state->destroy || !state->change || !state->compose ||
        !state->prepare_sampling || !state->sample) goto fail;
    char backend[4] = "GPU", error[2048];
    const char *override = getenv("NPU_RENDER_BACKEND");
    const char *config = getenv("NPU_RENDER_CONFIG");
    if (config) {
        if (snprintf(state->config, sizeof(state->config), "%s", config) >= (int)sizeof(state->config))
            goto fail;
    } else {
        const char *base = getenv("XDG_CONFIG_HOME");
        const char *home = getenv("HOME");
        int count;
        if (base) count = snprintf(state->config, sizeof(state->config), "%s/npu-render/config.json", base);
        else if (home) count = snprintf(state->config, sizeof(state->config), "%s/.config/npu-render/config.json", home);
        else goto fail;
        if (count >= (int)sizeof(state->config)) goto fail;
    }
    if (override) {
        if (strcmp(override, "CPU") && strcmp(override, "GPU") && strcmp(override, "NPU")) goto fail;
        memcpy(backend, override, 4);
        state->pinned = true;
    } else {
        int result = read_selection(state->config, state->last_config);
        if (result < 0 || (result == 1 && !parse_selection(state->last_config, backend))) goto fail;
    }
    state->context = state->create(backend, 256, error, sizeof(error));
    if (!state->context) {
        fprintf(stderr, "npu-render: initial backend failed: %s\n", error);
        goto fail;
    }
    if (pthread_create(&state->watcher, NULL, watch_selection, state)) goto fail;
    state->watching = true;
    fprintf(stderr, "npu-render: prepared %s; unsupported operations explicitly remain on Pixman\n", backend);
    return state;
fail:
    nr_weston_destroy(state);
    return NULL;
}

void nr_weston_destroy(struct nr_weston *state)
{
    if (!state) return;
    pthread_mutex_lock(&state->stop_mutex);
    state->stopping = true;
    pthread_mutex_unlock(&state->stop_mutex);
    if (state->watching) pthread_join(state->watcher, NULL);
    fprintf(stderr, "npu-render-summary: {\"native_calls\":%llu,\"native_pixels\":%llu,"
                    "\"sampling_calls\":%llu,\"pixman_fallback_calls\":%llu,\"native_failures\":%llu}\n",
            (unsigned long long)state->native_calls, (unsigned long long)state->native_pixels,
            (unsigned long long)state->sampling_calls,
            (unsigned long long)state->fallback_calls, (unsigned long long)state->failed_calls);
    if (state->context) state->destroy(state->context);
    if (state->library) dlclose(state->library);
    pthread_mutex_destroy(&state->stop_mutex);
    free(state);
}

static bool surface_view(pixman_image_t *image, nr_surface *surface)
{
    pixman_format_code_t format = pixman_image_get_format(image);
    int width = pixman_image_get_width(image), height = pixman_image_get_height(image);
    int stride = pixman_image_get_stride(image);
    uint32_t endian = 1;
    if (*(uint8_t *)&endian != 1 ||
        (format != PIXMAN_a8r8g8b8 && format != PIXMAN_x8r8g8b8) ||
        width < 1 || height < 1 || stride < 1 || !pixman_image_get_data(image)) return false;
    surface->data = (uint8_t *)pixman_image_get_data(image);
    surface->width = width;
    surface->height = height;
    surface->stride = stride;
    surface->length = (size_t)stride * height;
    surface->opaque = format == PIXMAN_x8r8g8b8;
    return true;
}

static bool sampling_ready(struct nr_weston *state, uint32_t width, uint32_t height)
{
    bool ready = false;
    pthread_mutex_lock(&state->stop_mutex);
    size_t i;
    for (i = 0; i < state->sampling_count; ++i)
        if (state->sampling[i].width == width && state->sampling[i].height == height) break;
    if (i < state->sampling_count) ready = state->sampling[i].status == 1;
    else if (i < 16 && width <= 4096 && height <= 4096) {
        state->sampling[i].width = width;
        state->sampling[i].height = height;
        state->sampling[i].status = 0;
        ++state->sampling_count;
    }
    pthread_mutex_unlock(&state->stop_mutex);
    return ready;
}

static int64_t fixed_floor(int64_t value)
{
    return value >= 0 ? value / 65536 : -((-value + 65535) / 65536);
}

static bool repaint_sampled(struct nr_weston *state, pixman_op_t operation,
                            const nr_surface *src, nr_surface *dst,
                            const pixman_transform_t *transform, pixman_filter_t filter,
                            pixman_region32_t *damage, const pixman_region32_t *source_clip,
                            float opacity)
{
    const pixman_fixed_t one = pixman_int_to_fixed(1);
    if (transform->matrix[2][0] || transform->matrix[2][1] || transform->matrix[2][2] != one ||
        dst->width > 16384 || dst->height > 16384) return false;
    pixman_box32_t clip = {0, 0, (int32_t)src->width, (int32_t)src->height};
    if (source_clip) {
        int count;
        pixman_box32_t *boxes = pixman_region32_rectangles(source_clip, &count);
        if (count != 1 || operation != PIXMAN_OP_OVER) return false;
        clip = boxes[0];
        if (clip.x1 < 0 || clip.y1 < 0 || clip.x2 > (int32_t)src->width ||
            clip.y2 > (int32_t)src->height || clip.x1 >= clip.x2 || clip.y1 >= clip.y2) return false;
    }
    if (!sampling_ready(state, src->width, src->height)) return false;
    int count;
    pixman_box32_t *boxes = pixman_region32_rectangles(damage, &count);
    if (count < 0 || count > 4096) return false;
    if (!count) return true;
    nr_damage_rect *regions = calloc((size_t)count, sizeof(*regions));
    nr_sample_point *points = NULL;
    if (!regions) return false;
    size_t region_count = 0, total = 0;
    bool success = false;
    for (int i = 0; i < count; ++i) {
        int64_t x1 = boxes[i].x1 < 0 ? 0 : boxes[i].x1;
        int64_t y1 = boxes[i].y1 < 0 ? 0 : boxes[i].y1;
        int64_t x2 = boxes[i].x2 > (int64_t)dst->width ? dst->width : boxes[i].x2;
        int64_t y2 = boxes[i].y2 > (int64_t)dst->height ? dst->height : boxes[i].y2;
        if (x2 <= x1 || y2 <= y1) continue;
        size_t pixels = (size_t)(x2-x1)*(y2-y1);
        if (pixels > SIZE_MAX/sizeof(*points)-total) goto done;
        total += pixels;
        regions[region_count++] = (nr_damage_rect){x1,y1,x2-x1,y2-y1};
    }
    if (!total) { success = true; goto done; }
    points = calloc(total, sizeof(*points));
    if (!points) goto done;
    size_t index = 0;
    for (size_t r = 0; r < region_count; ++r) {
        const nr_damage_rect *box = &regions[r];
        for (uint32_t y = box->y; y < box->y+box->height; ++y) {
            for (uint32_t x = box->x; x < box->x+box->width; ++x, ++index) {
                pixman_vector_t position = {{pixman_int_to_fixed(x)+one/2,
                                             pixman_int_to_fixed(y)+one/2, one}};
                if (!pixman_transform_point(transform, &position)) goto done;
                const int64_t sx = position.vector[0], sy = position.vector[1];
                int64_t x0, y0;
                float weights[4] = {1,0,0,0};
                if (filter == PIXMAN_FILTER_NEAREST) {
                    x0 = fixed_floor(sx-1); y0 = fixed_floor(sy-1);
                } else {
                    const int64_t xx = sx-one/2, yy = sy-one/2;
                    x0 = fixed_floor(xx); y0 = fixed_floor(yy);
                    /* Match Pixman's 7-bit bilinear fractional coordinates.
                     * Coordinate generation stays on CPU, not mislabeled NPU. */
                    unsigned wx = (unsigned)((xx-x0*one)/512), wy = (unsigned)((yy-y0*one)/512);
                    weights[0] = (128-wx)*(128-wy)/16384.f;
                    weights[1] = wx*(128-wy)/16384.f;
                    weights[2] = (128-wx)*wy/16384.f;
                    weights[3] = wx*wy/16384.f;
                }
                for (unsigned k = 0; k < 4; ++k) {
                    int64_t tx = x0 + (filter == PIXMAN_FILTER_BILINEAR ? (k&1) : 0);
                    int64_t ty = y0 + (filter == PIXMAN_FILTER_BILINEAR ? (k>>1) : 0);
                    if (!source_clip && filter == PIXMAN_FILTER_BILINEAR) {
                        if (tx < 0) tx = 0;
                        if (ty < 0) ty = 0;
                        if (tx >= src->width) tx = src->width-1;
                        if (ty >= src->height) ty = src->height-1;
                    }
                    if (tx >= clip.x1 && tx < clip.x2 && ty >= clip.y1 && ty < clip.y2) {
                        points[index].index[k] = (uint32_t)(ty*src->width+tx);
                        points[index].weight[k] = weights[k];
                    }
                }
            }
        }
    }
    char error[2048];
    nr_frame_stats stats;
    if (state->sample(state->context, src, dst, regions, region_count, points, total,
                      operation == PIXMAN_OP_SRC ? NR_SOURCE : NR_SOURCE_OVER,
                      opacity, &stats, error, sizeof(error))) {
        ++state->failed_calls;
        fprintf(stderr, "npu-render: sampled operation failed; explicit Pixman retry: %s\n", error);
        goto done;
    }
    ++state->native_calls; ++state->sampling_calls;
    state->native_pixels += stats.pixels;
    if (state->sampling_calls == 1 || state->sampling_calls%300 == 0)
        fprintf(stderr, "npu-render-sampling: {\"execution_devices\":\"%s\",\"pixels\":%llu,"
                        "\"dispatches\":%llu,\"elapsed_ms\":%.3f}\n", stats.execution_devices,
                (unsigned long long)stats.pixels, (unsigned long long)stats.dispatches, stats.elapsed_ms);
    success = true;
done:
    free(points); free(regions);
    return success;
}

bool nr_weston_repaint(struct nr_weston *state, pixman_op_t operation,
                       pixman_image_t *source, pixman_image_t *destination,
                       const pixman_transform_t *transform, pixman_filter_t filter,
                       pixman_region32_t *damage, const pixman_region32_t *source_clip, float opacity)
{
    if (!state) return false;
    nr_surface src, dst;
    nr_region *regions = NULL;
    const pixman_fixed_t one = pixman_int_to_fixed(1);
    if ((operation != PIXMAN_OP_OVER && operation != PIXMAN_OP_SRC) ||
        (filter != PIXMAN_FILTER_NEAREST && filter != PIXMAN_FILTER_BILINEAR) || !surface_view(source, &src) || !surface_view(destination, &dst))
        goto fallback;
    if (source_clip || filter != PIXMAN_FILTER_NEAREST || transform->matrix[0][0] != one || transform->matrix[1][1] != one ||
        transform->matrix[2][2] != one || transform->matrix[0][1] || transform->matrix[1][0] ||
        transform->matrix[2][0] || transform->matrix[2][1] ||
        transform->matrix[0][2] % one || transform->matrix[1][2] % one)
        goto sampling;
    int translate_x = transform->matrix[0][2] / one;
    int translate_y = transform->matrix[1][2] / one;
    int box_count;
    pixman_box32_t *boxes = pixman_region32_rectangles(damage, &box_count);
    if (box_count > 4096) goto fallback;
    if (box_count == 0) return true;
    regions = calloc((size_t)box_count, sizeof(*regions));
    if (!regions) goto fallback;
    size_t count = 0;
    for (int i = 0; i < box_count; ++i) {
        int64_t x1 = boxes[i].x1 < 0 ? 0 : boxes[i].x1;
        int64_t y1 = boxes[i].y1 < 0 ? 0 : boxes[i].y1;
        int64_t x2 = boxes[i].x2 > (int64_t)dst.width ? dst.width : boxes[i].x2;
        int64_t y2 = boxes[i].y2 > (int64_t)dst.height ? dst.height : boxes[i].y2;
        if (x2 <= x1 || y2 <= y1) continue;
        /* OVER outside the source is transparent; SRC would clear the target,
         * so retain Pixman for that case until clear regions have a contract. */
        bool outside = x1 + translate_x < 0 || y1 + translate_y < 0 ||
                       x2 + translate_x > src.width || y2 + translate_y > src.height;
        if (outside && operation == PIXMAN_OP_SRC) goto sampling;
        if (x1 + translate_x < 0) x1 = -translate_x;
        if (y1 + translate_y < 0) y1 = -translate_y;
        if (x2 + translate_x > src.width) x2 = (int64_t)src.width - translate_x;
        if (y2 + translate_y > src.height) y2 = (int64_t)src.height - translate_y;
        if (x2 <= x1 || y2 <= y1) continue;
        regions[count++] = (nr_region){x1 + translate_x, y1 + translate_y, x1, y1, x2 - x1, y2 - y1};
    }
    if (count) {
        char error[2048];
        nr_frame_stats stats;
        if (state->compose(state->context, &src, &dst, regions, count,
                           operation == PIXMAN_OP_SRC ? NR_SOURCE : NR_SOURCE_OVER,
                           opacity, &stats, error, sizeof(error))) {
            ++state->failed_calls;
            fprintf(stderr, "npu-render: native operation failed; explicit Pixman retry: %s\n", error);
            goto fallback;
        }
        ++state->native_calls;
        state->native_pixels += stats.pixels;
        if (state->native_calls == 1 || state->native_calls % 300 == 0)
            fprintf(stderr, "npu-render-frame: {\"execution_devices\":\"%s\",\"pixels\":%llu,"
                            "\"dispatches\":%llu,\"elapsed_ms\":%.3f}\n", stats.execution_devices,
                    (unsigned long long)stats.pixels, (unsigned long long)stats.dispatches, stats.elapsed_ms);
    }
    free(regions);
    return true;
sampling:
    free(regions); regions = NULL;
    if (repaint_sampled(state, operation, &src, &dst, transform, filter, damage, source_clip, opacity)) return true;
fallback:
    free(regions);
    ++state->fallback_calls;
    return false;
}
