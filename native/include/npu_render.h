#ifndef NPU_RENDER_H
#define NPU_RENDER_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
# ifdef NR_BUILD
#  define NR_API __declspec(dllexport)
# else
#  define NR_API __declspec(dllimport)
# endif
#else
# define NR_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nr_context nr_context;
typedef enum nr_operator { NR_SOURCE_OVER = 0, NR_SOURCE = 1 } nr_operator;

/* CPU-addressable, top-down premultiplied BGRA8. Byte-domain source-over
 * preserves the caller's blend color space; no color conversion is implied.
 * For BGRX8, alpha is read/written as 255. No imported device fences yet.
 */
typedef struct nr_surface {
    uint8_t *data;
    size_t length;
    size_t stride;
    uint32_t width;
    uint32_t height;
    int opaque;
} nr_surface;

typedef struct nr_region {
    uint32_t source_x, source_y;
    uint32_t destination_x, destination_y;
    uint32_t width, height;
} nr_region;

typedef struct nr_frame_stats {
    uint64_t pixels;
    uint64_t dispatches;
    double elapsed_ms;
    char execution_devices[128];
} nr_frame_stats;

/* tile_side fixes capacity for all requests; no compilation occurs per blend.
 * Creation and switching compile and qualify on the requested device only.
 * Errors are copied into the caller buffer; functions do not throw across C ABI.
 */
NR_API nr_context *nr_create(const char *backend, uint32_t tile_side,
                             char *error, size_t error_size);
NR_API void nr_destroy(nr_context *context);
NR_API int nr_switch(nr_context *context, const char *backend,
                     char *error, size_t error_size);
NR_API int nr_blend(nr_context *context, const nr_surface *source,
                    nr_surface *destination, const nr_region *region,
                    float opacity, nr_frame_stats *stats,
                    char *error, size_t error_size);
NR_API int nr_blend_regions(nr_context *context, const nr_surface *source,
                            nr_surface *destination, const nr_region *regions,
                            size_t region_count, float opacity, nr_frame_stats *stats,
                            char *error, size_t error_size);
NR_API int nr_compose_regions(nr_context *context, const nr_surface *source,
                              nr_surface *destination, const nr_region *regions,
                              size_t region_count, nr_operator operation, float opacity,
                              nr_frame_stats *stats, char *error, size_t error_size);

/* Source/destination storage must remain valid and stable during the call.
 * Infer requests serialize per prepared backend. The destination changes only
 * after all chunks succeed, allowing the integration to make an explicit
 * fallback decision on failure. Batch destinations must be disjoint, with at
 * most 4096 regions. Buffers may alias. Destroy requires quiescence.
 */
#ifdef __cplusplus
}
#endif
#endif
