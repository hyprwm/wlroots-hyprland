#ifndef BACKEND_DRM_RENDERER_H
#define BACKEND_DRM_RENDERER_H

#include <stdbool.h>
#include <stdint.h>
#include <wlr/backend.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/addon.h>

struct wlr_drm_backend;
struct wlr_drm_format;
struct wlr_drm_plane;
struct wlr_buffer;

struct wlr_drm_renderer {
	struct wlr_renderer *wlr_rend;
	struct wlr_allocator *allocator;
};

struct wlr_drm_surface {
	struct wlr_drm_renderer *renderer;
	struct wlr_swapchain *swapchain;
};

bool init_drm_renderer(struct wlr_drm_backend *drm,
	struct wlr_drm_renderer *renderer);
void finish_drm_renderer(struct wlr_drm_renderer *renderer);

bool init_drm_surface(struct wlr_drm_surface *surf,
	struct wlr_drm_renderer *renderer, int width, int height,
	const struct wlr_drm_format *drm_format);
void finish_drm_surface(struct wlr_drm_surface *surf);

struct wlr_buffer *drm_surface_blit(struct wlr_drm_surface *surf,
	struct wlr_buffer *buffer);

bool drm_plane_pick_render_format(struct wlr_drm_plane *plane,
	struct wlr_drm_format *fmt, struct wlr_drm_renderer *renderer);

#endif
