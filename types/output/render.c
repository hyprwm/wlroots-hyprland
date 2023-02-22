#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/interface.h>
#include <wlr/render/swapchain.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/backend.h"
#include "render/allocator/allocator.h"
#include "render/drm_format_set.h"
#include "render/wlr_renderer.h"
#include "render/pixel_format.h"
#include "types/wlr_output.h"

bool wlr_output_init_render(struct wlr_output *output,
		struct wlr_allocator *allocator, struct wlr_renderer *renderer) {
	assert(allocator != NULL && renderer != NULL);
	assert(output->back_buffer == NULL);

	uint32_t backend_caps = backend_get_buffer_caps(output->backend);
	uint32_t renderer_caps = renderer_get_render_buffer_caps(renderer);

	if (!(backend_caps & allocator->buffer_caps)) {
		wlr_log(WLR_ERROR, "output backend and allocator buffer capabilities "
			"don't match");
		return false;
	} else if (!(renderer_caps & allocator->buffer_caps)) {
		wlr_log(WLR_ERROR, "renderer and allocator buffer capabilities "
			"don't match");
		return false;
	}

	wlr_swapchain_destroy(output->swapchain);
	output->swapchain = NULL;

	wlr_swapchain_destroy(output->cursor_swapchain);
	output->cursor_swapchain = NULL;

	output->allocator = allocator;
	output->renderer = renderer;

	return true;
}

static bool output_attach_back_buffer(struct wlr_output *output,
		const struct wlr_output_state *state, int *buffer_age) {
	assert(output->back_buffer == NULL);

	// wlr_output_configure_primary_swapchain() function will call
	// wlr_output_test_state(), which can call us again. This is dangerous: we
	// risk infinite recursion. However, a buffer will always be supplied in
	// wlr_output_test_state(), which will prevent us from being called.
	if (!wlr_output_configure_primary_swapchain(output, state,
			&output->swapchain)) {
		return false;
	}

	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	struct wlr_buffer *buffer =
		wlr_swapchain_acquire(output->swapchain, buffer_age);
	if (buffer == NULL) {
		return false;
	}

	if (!renderer_bind_buffer(renderer, buffer)) {
		wlr_buffer_unlock(buffer);
		return false;
	}

	output->back_buffer = buffer;
	return true;
}

void output_clear_back_buffer(struct wlr_output *output) {
	if (output->back_buffer == NULL) {
		return;
	}

	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	renderer_bind_buffer(renderer, NULL);

	wlr_buffer_unlock(output->back_buffer);
	output->back_buffer = NULL;
}

bool wlr_output_attach_render(struct wlr_output *output, int *buffer_age) {
	return output_attach_back_buffer(output, &output->pending, buffer_age);
}

static bool output_attach_empty_back_buffer(struct wlr_output *output,
		const struct wlr_output_state *state) {
	assert(!(state->committed & WLR_OUTPUT_STATE_BUFFER));

	if (!output_attach_back_buffer(output, state, NULL)) {
		return false;
	}

	int width, height;
	output_pending_resolution(output, state, &width, &height);

	struct wlr_renderer *renderer = output->renderer;
	if (!wlr_renderer_begin(renderer, width, height)) {
		return false;
	}
	wlr_renderer_clear(renderer, (float[]){0, 0, 0, 0});
	wlr_renderer_end(renderer);

	return true;
}

// This function may attach a new, empty back buffer if necessary.
// If so, the new_back_buffer out parameter will be set to true.
bool output_ensure_buffer(struct wlr_output *output,
		const struct wlr_output_state *state,
		bool *new_back_buffer) {
	assert(*new_back_buffer == false);

	// If we already have a buffer, we don't need to allocate a new one
	if (state->committed & WLR_OUTPUT_STATE_BUFFER) {
		return true;
	}

	// If the compositor hasn't called wlr_output_init_render(), they will use
	// their own logic to attach buffers
	if (output->renderer == NULL) {
		return true;
	}

	bool enabled = output->enabled;
	if (state->committed & WLR_OUTPUT_STATE_ENABLED) {
		enabled = state->enabled;
	}

	// If we're lighting up an output or changing its mode, make sure to
	// provide a new buffer
	bool needs_new_buffer = false;
	if ((state->committed & WLR_OUTPUT_STATE_ENABLED) && state->enabled) {
		needs_new_buffer = true;
	}
	if (state->committed & WLR_OUTPUT_STATE_MODE) {
		needs_new_buffer = true;
	}
	if (state->committed & WLR_OUTPUT_STATE_RENDER_FORMAT) {
		needs_new_buffer = true;
	}
	if (state->allow_artifacts && output->commit_seq == 0 && enabled) {
		// On first commit, require a new buffer if the compositor called a
		// mode-setting function, even if the mode won't change. This makes it
		// so the swapchain is created now.
		needs_new_buffer = true;
	}
	if (!needs_new_buffer) {
		return true;
	}

	wlr_log(WLR_DEBUG, "Attaching empty buffer to output for modeset");
	if (!output_attach_empty_back_buffer(output, state)) {
		return false;
	}

	*new_back_buffer = true;
	return true;
}

void wlr_output_lock_attach_render(struct wlr_output *output, bool lock) {
	if (lock) {
		++output->attach_render_locks;
	} else {
		assert(output->attach_render_locks > 0);
		--output->attach_render_locks;
	}
	wlr_log(WLR_DEBUG, "%s direct scan-out on output '%s' (locks: %d)",
		lock ? "Disabling" : "Enabling", output->name,
		output->attach_render_locks);
}

struct wlr_drm_format *output_pick_format(struct wlr_output *output,
		const struct wlr_drm_format_set *display_formats,
		uint32_t fmt) {
	struct wlr_renderer *renderer = output->renderer;
	struct wlr_allocator *allocator = output->allocator;
	assert(renderer != NULL && allocator != NULL);

	const struct wlr_drm_format_set *render_formats =
		wlr_renderer_get_render_formats(renderer);
	if (render_formats == NULL) {
		wlr_log(WLR_ERROR, "Failed to get render formats");
		return NULL;
	}

	const struct wlr_drm_format *render_format =
		wlr_drm_format_set_get(render_formats, fmt);
	if (render_format == NULL) {
		wlr_log(WLR_DEBUG, "Renderer doesn't support format 0x%"PRIX32, fmt);
		return NULL;
	}

	struct wlr_drm_format *format = NULL;
	if (display_formats != NULL) {
		const struct wlr_drm_format *display_format =
			wlr_drm_format_set_get(display_formats, fmt);
		if (display_format == NULL) {
			wlr_log(WLR_DEBUG, "Output doesn't support format 0x%"PRIX32, fmt);
			return NULL;
		}
		format = wlr_drm_format_intersect(display_format, render_format);
	} else {
		// The output can display any format
		format = wlr_drm_format_dup(render_format);
	}

	if (format == NULL) {
		wlr_log(WLR_DEBUG, "Failed to intersect display and render "
			"modifiers for format 0x%"PRIX32 " on output %s",
			fmt, output->name);
		return NULL;
	}

	return format;
}

uint32_t wlr_output_preferred_read_format(struct wlr_output *output) {
	struct wlr_renderer *renderer = output->renderer;
	assert(renderer != NULL);

	if (!renderer->impl->preferred_read_format || !renderer->impl->read_pixels) {
		return DRM_FORMAT_INVALID;
	}

	if (!output_attach_back_buffer(output, &output->pending, NULL)) {
		return false;
	}

	uint32_t fmt = renderer->impl->preferred_read_format(renderer);

	output_clear_back_buffer(output);

	return fmt;
}

bool output_is_direct_scanout(struct wlr_output *output,
		struct wlr_buffer *buffer) {
	if (output->swapchain == NULL) {
		return true;
	}

	for (size_t i = 0; i < WLR_SWAPCHAIN_CAP; i++) {
		if (output->swapchain->slots[i].buffer == buffer) {
			return false;
		}
	}

	return true;
}
