#include <assert.h>
#include <stdlib.h>
#include "types/wlr_xdg_shell.h"

static const struct xdg_positioner_interface xdg_positioner_implementation;

struct wlr_xdg_positioner *wlr_xdg_positioner_from_resource(
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &xdg_positioner_interface,
		&xdg_positioner_implementation));
	return wl_resource_get_user_data(resource);
}

static void xdg_positioner_handle_set_size(struct wl_client *client,
		struct wl_resource *resource, int32_t width, int32_t height) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	if (width < 1 || height < 1) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"width and height must be positive and non-zero");
		return;
	}

	positioner->rules.size.width = width;
	positioner->rules.size.height = height;
}

static void xdg_positioner_handle_set_anchor_rect(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y, int32_t width,
		int32_t height) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	if (width < 0 || height < 0) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"width and height must be positive");
		return;
	}

	positioner->rules.anchor_rect.x = x;
	positioner->rules.anchor_rect.y = y;
	positioner->rules.anchor_rect.width = width;
	positioner->rules.anchor_rect.height = height;
}

static void xdg_positioner_handle_set_anchor(struct wl_client *client,
		struct wl_resource *resource, uint32_t anchor) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	if (anchor > XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"invalid anchor value");
		return;
	}

	positioner->rules.anchor = anchor;
}

static void xdg_positioner_handle_set_gravity(struct wl_client *client,
		struct wl_resource *resource, uint32_t gravity) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	if (gravity > XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
		wl_resource_post_error(resource,
			XDG_POSITIONER_ERROR_INVALID_INPUT,
			"invalid gravity value");
		return;
	}

	positioner->rules.gravity = gravity;
}

static void xdg_positioner_handle_set_constraint_adjustment(
		struct wl_client *client, struct wl_resource *resource,
		uint32_t constraint_adjustment) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	positioner->rules.constraint_adjustment = constraint_adjustment;
}

static void xdg_positioner_handle_set_offset(struct wl_client *client,
		struct wl_resource *resource, int32_t x, int32_t y) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);

	positioner->rules.offset.x = x;
	positioner->rules.offset.y = y;
}

static void xdg_positioner_handle_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct xdg_positioner_interface
		xdg_positioner_implementation = {
	.destroy = xdg_positioner_handle_destroy,
	.set_size = xdg_positioner_handle_set_size,
	.set_anchor_rect = xdg_positioner_handle_set_anchor_rect,
	.set_anchor = xdg_positioner_handle_set_anchor,
	.set_gravity = xdg_positioner_handle_set_gravity,
	.set_constraint_adjustment =
		xdg_positioner_handle_set_constraint_adjustment,
	.set_offset = xdg_positioner_handle_set_offset,
};

static void xdg_positioner_handle_resource_destroy(
		struct wl_resource *resource) {
	struct wlr_xdg_positioner *positioner =
		wlr_xdg_positioner_from_resource(resource);
	free(positioner);
}

void create_xdg_positioner(struct wlr_xdg_client *client, uint32_t id) {
	struct wlr_xdg_positioner *positioner = calloc(1, sizeof(*positioner));
	if (positioner == NULL) {
		wl_client_post_no_memory(client->client);
		return;
	}

	positioner->resource = wl_resource_create(client->client,
		&xdg_positioner_interface,
		wl_resource_get_version(client->resource),
		id);
	if (positioner->resource == NULL) {
		free(positioner);
		wl_client_post_no_memory(client->client);
		return;
	}
	wl_resource_set_implementation(positioner->resource,
		&xdg_positioner_implementation,
		positioner, xdg_positioner_handle_resource_destroy);
}

static bool positioner_anchor_has_edge(enum xdg_positioner_anchor anchor,
		enum xdg_positioner_anchor edge) {
	switch (edge) {
	case XDG_POSITIONER_ANCHOR_TOP:
		return anchor == XDG_POSITIONER_ANCHOR_TOP ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM:
		return anchor == XDG_POSITIONER_ANCHOR_BOTTOM ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	case XDG_POSITIONER_ANCHOR_LEFT:
		return anchor == XDG_POSITIONER_ANCHOR_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_LEFT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	case XDG_POSITIONER_ANCHOR_RIGHT:
		return anchor == XDG_POSITIONER_ANCHOR_RIGHT ||
			anchor == XDG_POSITIONER_ANCHOR_TOP_RIGHT ||
			anchor == XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	default:
		abort(); // unreachable
	}
}

static bool positioner_gravity_has_edge(enum xdg_positioner_gravity gravity,
		enum xdg_positioner_gravity edge) {
	// gravity and edge enums are the same
	return positioner_anchor_has_edge((enum xdg_positioner_anchor)gravity,
		(enum xdg_positioner_anchor)edge);
}

void wlr_xdg_positioner_rules_get_geometry(
		struct wlr_xdg_positioner_rules *rules, struct wlr_box *box) {
	box->x = rules->offset.x;
	box->y = rules->offset.y;
	box->width = rules->size.width;
	box->height = rules->size.height;

	if (positioner_anchor_has_edge(rules->anchor, XDG_POSITIONER_ANCHOR_TOP)) {
		box->y += rules->anchor_rect.y;
	} else if (positioner_anchor_has_edge(rules->anchor, XDG_POSITIONER_ANCHOR_BOTTOM)) {
		box->y += rules->anchor_rect.y + rules->anchor_rect.height;
	} else {
		box->y += rules->anchor_rect.y + rules->anchor_rect.height / 2;
	}

	if (positioner_anchor_has_edge(rules->anchor, XDG_POSITIONER_ANCHOR_LEFT)) {
		box->x += rules->anchor_rect.x;
	} else if (positioner_anchor_has_edge(rules->anchor, XDG_POSITIONER_ANCHOR_RIGHT)) {
		box->x += rules->anchor_rect.x + rules->anchor_rect.width;
	} else {
		box->x += rules->anchor_rect.x + rules->anchor_rect.width / 2;
	}

	if (positioner_gravity_has_edge(rules->gravity, XDG_POSITIONER_GRAVITY_TOP)) {
		box->y -= box->height;
	} else if (!positioner_gravity_has_edge(rules->gravity, XDG_POSITIONER_GRAVITY_BOTTOM)) {
		box->y -= box->height / 2;
	}

	if (positioner_gravity_has_edge(rules->gravity, XDG_POSITIONER_GRAVITY_LEFT)) {
		box->x -= box->width;
	} else if (!positioner_gravity_has_edge(rules->gravity, XDG_POSITIONER_GRAVITY_RIGHT)) {
		box->x -= box->width / 2;
	}
}

static enum xdg_positioner_anchor positioner_anchor_invert_x(
		enum xdg_positioner_anchor anchor) {
	switch (anchor) {
	case XDG_POSITIONER_ANCHOR_LEFT:
		return XDG_POSITIONER_ANCHOR_RIGHT;
	case XDG_POSITIONER_ANCHOR_RIGHT:
		return XDG_POSITIONER_ANCHOR_LEFT;
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
		return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
		return XDG_POSITIONER_ANCHOR_TOP_LEFT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	default:
		return anchor;
	}
}

static enum xdg_positioner_gravity positioner_gravity_invert_x(
		enum xdg_positioner_gravity gravity) {
	// gravity and edge enums are the same
	return (enum xdg_positioner_gravity)positioner_anchor_invert_x(
		(enum xdg_positioner_anchor)gravity);
}

static enum xdg_positioner_anchor positioner_anchor_invert_y(
		enum xdg_positioner_anchor anchor) {
	switch (anchor) {
	case XDG_POSITIONER_ANCHOR_TOP:
		return XDG_POSITIONER_ANCHOR_BOTTOM;
	case XDG_POSITIONER_ANCHOR_BOTTOM:
		return XDG_POSITIONER_ANCHOR_TOP;
	case XDG_POSITIONER_ANCHOR_TOP_LEFT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_LEFT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
		return XDG_POSITIONER_ANCHOR_TOP_LEFT;
	case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
		return XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT;
	case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT:
		return XDG_POSITIONER_ANCHOR_TOP_RIGHT;
	default:
		return anchor;
	}
}

static enum xdg_positioner_gravity positioner_gravity_invert_y(
		enum xdg_positioner_gravity gravity) {
	// gravity and edge enums are the same
	return (enum xdg_positioner_gravity)positioner_anchor_invert_y(
		(enum xdg_positioner_anchor)gravity);
}

void wlr_xdg_positioner_rules_invert_x(struct wlr_xdg_positioner_rules *rules) {
	rules->anchor = positioner_anchor_invert_x(rules->anchor);
	rules->gravity = positioner_gravity_invert_x(rules->gravity);
}

void wlr_xdg_positioner_rules_invert_y(struct wlr_xdg_positioner_rules *rules) {
	rules->anchor = positioner_anchor_invert_y(rules->anchor);
	rules->gravity = positioner_gravity_invert_y(rules->gravity);
}
