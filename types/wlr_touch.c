#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server-core.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/types/wlr_touch.h>

#include "interfaces/wlr_input_device.h"

struct wlr_touch *wlr_touch_from_input_device(
		struct wlr_input_device *input_device) {
	assert(input_device->type == WLR_INPUT_DEVICE_TOUCH);
	return wl_container_of(input_device, (struct wlr_touch *)NULL, base);
}

void wlr_touch_init(struct wlr_touch *touch,
		const struct wlr_touch_impl *impl, const char *name) {
	memset(touch, 0, sizeof(*touch));
	wlr_input_device_init(&touch->base, WLR_INPUT_DEVICE_TOUCH, name);

	touch->impl = impl;
	wl_signal_init(&touch->events.down);
	wl_signal_init(&touch->events.up);
	wl_signal_init(&touch->events.motion);
	wl_signal_init(&touch->events.cancel);
	wl_signal_init(&touch->events.frame);
}

void wlr_touch_finish(struct wlr_touch *touch) {
	wlr_input_device_finish(&touch->base);

	free(touch->output_name);
}
