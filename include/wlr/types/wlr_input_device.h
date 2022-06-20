/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_INPUT_DEVICE_H
#define WLR_TYPES_WLR_INPUT_DEVICE_H

#include <wayland-server-core.h>

enum wlr_button_state {
	WLR_BUTTON_RELEASED,
	WLR_BUTTON_PRESSED,
};

enum wlr_input_device_type {
	WLR_INPUT_DEVICE_KEYBOARD,
	WLR_INPUT_DEVICE_POINTER,
	WLR_INPUT_DEVICE_TOUCH,
	WLR_INPUT_DEVICE_TABLET_TOOL,
	WLR_INPUT_DEVICE_TABLET_PAD,
	WLR_INPUT_DEVICE_SWITCH,
};

struct wlr_input_device {
	enum wlr_input_device_type type;
	unsigned int vendor, product;
	char *name;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

#endif
