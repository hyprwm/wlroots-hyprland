#ifndef WLR_TYPES_WLR_GAMMA_CONTROL_V1_H
#define WLR_TYPES_WLR_GAMMA_CONTROL_V1_H

#include <wayland-server-core.h>

struct wlr_output;
struct wlr_output_state;

struct wlr_gamma_control_manager_v1 {
	struct wl_global *global;
	struct wl_list controls; // wlr_gamma_control_v1.link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
		struct wl_signal set_gamma; // struct wlr_gamma_control_manager_v1_set_gamma_event
	} events;

	void *data;
};

struct wlr_gamma_control_manager_v1_set_gamma_event {
	struct wlr_output *output;
	struct wlr_gamma_control_v1 *control; // may be NULL
};

struct wlr_gamma_control_v1 {
	struct wl_resource *resource;
	struct wlr_output *output;
	struct wlr_gamma_control_manager_v1 *manager;
	struct wl_list link;

	uint16_t *table;
	size_t ramp_size;

	struct wl_listener output_destroy_listener;

	void *data;
};

struct wlr_gamma_control_manager_v1 *wlr_gamma_control_manager_v1_create(
	struct wl_display *display);
struct wlr_gamma_control_v1 *wlr_gamma_control_manager_v1_get_control(
	struct wlr_gamma_control_manager_v1 *manager, struct wlr_output *output);
bool wlr_gamma_control_v1_apply(struct wlr_gamma_control_v1 *gamma_control,
	struct wlr_output_state *output_state);
void wlr_gamma_control_v1_send_failed_and_destroy(struct wlr_gamma_control_v1 *gamma_control);

#endif
