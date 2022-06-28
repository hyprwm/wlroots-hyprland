/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_OUTPUT_LAYER_H
#define WLR_TYPES_WLR_OUTPUT_LAYER_H

#include <wlr/types/wlr_output.h>
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
 * struct wlr_output_state to describe their new state, and commit the output.
 *
 * Backends may have arbitrary limitations when it comes to displaying output
 * layers. Backends indicate whether or not a layer can be displayed via
 * wlr_output_layer_state.accepted after wlr_output_test() or
 * wlr_output_commit() is called. Compositors using the output layers API
 * directly are expected to setup layers, call wlr_output_test(), paint the
 * layers that the backend rejected with the renderer, then call
 * wlr_output_commit().
 */
struct wlr_output_layer {
	struct wl_list link; // wlr_output.layers
	struct wlr_addon_set addons;

	void *data;
};

/**
 * State for an output layer.
 */
struct wlr_output_layer_state {
	struct wlr_output_layer *layer;

	// Buffer to display, or NULL to disable the layer
	struct wlr_buffer *buffer;
	// Position in output-buffer-local coordinates
	int x, y;

	// Populated by the backend after wlr_output_test() and wlr_output_commit(),
	// indicates whether the backend has acknowledged and will take care of
	// displaying the layer
	bool accepted;
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
