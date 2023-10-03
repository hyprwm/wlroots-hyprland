#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <stdlib.h>
#include <wayland-server-core.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_tearing_control_v1.h>
#include <wlr/util/log.h>

#include "tearing-control-v1-protocol.h"

#define TEARING_CONTROL_MANAGER_VERSION 1

static const struct wp_tearing_control_manager_v1_interface tearing_impl;
static const struct wp_tearing_control_v1_interface tearing_control_impl;

static struct wlr_tearing_control_manager_v1 *tearing_manager_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wp_tearing_control_manager_v1_interface,
		&tearing_impl));
	return wl_resource_get_user_data(resource);
}

static struct wlr_tearing_control_v1 *tearing_surface_hint_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wp_tearing_control_v1_interface,
		&tearing_control_impl));
	return wl_resource_get_user_data(resource);
}

static void destroy_tearing_hint(struct wlr_tearing_control_v1 *hint) {
	if (hint == NULL) {
		return;
	}

	wl_signal_emit_mutable(&hint->events.destroy, NULL);

	wl_list_remove(&hint->link);
	wl_resource_set_user_data(hint->resource, NULL);

	wlr_addon_finish(&hint->addon);

	free(hint);
}

static void surface_addon_destroy(struct wlr_addon *addon) {
	struct wlr_tearing_control_v1 *hint = wl_container_of(addon, hint, addon);

	destroy_tearing_hint(hint);
}

static const struct wlr_addon_interface surface_addon_impl = {
	.name = "wp_tearing_control_v1",
	.destroy = surface_addon_destroy,
};

static void resource_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_tearing_resource_impl(struct wl_resource *resource) {
	struct wlr_tearing_control_v1 *hint = tearing_surface_hint_from_resource(resource);
	destroy_tearing_hint(hint);
}

static void tearing_control_handle_presentation_hint(struct wl_client *client,
		struct wl_resource *resource, uint32_t hint) {
	struct wlr_tearing_control_v1 *surface_hint =
		tearing_surface_hint_from_resource(resource);

	surface_hint->hint = hint;

	wl_signal_emit_mutable(&surface_hint->events.set_hint, NULL);
}

static const struct wp_tearing_control_v1_interface tearing_control_impl = {
	.destroy = resource_handle_destroy,
	.set_presentation_hint = tearing_control_handle_presentation_hint
};

static void tearing_control_manager_handle_get_tearing_control(
		struct wl_client *client, struct wl_resource *resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_tearing_control_manager_v1 *manager = tearing_manager_from_resource(resource);
	struct wlr_surface *surface = wlr_surface_from_resource(surface_resource);

	if (wlr_addon_find(&surface->addons, manager, &surface_addon_impl) != NULL) {
		wl_resource_post_error(resource,
			WP_TEARING_CONTROL_MANAGER_V1_ERROR_TEARING_CONTROL_EXISTS,
			"Tearing control object already exists!");
		return;
	}

	struct wlr_tearing_control_v1 *hint = calloc(1, sizeof(*hint));
	if (!hint) {
		wl_client_post_no_memory(client);
		return;
	}

	struct wl_resource *created_resource =
		wl_resource_create(client, &wp_tearing_control_v1_interface,
			wl_resource_get_version(resource), id);

	if (created_resource == NULL) {
		wl_resource_post_no_memory(resource);
		return;
	}

	wl_resource_set_implementation(created_resource, &tearing_control_impl,
		hint, destroy_tearing_resource_impl);

	hint->hint = WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC;
	hint->client = client;
	hint->resource = created_resource;
	hint->surface = surface;
	wlr_addon_init(&hint->addon, &hint->surface->addons, manager, &surface_addon_impl);

	wl_signal_init(&hint->events.set_hint);
	wl_signal_init(&hint->events.destroy);

	wl_list_insert(&manager->surface_hints, &hint->link);

	wl_signal_emit_mutable(&manager->events.new_object, hint);
}

static const struct wp_tearing_control_manager_v1_interface tearing_impl = {
	.destroy = resource_handle_destroy,
	.get_tearing_control = tearing_control_manager_handle_get_tearing_control,
};

static void tearing_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_tearing_control_manager_v1 *manager = data;

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&wp_tearing_control_manager_v1_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, &tearing_impl, manager, NULL);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tearing_control_manager_v1 *manager =
		wl_container_of(listener, manager, display_destroy);

	wl_signal_emit_mutable(&manager->events.destroy, NULL);

	struct wlr_tearing_control_v1 *hint, *tmp;
	wl_list_for_each_safe(hint, tmp, &manager->surface_hints, link) {
		destroy_tearing_hint(hint);
	}

	wl_list_remove(&manager->display_destroy.link);
	wl_global_destroy(manager->global);
	free(manager);
}

struct wlr_tearing_control_manager_v1 *wlr_tearing_control_manager_v1_create(
		struct wl_display *display, uint32_t version) {
	assert(version <= TEARING_CONTROL_MANAGER_VERSION);

	struct wlr_tearing_control_manager_v1 *manager = calloc(1, sizeof(*manager));
	if (!manager) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	wl_signal_init(&manager->events.new_object);
	wl_signal_init(&manager->events.destroy);

	wl_list_init(&manager->surface_hints);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	manager->global = wl_global_create(display, &wp_tearing_control_manager_v1_interface,
		version, manager, tearing_bind);

	if (manager->global == NULL) {
		wl_list_remove(&manager->display_destroy.link);
		free(manager);
		return NULL;
	}
	return manager;
}

enum wp_tearing_control_v1_presentation_hint
wlr_tearing_control_manager_v1_surface_hint_from_surface(struct wlr_tearing_control_manager_v1 *manager,
		struct wlr_surface *surface) {
	struct wlr_addon *addon =
		wlr_addon_find(&surface->addons, manager, &surface_addon_impl);
	if (addon == NULL) {
		return WP_TEARING_CONTROL_V1_PRESENTATION_HINT_VSYNC;
	}

	struct wlr_tearing_control_v1 *hint = wl_container_of(addon, hint, addon);

	return hint->hint;
}
