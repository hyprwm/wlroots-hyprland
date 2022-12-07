#ifndef RENDER_PASS_H
#define RENDER_PASS_H

#include <wlr/render/interface.h>

struct wlr_render_pass *begin_legacy_buffer_render_pass(struct wlr_renderer *renderer,
	struct wlr_buffer *buffer);

#endif
