#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_matrix.h>
#include "render/pass.h"

struct wlr_render_pass_legacy {
	struct wlr_render_pass base;
	struct wlr_renderer *renderer;
	int width, height;
};

void wlr_render_pass_init(struct wlr_render_pass *render_pass,
		const struct wlr_render_pass_impl *impl) {
	assert(impl->submit && impl->add_texture && impl->add_rect);
	*render_pass = (struct wlr_render_pass){
		.impl = impl,
	};
}

bool wlr_render_pass_submit(struct wlr_render_pass *render_pass) {
	return render_pass->impl->submit(render_pass);
}

void wlr_render_pass_add_texture(struct wlr_render_pass *render_pass,
		const struct wlr_render_texture_options *options) {
	// make sure the texture source box does not try and sample outside of the
	// texture
	if (!wlr_fbox_empty(&options->src_box)) {
		const struct wlr_fbox *box = &options->src_box;
		assert(box->x >= 0 && box->y >= 0 &&
			box->x + box->width <= options->texture->width &&
			box->y + box->height <= options->texture->height);
	}

	render_pass->impl->add_texture(render_pass, options);
}

void wlr_render_pass_add_rect(struct wlr_render_pass *render_pass,
		const struct wlr_render_rect_options *options) {
	assert(options->box.width >= 0 && options->box.height >= 0);
	render_pass->impl->add_rect(render_pass, options);
}

void wlr_render_texture_options_get_src_box(const struct wlr_render_texture_options *options,
		struct wlr_fbox *box) {
	*box = options->src_box;
	if (wlr_fbox_empty(box)) {
		*box = (struct wlr_fbox){
			.width = options->texture->width,
			.height = options->texture->height,
		};
	}
}

void wlr_render_texture_options_get_dst_box(const struct wlr_render_texture_options *options,
		struct wlr_box *box) {
	*box = options->dst_box;
	if (wlr_box_empty(box)) {
		box->width = options->texture->width;
		box->height = options->texture->height;
	}
}

float wlr_render_texture_options_get_alpha(const struct wlr_render_texture_options *options) {
	if (options->alpha == NULL) {
		return 1;
	}
	return *options->alpha;
}

void wlr_render_rect_options_get_box(const struct wlr_render_rect_options *options,
		const struct wlr_buffer *buffer, struct wlr_box *box) {
	if (wlr_box_empty(&options->box)) {
		*box = (struct wlr_box){
			.width = buffer->width,
			.height = buffer->height,
		};

		return;
	}

	*box = options->box;
}

static const struct wlr_render_pass_impl legacy_impl;

static struct wlr_render_pass_legacy *legacy_pass_from_pass(struct wlr_render_pass *pass) {
	assert(pass->impl == &legacy_impl);
	struct wlr_render_pass_legacy *legacy = wl_container_of(pass, legacy, base);
	return legacy;
}

static bool legacy_submit(struct wlr_render_pass *wlr_pass) {
	struct wlr_render_pass_legacy *pass = legacy_pass_from_pass(wlr_pass);
	wlr_renderer_end(pass->renderer);
	free(pass);
	return true;
}

static void get_clip_region(struct wlr_render_pass_legacy *pass,
		const pixman_region32_t *in, pixman_region32_t *out) {
	if (in != NULL) {
		pixman_region32_init(out);
		pixman_region32_copy(out, in);
	} else {
		pixman_region32_init_rect(out, 0, 0, pass->width, pass->height);
	}
}

static void scissor(struct wlr_renderer *renderer, const pixman_box32_t *rect) {
	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};
	wlr_renderer_scissor(renderer, &box);
}

static void legacy_add_texture(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_texture_options *options) {
	struct wlr_render_pass_legacy *pass = legacy_pass_from_pass(wlr_pass);
	struct wlr_texture *texture = options->texture;

	struct wlr_fbox src_box;
	wlr_render_texture_options_get_src_box(options, &src_box);
	struct wlr_box dst_box;
	wlr_render_texture_options_get_dst_box(options, &dst_box);
	float alpha = wlr_render_texture_options_get_alpha(options);

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &dst_box, options->transform, 0.0, proj);

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	float black[4] = {0};
	int rects_len = 0;
	const pixman_box32_t *rects = pixman_region32_rectangles(&clip, &rects_len);
	for (int i = 0; i < rects_len; i++) {
		scissor(pass->renderer, &rects[i]);

		if (options->blend_mode == WLR_RENDER_BLEND_MODE_NONE) {
			wlr_renderer_clear(pass->renderer, black);
		}

		wlr_render_subtexture_with_matrix(pass->renderer, texture, &src_box, matrix, alpha);
	}

	wlr_renderer_scissor(pass->renderer, NULL);
	pixman_region32_fini(&clip);
}

static void legacy_add_rect(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_rect_options *options) {
	struct wlr_render_pass_legacy *pass = legacy_pass_from_pass(wlr_pass);

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &options->box, WL_OUTPUT_TRANSFORM_NORMAL, 0.0, proj);

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);
	pixman_region32_intersect_rect(&clip, &clip,
		options->box.x, options->box.y, options->box.width, options->box.height);

	float color[4] = {
		options->color.r,
		options->color.g,
		options->color.b,
		options->color.a,
	};

	int rects_len = 0;
	const pixman_box32_t *rects = pixman_region32_rectangles(&clip, &rects_len);
	for (int i = 0; i < rects_len; i++) {
		scissor(pass->renderer, &rects[i]);
		switch (options->blend_mode) {
		case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:
			wlr_render_quad_with_matrix(pass->renderer, color, matrix);
			break;
		case WLR_RENDER_BLEND_MODE_NONE:
			wlr_renderer_clear(pass->renderer, color);
			break;
		}
	}

	wlr_renderer_scissor(pass->renderer, NULL);
	pixman_region32_fini(&clip);
}

static const struct wlr_render_pass_impl legacy_impl = {
	.submit = legacy_submit,
	.add_texture = legacy_add_texture,
	.add_rect = legacy_add_rect,
};

struct wlr_render_pass *begin_legacy_buffer_render_pass(struct wlr_renderer *renderer,
		struct wlr_buffer *buffer) {
	if (renderer->rendering) {
		return NULL;
	}

	struct wlr_render_pass_legacy *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		return NULL;
	}

	wlr_render_pass_init(&pass->base, &legacy_impl);
	pass->renderer = renderer;
	pass->width = buffer->width;
	pass->height = buffer->height;

	if (!wlr_renderer_begin_with_buffer(renderer, buffer)) {
		free(pass);
		return NULL;
	}

	return &pass->base;
}
