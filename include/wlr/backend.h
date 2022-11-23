/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_BACKEND_H
#define WLR_BACKEND_H

#include <wayland-server-core.h>

struct wlr_session;
struct wlr_backend_impl;

/**
 * A backend provides a set of input and output devices.
 */
struct wlr_backend {
	const struct wlr_backend_impl *impl;

	struct {
		/** Raised when destroyed */
		struct wl_signal destroy;
		/** Raised when new inputs are added, passed the struct wlr_input_device */
		struct wl_signal new_input;
		/** Raised when new outputs are added, passed the struct wlr_output */
		struct wl_signal new_output;
	} events;
};

/**
 * Automatically initializes the most suitable backend given the environment.
 * Will always return a multi-backend. The backend is created but not started.
 * Returns NULL on failure.
 *
 * If session_ptr is not NULL, it's populated with the session which has been
 * created with the backend, if any.
 */
struct wlr_backend *wlr_backend_autocreate(struct wl_display *display,
	struct wlr_session **session_ptr);
/**
 * Start the backend. This may signal new_input or new_output immediately, but
 * may also wait until the display's event loop begins. Returns false on
 * failure.
 */
bool wlr_backend_start(struct wlr_backend *backend);
/**
 * Destroy the backend and clean up all of its resources. Normally called
 * automatically when the struct wl_display is destroyed.
 */
void wlr_backend_destroy(struct wlr_backend *backend);
/**
 * Returns the clock used by the backend for presentation feedback.
 */
clockid_t wlr_backend_get_presentation_clock(struct wlr_backend *backend);
/**
 * Returns the DRM node file descriptor used by the backend's underlying
 * platform. Can be used by consumers for additional rendering operations.
 * The consumer must not close the file descriptor since the backend continues
 * to have ownership of it.
 */
int wlr_backend_get_drm_fd(struct wlr_backend *backend);

#endif
