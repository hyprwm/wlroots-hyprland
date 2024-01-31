#ifndef RENDER_ALLOCATOR_ALLOCATOR_H
#define RENDER_ALLOCATOR_ALLOCATOR_H

#include <wlr/render/allocator.h>

struct wlr_allocator *allocator_autocreate_with_drm_fd(
	uint32_t backend_caps, struct wlr_renderer *renderer, int drm_fd);

#endif
