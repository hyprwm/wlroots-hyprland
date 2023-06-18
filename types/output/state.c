#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "types/wlr_output.h"

void wlr_output_state_finish(struct wlr_output_state *state) {
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		wlr_buffer_unlock(state->buffer);
		// struct wlr_buffer is ref'counted, so the pointer may remain valid after
		// wlr_buffer_unlock(). Reset the field to NULL to ensure nobody mistakenly
		// reads it after output_state_finish().
		state->buffer = NULL;
	}
	if (state->committed & WLR_OUTPUT_STATE_DAMAGE) {
		pixman_region32_fini(&state->damage);
	}
	if (state->committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		free(state->gamma_lut);
	}
}

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

void wlr_output_state_set_buffer(struct wlr_output_state *state,
		struct wlr_buffer *buffer) {
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		wlr_buffer_unlock(state->buffer);
	}

	state->committed |= WLR_OUTPUT_STATE_BUFFER;
	state->buffer = wlr_buffer_lock(buffer);
}

void wlr_output_state_set_damage(struct wlr_output_state *state,
		const pixman_region32_t *damage) {
	if (state->committed & WLR_OUTPUT_STATE_DAMAGE) {
		pixman_region32_fini(&state->damage);
	}

	state->committed |= WLR_OUTPUT_STATE_DAMAGE;

	pixman_region32_init(&state->damage);
	pixman_region32_copy(&state->damage, damage);
}

bool wlr_output_state_set_gamma_lut(struct wlr_output_state *state,
		size_t ramp_size, const uint16_t *r, const uint16_t *g, const uint16_t *b) {
	uint16_t *gamma_lut = NULL;
	if (ramp_size > 0) {
		if (state->committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
			gamma_lut = realloc(state->gamma_lut, 3 * ramp_size * sizeof(uint16_t));
		} else {
			gamma_lut = malloc(3 * ramp_size * sizeof(uint16_t));
		}
		if (gamma_lut == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		memcpy(gamma_lut, r, ramp_size * sizeof(uint16_t));
		memcpy(gamma_lut + ramp_size, g, ramp_size * sizeof(uint16_t));
		memcpy(gamma_lut + 2 * ramp_size, b, ramp_size * sizeof(uint16_t));
	} else {
		if (state->committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
			free(state->gamma_lut);
		}
	}

	state->committed |= WLR_OUTPUT_STATE_GAMMA_LUT;
	state->gamma_lut_size = ramp_size;
	state->gamma_lut = gamma_lut;
	return true;
}

void wlr_output_state_set_layers(struct wlr_output_state *state,
		struct wlr_output_layer_state *layers, size_t layers_len) {
	state->committed |= WLR_OUTPUT_STATE_LAYERS;
	state->layers = layers;
	state->layers_len = layers_len;
}
