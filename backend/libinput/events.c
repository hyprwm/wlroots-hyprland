#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/array.h"
#include "util/signal.h"

struct wlr_input_device *get_appropriate_device(
		enum wlr_input_device_type desired_type,
		struct libinput_device *libinput_dev) {
	struct wl_list *wlr_devices = libinput_device_get_user_data(libinput_dev);
	if (!wlr_devices) {
		return NULL;
	}
	struct wlr_libinput_input_device *dev;
	wl_list_for_each(dev, wlr_devices, link) {
		if (dev->wlr_input_device.type == desired_type) {
			return &dev->wlr_input_device;
		}
	}
	return NULL;
}


void destroy_libinput_input_device(struct wlr_libinput_input_device *dev)
{
	/**
	 * TODO remove the redundant wlr_input_device from wlr_libinput_input_device
	 * wlr_libinput_input_device::wlr_input_device is not owned by its input
	 * device type, which means we have 2 wlr_input_device to cleanup
	 */
	if (dev->wlr_input_device._device) {
		wlr_input_device_destroy(&dev->wlr_input_device);
		wlr_input_device_finish(&dev->wlr_input_device);
	} else {
		if (dev->keyboard.impl) {
			wlr_keyboard_destroy(&dev->keyboard);
		}
		if (dev->pointer.impl) {
			wlr_pointer_destroy(&dev->pointer);
		}
	}

	libinput_device_unref(dev->handle);
	wl_list_remove(&dev->link);
	free(dev);
}

static struct wlr_input_device *allocate_device(
		struct wlr_libinput_backend *backend,
		struct libinput_device *libinput_dev, struct wl_list *wlr_devices,
		enum wlr_input_device_type type) {
	const char *name = libinput_device_get_name(libinput_dev);
	struct wlr_libinput_input_device *dev =
		calloc(1, sizeof(struct wlr_libinput_input_device));
	if (dev == NULL) {
		return NULL;
	}
	struct wlr_input_device *wlr_dev = &dev->wlr_input_device;
	libinput_device_get_size(libinput_dev,
			&wlr_dev->width_mm, &wlr_dev->height_mm);
	const char *output_name = libinput_device_get_output_name(libinput_dev);
	if (output_name != NULL) {
		wlr_dev->output_name = strdup(output_name);
	}
	wl_list_insert(wlr_devices, &dev->link);
	dev->handle = libinput_dev;
	libinput_device_ref(libinput_dev);
	wlr_input_device_init(wlr_dev, type, name);
	wlr_dev->vendor = libinput_device_get_id_vendor(libinput_dev);
	wlr_dev->product = libinput_device_get_id_product(libinput_dev);
	return wlr_dev;
}

bool wlr_input_device_is_libinput(struct wlr_input_device *wlr_dev) {
	switch (wlr_dev->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		return wlr_dev->keyboard->impl == &libinput_keyboard_impl;
	case WLR_INPUT_DEVICE_POINTER:
		return wlr_dev->pointer->impl == &libinput_pointer_impl;
	case WLR_INPUT_DEVICE_TOUCH:
		return wlr_dev->touch->impl == &libinput_touch_impl;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		return wlr_dev->tablet->impl == &libinput_tablet_impl;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		return wlr_dev->tablet_pad->impl == &libinput_tablet_pad_impl;
	case WLR_INPUT_DEVICE_SWITCH:
		return wlr_dev->switch_device->impl == &libinput_switch_impl;
	default:
		return false;
	}
}

static void handle_device_added(struct wlr_libinput_backend *backend,
		struct libinput_device *libinput_dev) {
	/*
	 * Note: the wlr API exposes only devices with a single capability, because
	 * that meshes better with how Wayland does things and is a bit simpler.
	 * However, libinput devices often have multiple capabilities - in such
	 * cases we have to create several devices.
	 */
	int vendor = libinput_device_get_id_vendor(libinput_dev);
	int product = libinput_device_get_id_product(libinput_dev);
	const char *name = libinput_device_get_name(libinput_dev);
	wlr_log(WLR_DEBUG, "Added %s [%d:%d]", name, vendor, product);

	struct wlr_libinput_input_device *dev =
		calloc(1, sizeof(struct wlr_libinput_input_device));
	if (dev == NULL) {
		wlr_log_errno(WLR_ERROR, "failed to allocate wlr_libinput_input_device");
		return;
	}

	dev->handle = libinput_dev;
	libinput_device_ref(libinput_dev);
	libinput_device_set_user_data(libinput_dev, dev);

	bool dev_used = false;

	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
		init_device_keyboard(dev);

		wlr_signal_emit_safe(&backend->backend.events.new_input,
			&dev->keyboard.base);
		dev_used = true;
	}

	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_POINTER)) {
		init_device_pointer(dev);

		wlr_signal_emit_safe(&backend->backend.events.new_input,
			&dev->pointer.base);

		dev_used = true;
	}

	if (dev_used) {
		wl_list_insert(&backend->devices, &dev->link);
		return;
	} else {
		libinput_device_unref(libinput_dev);
		free(dev);
	}

	struct wl_list *wlr_devices = calloc(1, sizeof(struct wl_list));
	if (!wlr_devices) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return;
	}
	wl_list_init(wlr_devices);

	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TOUCH);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->touch = create_libinput_touch(libinput_dev);
		if (!wlr_dev->touch) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev,
			LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TABLET_TOOL);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->tablet = create_libinput_tablet(libinput_dev);
		if (!wlr_dev->tablet) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TABLET_PAD);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->tablet_pad = create_libinput_tablet_pad(libinput_dev);
		if (!wlr_dev->tablet_pad) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
		// TODO
	}
	if (libinput_device_has_capability(
			libinput_dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
			libinput_dev, wlr_devices, WLR_INPUT_DEVICE_SWITCH);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->switch_device = create_libinput_switch(libinput_dev);
		if (!wlr_dev->switch_device) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}

	if (!wl_list_empty(wlr_devices)) {
		struct wl_list **dst = wl_array_add(&backend->wlr_device_lists, sizeof(wlr_devices));
		if (!dst) {
			goto fail;
		}
		*dst = wlr_devices;

		libinput_device_set_user_data(libinput_dev, wlr_devices);
	} else {
		free(wlr_devices);
	}
	return;

fail:
	wlr_log(WLR_ERROR, "Could not allocate new device");
	struct wlr_libinput_input_device *device, *tmp;
	wl_list_for_each_safe(device, tmp, wlr_devices, link) {
		free(device);
	}
	free(wlr_devices);
}

static void handle_device_removed(struct wlr_libinput_backend *backend,
		struct libinput_device *libinput_dev) {
	int vendor = libinput_device_get_id_vendor(libinput_dev);
	int product = libinput_device_get_id_product(libinput_dev);
	const char *name = libinput_device_get_name(libinput_dev);
	wlr_log(WLR_DEBUG, "Removing %s [%d:%d]", name, vendor, product);

	// TODO: use libinput_device_get_user_data(libinput_dev);
	if (!wl_list_empty(&backend->devices)) {
		struct wlr_libinput_input_device *dev, *tmp_dev;
		wl_list_for_each_safe(dev, tmp_dev, &backend->devices, link) {
			if (dev->handle == libinput_dev) {
				destroy_libinput_input_device(dev);
				return;
			}
		}
	}

	struct wl_list *wlr_devices = libinput_device_get_user_data(libinput_dev);
	if (!wlr_devices) {
		return;
	}
	struct wlr_libinput_input_device *dev, *tmp_dev;
	wl_list_for_each_safe(dev, tmp_dev, wlr_devices, link) {
		destroy_libinput_input_device(dev);
	}
	size_t i = 0;
	struct wl_list **ptr;
	wl_array_for_each(ptr, &backend->wlr_device_lists) {
		struct wl_list *iter = *ptr;
		if (iter == wlr_devices) {
			array_remove_at(&backend->wlr_device_lists,
				i * sizeof(struct wl_list *), sizeof(struct wl_list *));
			break;
		}
		i++;
	}
	free(wlr_devices);
}

void handle_libinput_event(struct wlr_libinput_backend *backend,
		struct libinput_event *event) {
	struct libinput_device *libinput_dev = libinput_event_get_device(event);
	struct wlr_libinput_input_device *dev =
		libinput_device_get_user_data(libinput_dev);
	enum libinput_event_type event_type = libinput_event_get_type(event);
	switch (event_type) {
	case LIBINPUT_EVENT_DEVICE_ADDED:
		handle_device_added(backend, libinput_dev);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		handle_device_removed(backend, libinput_dev);
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		handle_keyboard_key(event, &dev->keyboard);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		handle_pointer_motion(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		handle_pointer_motion_abs(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		handle_pointer_button(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		handle_pointer_axis(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		handle_touch_down(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		handle_touch_up(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		handle_touch_motion(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		handle_touch_cancel(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		handle_touch_frame(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		handle_tablet_tool_axis(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		handle_tablet_tool_proximity(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		handle_tablet_tool_tip(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		handle_tablet_tool_button(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		handle_tablet_pad_button(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		handle_tablet_pad_ring(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		handle_tablet_pad_strip(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_SWITCH_TOGGLE:
		handle_switch_toggle(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_BEGIN:
		handle_pointer_swipe_begin(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_UPDATE:
		handle_pointer_swipe_update(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_SWIPE_END:
		handle_pointer_swipe_end(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_BEGIN:
		handle_pointer_pinch_begin(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_UPDATE:
		handle_pointer_pinch_update(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_PINCH_END:
		handle_pointer_pinch_end(event, &dev->pointer);
		break;
#if LIBINPUT_HAS_HOLD_GESTURES
	case LIBINPUT_EVENT_GESTURE_HOLD_BEGIN:
		handle_pointer_hold_begin(event, &dev->pointer);
		break;
	case LIBINPUT_EVENT_GESTURE_HOLD_END:
		handle_pointer_hold_end(event, &dev->pointer);
		break;
#endif
	default:
		wlr_log(WLR_DEBUG, "Unknown libinput event %d", event_type);
		break;
	}
}
