#ifndef TYPES_WLR_OUTPUT_H
#define TYPES_WLR_OUTPUT_H

#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_output.h>

void output_pending_resolution(struct wlr_output *output,
	const struct wlr_output_state *state, int *width, int *height);
bool output_is_direct_scanout(struct wlr_output *output,
	struct wlr_buffer *buffer);

struct wlr_drm_format *output_pick_format(struct wlr_output *output,
	const struct wlr_drm_format_set *display_formats, uint32_t format);
void output_clear_back_buffer(struct wlr_output *output);
bool output_ensure_buffer(struct wlr_output *output,
	const struct wlr_output_state *state, bool *new_back_buffer);

#endif
