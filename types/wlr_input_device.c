#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "util/signal.h"

void wlr_input_device_init(struct wlr_input_device *dev,
		enum wlr_input_device_type type, const char *name) {
	dev->type = type;
	dev->name = strdup(name);
	dev->vendor = 0;
	dev->product = 0;

	wl_signal_init(&dev->events.destroy);
}

void wlr_input_device_finish(struct wlr_input_device *wlr_device) {
	if (!wlr_device) {
		return;
	}

	wlr_signal_emit_safe(&wlr_device->events.destroy, wlr_device);

	free(wlr_device->name);
	free(wlr_device->output_name);
}

void wlr_input_device_destroy(struct wlr_input_device *dev) {
	if (!dev) {
		return;
	}

	if (dev->_device) {
		switch (dev->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			wlr_log(WLR_ERROR, "wlr_keyboard will not be destroyed");
			break;
		case WLR_INPUT_DEVICE_POINTER:
			wlr_log(WLR_ERROR, "wlr_pointer will not be destroyed");
			break;
		case WLR_INPUT_DEVICE_SWITCH:
			wlr_log(WLR_ERROR, "wlr_switch will not be destroyed");
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			wlr_touch_destroy(dev->touch);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			wlr_log(WLR_ERROR, "wlr_tablet_tool will not be destroyed");
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			wlr_log(WLR_ERROR, "wlr_tablet_pad will not be destroyed");
			break;
		}
	} else {
		wlr_input_device_finish(dev);
		free(dev);
	}
}
