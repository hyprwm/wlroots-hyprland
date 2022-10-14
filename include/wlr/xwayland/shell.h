/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_XWAYLAND_SHELL_H
#define WLR_XWAYLAND_SHELL_H

#include <stdbool.h>
#include <wayland-server-core.h>

/**
 * The Xwayland shell.
 *
 * This is a shell only exposed to Xwayland.
 */
struct wlr_xwayland_shell_v1 {
	struct wl_global *global;

	struct {
		struct wl_signal new_surface; // struct wlr_xwayland_surface_v1
	} events;

	// private state

	struct wl_listener display_destroy;
};

/**
 * An Xwayland shell surface.
 */
struct wlr_xwayland_surface_v1 {
	struct wlr_surface *surface;
	uint64_t serial;

	// private state

	struct wl_resource *resource;
	struct wlr_xwayland_shell_v1 *shell;
	bool added;

	struct wl_listener surface_destroy;
};

/**
 * Create the xwayland_shell_v1 global.
 *
 * Compositors should add a global filter (see wl_display_set_global_filter())
 * to only expose this global to Xwayland clients.
 */
struct wlr_xwayland_shell_v1 *wlr_xwayland_shell_v1_create(
	struct wl_display *display, uint32_t version);

#endif
