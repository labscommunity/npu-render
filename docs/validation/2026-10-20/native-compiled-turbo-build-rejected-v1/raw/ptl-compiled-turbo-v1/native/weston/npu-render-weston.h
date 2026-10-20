#ifndef NPU_RENDER_WESTON_H
#define NPU_RENDER_WESTON_H

#include <pixman.h>
#include <stdbool.h>

struct nr_weston;
struct nr_weston *nr_weston_create(void);
void nr_weston_destroy(struct nr_weston *state);
bool nr_weston_repaint(struct nr_weston *state, pixman_op_t operation,
                       pixman_image_t *source, pixman_image_t *destination,
                       const pixman_transform_t *transform, pixman_filter_t filter,
                       pixman_region32_t *damage, const pixman_region32_t *source_clip, float opacity);
#endif
