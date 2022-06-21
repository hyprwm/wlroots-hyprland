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
