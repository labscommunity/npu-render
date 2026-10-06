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
    char config[PATH_MAX];
    char last_config[256];
    pthread_t watcher;
    pthread_mutex_t stop_mutex;
    bool watching, stopping;
    uint64_t native_calls, native_pixels, fallback_calls, failed_calls;
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
        int result = read_selection(state->config, text);
        if (result == 1 && strcmp(text, state->last_config)) {
            memcpy(state->last_config, text, strlen(text) + 1);
            if (!parse_selection(text, backend)) {
                fprintf(stderr, "npu-render: rejected invalid selection; active backend retained\n");
            } else if (state->change(state->context, backend, error, sizeof(error))) {
                fprintf(stderr, "npu-render: switch to %s failed: %s\n", backend, error);
            } else {
                fprintf(stderr, "npu-render: activated %s for eligible compositor operations\n", backend);
            }
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
    if (!state->create || !state->destroy || !state->change || !state->compose) goto fail;
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
    } else {
        int result = read_selection(state->config, state->last_config);
        if (result < 0 || (result == 1 && !parse_selection(state->last_config, backend))) goto fail;
    }
    state->context = state->create(backend, 256, error, sizeof(error));
    if (!state->context) {
        fprintf(stderr, "npu-render: initial backend failed: %s\n", error);
        goto fail;
    }
    if (!override) {
        if (pthread_create(&state->watcher, NULL, watch_selection, state)) goto fail;
        state->watching = true;
    }
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
                    "\"pixman_fallback_calls\":%llu,\"native_failures\":%llu}\n",
            (unsigned long long)state->native_calls, (unsigned long long)state->native_pixels,
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

bool nr_weston_repaint(struct nr_weston *state, pixman_op_t operation,
                       pixman_image_t *source, pixman_image_t *destination,
                       const pixman_transform_t *transform, pixman_filter_t filter,
                       pixman_region32_t *damage, bool source_clipped, float opacity)
{
    if (!state) return false;
    nr_surface src, dst;
    nr_region *regions = NULL;
    const pixman_fixed_t one = pixman_int_to_fixed(1);
    if ((operation != PIXMAN_OP_OVER && operation != PIXMAN_OP_SRC) || source_clipped ||
        filter != PIXMAN_FILTER_NEAREST || !surface_view(source, &src) || !surface_view(destination, &dst))
        goto fallback;
    if (transform->matrix[0][0] != one || transform->matrix[1][1] != one ||
        transform->matrix[2][2] != one || transform->matrix[0][1] || transform->matrix[1][0] ||
        transform->matrix[2][0] || transform->matrix[2][1] ||
        transform->matrix[0][2] % one || transform->matrix[1][2] % one)
        goto fallback;
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
        if (outside && operation == PIXMAN_OP_SRC) goto fallback;
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
fallback:
    free(regions);
    ++state->fallback_calls;
    return false;
}
