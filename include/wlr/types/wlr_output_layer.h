/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_LAYER_H
#define WLR_TYPES_WLR_OUTPUT_LAYER_H

#include <wlr/render/drm_format_set.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>
#include <wlr/util/addon.h>

/**
 * An output layer.
 *
 * Output layers are displayed between the output primary buffer (see
 * wlr_output_attach_buffer() and wlr_output_attach_render()) and the cursor
 * buffer. They can offload some rendering work to the backend.
 *
 * To configure output layers, callers should call wlr_output_layer_create() to
 * create layers, attach struct wlr_output_layer_state onto
 * struct wlr_output_state via wlr_output_set_layers() to describe their new
 * state, and commit the output via wlr_output_commit().
 *
 * Backends may have arbitrary limitations when it comes to displaying output
 * layers. Backends indicate whether or not a layer can be displayed via
 * wlr_output_layer_state.accepted after wlr_output_test() or
 * wlr_output_commit() is called. Compositors using the output layers API
 * directly are expected to setup layers, call wlr_output_test(), paint the
 * layers that the backend rejected with the renderer, then call
 * wlr_output_commit().
 *
 * Callers are responsible for disabling output layers when they need the full
 * output contents to be composited onto a single buffer, e.g. during screen
 * capture.
 *
 * Callers must always include the state for all layers on output test/commit.
 */
struct wlr_output_layer {
	struct wl_list link; // wlr_output.layers
	struct wlr_addon_set addons;

	struct {
		struct wl_signal feedback; // struct wlr_output_layer_feedback_event
	} events;

	void *data;

	// private state

	struct wlr_box dst_box;
};

/**
 * State for an output layer.
 */
struct wlr_output_layer_state {
	struct wlr_output_layer *layer;

	// Buffer to display, or NULL to disable the layer
	struct wlr_buffer *buffer;
	// Destination box in output-buffer-local coordinates
	struct wlr_box dst_box;

	// Populated by the backend after wlr_output_test() and wlr_output_commit(),
	// indicates whether the backend has acknowledged and will take care of
	// displaying the layer
	bool accepted;
};

/**
 * Feedback for an output layer.
 *
 * After an output commit, if the backend is not able to display a layer, it
 * can send feedback events. These events can be used to re-allocate the
 * layer's buffers so that they have a higher chance to get displayed.
 */
struct wlr_output_layer_feedback_event {
	dev_t target_device;
	const struct wlr_drm_format_set *formats;
};

/**
 * Create a new output layer.
 */
struct wlr_output_layer *wlr_output_layer_create(struct wlr_output *output);

/**
 * Destroy an output layer.
 */
void wlr_output_layer_destroy(struct wlr_output_layer *layer);

#endif
