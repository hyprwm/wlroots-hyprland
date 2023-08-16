/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_RENDER_PASS_H
#define WLR_RENDER_PASS_H

#include <pixman.h>
#include <stdint.h>
#include <wayland-server-core.h>
#include <wlr/util/box.h>

struct wlr_renderer;
struct wlr_buffer;

/**
 * A render pass accumulates drawing operations until submitted to the GPU.
 */
struct wlr_render_pass;

/**
 * An object that can be queried after a render to get the duration of the render.
 */
struct wlr_render_timer;

struct wlr_buffer_pass_options {
	/* Timer to measure the duration of the render pass */
	struct wlr_render_timer *timer;
};

/**
 * Begin a new render pass with the supplied destination buffer.
 *
 * Callers must call wlr_render_pass_submit() once they are done with the
 * render pass.
 */
struct wlr_render_pass *wlr_renderer_begin_buffer_pass(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer, const struct wlr_buffer_pass_options *options);

/**
 * Submit the render pass.
 *
 * The render pass cannot be used after this function is called.
 */
bool wlr_render_pass_submit(struct wlr_render_pass *render_pass);

/**
 * Blend modes.
 */
enum wlr_render_blend_mode {
	/* Pre-multiplied alpha (default) */
	WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
	/* Blending is disabled */
	WLR_RENDER_BLEND_MODE_NONE,
};

/**
 * Filter modes.
 */
enum wlr_scale_filter_mode {
	/* bilinear texture filtering (default) */
	WLR_SCALE_FILTER_BILINEAR,
	/* nearest texture filtering */
	WLR_SCALE_FILTER_NEAREST,
};

struct wlr_render_texture_options {
	/* Source texture */
	struct wlr_texture *texture;
	/* Source coordinates, leave empty to render the whole texture */
	struct wlr_fbox src_box;
	/* Destination coordinates, width/height default to the texture size */
	struct wlr_box dst_box;
	/* Opacity between 0 (transparent) and 1 (opaque), leave NULL for opaque */
	const float *alpha;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;
	/* Transform applied to the source texture */
	enum wl_output_transform transform;
	/* Filtering */
	enum wlr_scale_filter_mode filter_mode;
	/* Blend mode */
	enum wlr_render_blend_mode blend_mode;
};

/**
 * Render a texture.
 */
void wlr_render_pass_add_texture(struct wlr_render_pass *render_pass,
	const struct wlr_render_texture_options *options);

/**
 * A color value.
 *
 * Each channel has values between 0 and 1 inclusive. The R, G, B
 * channels need to be pre-multiplied by A.
 */
struct wlr_render_color {
	float r, g, b, a;
};

struct wlr_render_rect_options {
	/* Rectangle coordinates */
	struct wlr_box box;
	/* Source color */
	struct wlr_render_color color;
	/* Clip region, leave NULL to disable clipping */
	const pixman_region32_t *clip;
	/* Blend mode */
	enum wlr_render_blend_mode blend_mode;
};

/**
 * Render a rectangle.
 */
void wlr_render_pass_add_rect(struct wlr_render_pass *render_pass,
	const struct wlr_render_rect_options *options);

#endif
