#include "types/wlr_output.h"

void wlr_output_state_set_enabled(struct wlr_output_state *state,
		bool enabled) {
	state->committed |= WLR_OUTPUT_STATE_ENABLED;
	state->enabled = enabled;
	state->allow_artifacts = true;
}

void wlr_output_state_set_mode(struct wlr_output_state *state,
		struct wlr_output_mode *mode) {
	state->committed |= WLR_OUTPUT_STATE_MODE;
	state->mode_type = WLR_OUTPUT_STATE_MODE_FIXED;
	state->mode = mode;
	state->allow_artifacts = true;
}

void wlr_output_state_set_custom_mode(struct wlr_output_state *state,
		int32_t width, int32_t height, int32_t refresh) {
	state->committed |= WLR_OUTPUT_STATE_MODE;
	state->mode_type = WLR_OUTPUT_STATE_MODE_CUSTOM;
	state->custom_mode.width = width;
	state->custom_mode.height = height;
	state->custom_mode.refresh = refresh;
	state->allow_artifacts = true;
}

void wlr_output_state_set_scale(struct wlr_output_state *state, float scale) {
	state->committed |= WLR_OUTPUT_STATE_SCALE;
	state->scale = scale;
}

void wlr_output_state_set_transform(struct wlr_output_state *state,
		enum wl_output_transform transform) {
	state->committed |= WLR_OUTPUT_STATE_TRANSFORM;
	state->transform = transform;
}

void wlr_output_state_set_adaptive_sync_enabled(struct wlr_output_state *state,
		bool enabled) {
	state->committed |= WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED;
	state->adaptive_sync_enabled = enabled;
}

void wlr_output_state_set_render_format(struct wlr_output_state *state,
		uint32_t format) {
	state->committed |= WLR_OUTPUT_STATE_RENDER_FORMAT;
	state->render_format = format;
}

void wlr_output_state_set_subpixel(struct wlr_output_state *state,
		enum wl_output_subpixel subpixel) {
	state->committed |= WLR_OUTPUT_STATE_SUBPIXEL;
	state->subpixel = subpixel;
}
