#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "types/wlr_output.h"

void wlr_output_state_init(struct wlr_output_state *state) {
	*state = (struct wlr_output_state){0};
	pixman_region32_init(&state->damage);
}

void wlr_output_state_finish(struct wlr_output_state *state) {
	wlr_buffer_unlock(state->buffer);
	// struct wlr_buffer is ref'counted, so the pointer may remain valid after
	// wlr_buffer_unlock(). Reset the field to NULL to ensure nobody mistakenly
	// reads it after output_state_finish().
	state->buffer = NULL;
	pixman_region32_fini(&state->damage);
	free(state->gamma_lut);
}

void wlr_output_state_set_enabled(struct wlr_output_state *state,
		bool enabled) {
	state->committed |= WLR_OUTPUT_STATE_ENABLED;
	state->enabled = enabled;
	state->allow_reconfiguration = true;
}

void wlr_output_state_set_mode(struct wlr_output_state *state,
		struct wlr_output_mode *mode) {
	state->committed |= WLR_OUTPUT_STATE_MODE;
	state->mode_type = WLR_OUTPUT_STATE_MODE_FIXED;
	state->mode = mode;
	state->allow_reconfiguration = true;
}

void wlr_output_state_set_custom_mode(struct wlr_output_state *state,
		int32_t width, int32_t height, int32_t refresh) {
	state->committed |= WLR_OUTPUT_STATE_MODE;
	state->mode_type = WLR_OUTPUT_STATE_MODE_CUSTOM;
	state->custom_mode.width = width;
	state->custom_mode.height = height;
	state->custom_mode.refresh = refresh;
	state->allow_reconfiguration = true;
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
	state->committed |= WLR_OUTPUT_STATE_BUFFER;
	wlr_buffer_unlock(state->buffer);
	state->buffer = wlr_buffer_lock(buffer);
}

void wlr_output_state_set_damage(struct wlr_output_state *state,
		const pixman_region32_t *damage) {
	state->committed |= WLR_OUTPUT_STATE_DAMAGE;
	pixman_region32_copy(&state->damage, damage);
}

bool wlr_output_state_set_gamma_lut(struct wlr_output_state *state,
		size_t ramp_size, const uint16_t *r, const uint16_t *g, const uint16_t *b) {
	uint16_t *gamma_lut = NULL;
	if (ramp_size > 0) {
		gamma_lut = realloc(state->gamma_lut, 3 * ramp_size * sizeof(uint16_t));
		if (gamma_lut == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		memcpy(gamma_lut, r, ramp_size * sizeof(uint16_t));
		memcpy(gamma_lut + ramp_size, g, ramp_size * sizeof(uint16_t));
		memcpy(gamma_lut + 2 * ramp_size, b, ramp_size * sizeof(uint16_t));
	} else {
		free(state->gamma_lut);
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

bool wlr_output_state_copy(struct wlr_output_state *dst,
		const struct wlr_output_state *src) {
	struct wlr_output_state copy = *src;
	copy.committed &= ~(WLR_OUTPUT_STATE_BUFFER |
		WLR_OUTPUT_STATE_DAMAGE |
		WLR_OUTPUT_STATE_GAMMA_LUT);
	copy.buffer = NULL;
	pixman_region32_init(&copy.damage);
	copy.gamma_lut = NULL;
	copy.gamma_lut_size = 0;

	if (src->committed & WLR_OUTPUT_STATE_BUFFER) {
		wlr_output_state_set_buffer(&copy, src->buffer);
	}

	if (src->committed & WLR_OUTPUT_STATE_DAMAGE) {
		wlr_output_state_set_damage(&copy, &src->damage);
	}

	if (src->committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		const uint16_t *r = src->gamma_lut;
		const uint16_t *g = src->gamma_lut + src->gamma_lut_size;
		const uint16_t *b = src->gamma_lut + 2 * src->gamma_lut_size;
		if (!wlr_output_state_set_gamma_lut(&copy, src->gamma_lut_size, r, g, b)) {
			goto err;
		}
	}

	wlr_output_state_finish(dst);
	*dst = copy;
	return true;

err:
	wlr_output_state_finish(&copy);
	return false;
}
