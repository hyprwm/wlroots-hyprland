#ifndef TYPES_WLR_OUTPUT_H
#define TYPES_WLR_OUTPUT_H

#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_output.h>

void output_pending_resolution(struct wlr_output *output,
	const struct wlr_output_state *state, int *width, int *height);

struct wlr_drm_format *output_pick_format(struct wlr_output *output,
	const struct wlr_drm_format_set *display_formats, uint32_t format);
void output_clear_back_buffer(struct wlr_output *output);
bool output_ensure_buffer(struct wlr_output *output,
	const struct wlr_output_state *state, bool *new_back_buffer);

bool output_cursor_set_texture(struct wlr_output_cursor *cursor,
	struct wlr_texture *texture, bool own_texture, float scale,
	enum wl_output_transform transform, int32_t hotspot_x, int32_t hotspot_y);

#endif
