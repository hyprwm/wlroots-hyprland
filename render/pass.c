#include <assert.h>
#include <string.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>

void wlr_render_pass_init(struct wlr_render_pass *render_pass,
		const struct wlr_render_pass_impl *impl) {
	assert(impl->submit && impl->add_texture && impl->add_rect);
	memset(render_pass, 0, sizeof(*render_pass));
	render_pass->impl = impl;
}

bool wlr_render_pass_submit(struct wlr_render_pass *render_pass) {
	return render_pass->impl->submit(render_pass);
}

void wlr_render_pass_add_texture(struct wlr_render_pass *render_pass,
		const struct wlr_render_texture_options *options) {
	render_pass->impl->add_texture(render_pass, options);
}

void wlr_render_pass_add_rect(struct wlr_render_pass *render_pass,
		const struct wlr_render_rect_options *options) {
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
