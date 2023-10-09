/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_XWAYLAND_XWAYLAND_H
#define WLR_XWAYLAND_XWAYLAND_H

#include <stdbool.h>
#include <wayland-server-core.h>
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/xcb_icccm.h>
#include <wlr/util/addon.h>

struct wlr_xwm;
struct wlr_data_source;
struct wlr_drag;

struct wlr_xwayland {
	struct wlr_xwayland_server *server;
	bool own_server;
	struct wlr_xwm *xwm;
	struct wlr_xwayland_shell_v1 *shell_v1;
	struct wlr_xwayland_cursor *cursor;

	const char *display_name;

	struct wl_display *wl_display;
	struct wlr_compositor *compositor;
	struct wlr_seat *seat;

	struct {
		struct wl_signal ready;
		struct wl_signal new_surface; // struct wlr_xwayland_surface
		struct wl_signal remove_startup_info; // struct wlr_xwayland_remove_startup_info_event
	} events;

	/**
	 * Add a custom event handler to xwayland. Return 1 if the event was
	 * handled or 0 to use the default wlr-xwayland handler. wlr-xwayland will
	 * free the event.
	 */
	int (*user_event_handler)(struct wlr_xwm *xwm, xcb_generic_event_t *event);

	struct wl_listener server_start;
	struct wl_listener server_ready;
	struct wl_listener server_destroy;
	struct wl_listener seat_destroy;
	struct wl_listener shell_destroy;

	void *data;
};

enum wlr_xwayland_surface_decorations {
	WLR_XWAYLAND_SURFACE_DECORATIONS_ALL = 0,
	WLR_XWAYLAND_SURFACE_DECORATIONS_NO_BORDER = 1,
	WLR_XWAYLAND_SURFACE_DECORATIONS_NO_TITLE = 2,
};

/**
 * This represents the input focus described as follows:
 *
 * https://www.x.org/releases/X11R7.6/doc/xorg-docs/specs/ICCCM/icccm.html#input_focus
 */
enum wlr_xwayland_icccm_input_model {
	WLR_ICCCM_INPUT_MODEL_NONE = 0,
	WLR_ICCCM_INPUT_MODEL_PASSIVE = 1,
	WLR_ICCCM_INPUT_MODEL_LOCAL = 2,
	WLR_ICCCM_INPUT_MODEL_GLOBAL = 3,
};

/**
 * An Xwayland user interface component. It has an absolute position in
 * layout-local coordinates.
 *
 * The inner struct wlr_surface is valid once the associate event is emitted.
 * Compositors can set up e.g. map and unmap listeners at this point. The
 * struct wlr_surface becomes invalid when the dissociate event is emitted.
 */
struct wlr_xwayland_surface {
	xcb_window_t window_id;
	struct wlr_xwm *xwm;
	uint32_t surface_id;
	uint64_t serial;

	struct wl_list link;
	struct wl_list stack_link;
	struct wl_list unpaired_link;

	struct wlr_surface *surface;
	struct wlr_addon surface_addon;
	struct wl_listener surface_commit;
	struct wl_listener surface_map;
	struct wl_listener surface_unmap;

	int16_t x, y;
	uint16_t width, height;
	uint16_t saved_width, saved_height;
	bool override_redirect;

	char *title;
	char *class;
	char *instance;
	char *role;
	char *startup_id;
	pid_t pid;
	bool has_utf8_title;

	struct wl_list children; // wlr_xwayland_surface.parent_link
	struct wlr_xwayland_surface *parent;
	struct wl_list parent_link; // wlr_xwayland_surface.children

	xcb_atom_t *window_type;
	size_t window_type_len;

	xcb_atom_t *protocols;
	size_t protocols_len;

	uint32_t decorations;
	xcb_icccm_wm_hints_t *hints;
	xcb_size_hints_t *size_hints;
	/*
	 * _NET_WM_STRUT_PARTIAL (used by e.g. XWayland panels;
	 * right/bottom are translated into root x/y coordinates)
	 */
	xcb_ewmh_wm_strut_partial_t *strut_partial;

	bool pinging;
	struct wl_event_source *ping_timer;

	// _NET_WM_STATE
	bool modal;
	bool fullscreen;
	bool maximized_vert, maximized_horz;
	bool minimized;
	bool withdrawn;

	bool has_alpha;

	struct {
		struct wl_signal destroy;
		struct wl_signal request_configure; // struct wlr_xwayland_surface_configure_event
		struct wl_signal request_move;
		struct wl_signal request_resize; // struct wlr_xwayland_resize_event
		struct wl_signal request_minimize; // struct wlr_xwayland_minimize_event
		struct wl_signal request_maximize;
		struct wl_signal request_fullscreen;
		struct wl_signal request_activate;

		struct wl_signal associate;
		struct wl_signal dissociate;

		struct wl_signal set_title;
		struct wl_signal set_class;
		struct wl_signal set_role;
		struct wl_signal set_parent;
		struct wl_signal set_startup_id;
		struct wl_signal set_window_type;
		struct wl_signal set_hints;
		struct wl_signal set_decorations;
		struct wl_signal set_strut_partial;
		struct wl_signal set_override_redirect;
		struct wl_signal set_geometry;
		struct wl_signal ping_timeout;
	} events;

	void *data;
};

struct wlr_xwayland_surface_configure_event {
	struct wlr_xwayland_surface *surface;
	int16_t x, y;
	uint16_t width, height;
	uint16_t mask; // xcb_config_window_t
};

struct wlr_xwayland_remove_startup_info_event  {
	const char *id;
	xcb_window_t window;
};

struct wlr_xwayland_resize_event {
	struct wlr_xwayland_surface *surface;
	uint32_t edges;
};

struct wlr_xwayland_minimize_event {
	struct wlr_xwayland_surface *surface;
	bool minimize;
};

/** Create an Xwayland server and XWM.
 *
 * The server supports a lazy mode in which Xwayland is only started when a
 * client tries to connect.
 */
struct wlr_xwayland *wlr_xwayland_create(struct wl_display *wl_display,
	struct wlr_compositor *compositor, bool lazy);

/**
 * Create an XWM from an existing Xwayland server.
 */
struct wlr_xwayland *wlr_xwayland_create_with_server(struct wl_display *display,
	struct wlr_compositor *compositor, struct wlr_xwayland_server *server);

void wlr_xwayland_destroy(struct wlr_xwayland *wlr_xwayland);

void wlr_xwayland_set_cursor(struct wlr_xwayland *wlr_xwayland,
	uint8_t *pixels, uint32_t stride, uint32_t width, uint32_t height,
	int32_t hotspot_x, int32_t hotspot_y);

void wlr_xwayland_surface_activate(struct wlr_xwayland_surface *surface,
	bool activated);

/**
 * Restack surface relative to sibling.
 * If sibling is NULL, then the surface is moved to the top or the bottom
 * of the stack (depending on the mode).
 */
void wlr_xwayland_surface_restack(struct wlr_xwayland_surface *surface,
	struct wlr_xwayland_surface *sibling, enum xcb_stack_mode_t mode);

void wlr_xwayland_surface_configure(struct wlr_xwayland_surface *surface,
	int16_t x, int16_t y, uint16_t width, uint16_t height);

void wlr_xwayland_surface_close(struct wlr_xwayland_surface *surface);

void wlr_xwayland_surface_set_withdrawn(struct wlr_xwayland_surface *surface,
	bool withdrawn);

void wlr_xwayland_surface_set_minimized(struct wlr_xwayland_surface *surface,
	bool minimized);

void wlr_xwayland_surface_set_maximized(struct wlr_xwayland_surface *surface,
	bool maximized);

void wlr_xwayland_surface_set_fullscreen(struct wlr_xwayland_surface *surface,
	bool fullscreen);

void wlr_xwayland_set_seat(struct wlr_xwayland *xwayland,
	struct wlr_seat *seat);

/**
 * Get a struct wlr_xwayland_surface from a struct wlr_surface.
 *
 * If the surface hasn't been created by Xwayland or has no X11 window
 * associated, NULL is returned.
 */
struct wlr_xwayland_surface *wlr_xwayland_surface_try_from_wlr_surface(
	struct wlr_surface *surface);

void wlr_xwayland_surface_ping(struct wlr_xwayland_surface *surface);

/** Metric to guess if an OR window should "receive" focus
 *
 * In the pure X setups, window managers usually straight up ignore override
 * redirect windows, and never touch them. (we have to handle them for mapping)
 *
 * When such a window wants to receive keyboard input (e.g. rofi/dzen) it will
 * use mechanics we don't support (sniffing/grabbing input).
 * [Sadly this is unrelated to xwayland-keyboard-grab]
 *
 * To still support these windows, while keeping general OR semantics as is, we
 * need to hand a subset of windows focus.
 * The dirty truth is, we need to hand focus to any Xwayland window, though
 * pretending this window has focus makes it easier to handle unmap.
 *
 * This function provides a handy metric based on the window type to guess if
 * the OR window wants focus.
 * It's probably not perfect, nor exactly intended but works in practice.
 *
 * Returns: true if the window should receive focus
 *          false if it should be ignored
 */
bool wlr_xwayland_or_surface_wants_focus(
	const struct wlr_xwayland_surface *xsurface);

enum wlr_xwayland_icccm_input_model wlr_xwayland_icccm_input_model(
	const struct wlr_xwayland_surface *xsurface);

#endif
