/*
 * This is a deprecated interface of wlroots. It will be removed in a future
 * version. wlr/types/wlr_compositor.h should be used instead.
 */

#ifndef WLR_TYPES_WLR_REGION_H
#define WLR_TYPES_WLR_REGION_H

#include <pixman.h>

struct wl_resource;

/**
 * Obtain a Pixman region from a wl_region resource.
 *
 * To allow clients to create wl_region objects, call wlr_compositor_create().
 */
const pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource);

#endif
