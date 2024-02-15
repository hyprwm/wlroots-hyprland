#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_fullscreen_shell_v1.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>

/**
 * A minimal fullscreen-shell server. It only supports rendering.
 */

struct fullscreen_server {
	struct wl_display *wl_display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_allocator *allocator;

	struct wlr_fullscreen_shell_v1 *fullscreen_shell;
	struct wl_listener present_surface;

	struct wlr_output_layout *output_layout;
	struct wl_list outputs;
	struct wl_listener new_output;
};

struct fullscreen_output {
	struct wl_list link;
	struct fullscreen_server *server;
	struct wlr_output *wlr_output;
	struct wlr_surface *surface;
	struct wl_listener surface_destroy;

	struct wl_listener frame;
};

struct render_data {
	struct wlr_output *output;
	struct wlr_render_pass *render_pass;
	struct timespec *when;
};

static void render_surface(struct wlr_surface *surface,
		int sx, int sy, void *data) {
	struct render_data *rdata = data;
	struct wlr_output *output = rdata->output;

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture == NULL) {
		return;
	}

	struct wlr_box box = {
		.x = sx * output->scale,
		.y = sy * output->scale,
		.width = surface->current.width * output->scale,
		.height = surface->current.height * output->scale,
	};

	enum wl_output_transform transform = wlr_output_transform_invert(surface->current.transform);
	transform = wlr_output_transform_compose(transform, output->transform);

	wlr_render_pass_add_texture(rdata->render_pass, &(struct wlr_render_texture_options){
		.texture = texture,
		.dst_box = box,
		.transform = transform,
	});

	wlr_surface_send_frame_done(surface, rdata->when);
}

static void output_handle_frame(struct wl_listener *listener, void *data) {
	struct fullscreen_output *output =
		wl_container_of(listener, output, frame);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	int width, height;
	wlr_output_effective_resolution(output->wlr_output, &width, &height);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	struct wlr_render_pass *pass = wlr_output_begin_render_pass(output->wlr_output, &state, NULL,
		NULL);
	if (pass == NULL) {
		return;
	}

	wlr_render_pass_add_rect(pass, &(struct wlr_render_rect_options){
		.color = { 0.3, 0.3, 0.3, 1.0 },
		.box = { .width = width, .height = height },
	});

	if (output->surface != NULL) {
		struct render_data rdata = {
			.output = output->wlr_output,
			.render_pass = pass,
			.when = &now,
		};
		wlr_surface_for_each_surface(output->surface, render_surface, &rdata);
	}

	wlr_render_pass_submit(pass);
	wlr_output_commit_state(output->wlr_output, &state);
	wlr_output_state_finish(&state);
}

static void output_set_surface(struct fullscreen_output *output,
		struct wlr_surface *surface);

static void output_handle_surface_destroy(struct wl_listener *listener,
		void *data) {
	struct fullscreen_output *output =
		wl_container_of(listener, output, surface_destroy);
	output_set_surface(output, NULL);
}

static void output_set_surface(struct fullscreen_output *output,
		struct wlr_surface *surface) {
	if (output->surface == surface) {
		return;
	}

	if (output->surface != NULL) {
		wl_list_remove(&output->surface_destroy.link);
		output->surface = NULL;
	}

	if (surface != NULL) {
		output->surface_destroy.notify = output_handle_surface_destroy;
		wl_signal_add(&surface->events.destroy, &output->surface_destroy);
		output->surface = surface;
	}

	wlr_log(WLR_DEBUG, "Presenting surface %p on output %s",
		surface, output->wlr_output->name);
}

static void server_handle_new_output(struct wl_listener *listener, void *data) {
	struct fullscreen_server *server =
		wl_container_of(listener, server, new_output);
	struct wlr_output *wlr_output = data;

	wlr_output_init_render(wlr_output, server->allocator, server->renderer);

	struct fullscreen_output *output = calloc(1, sizeof(*output));
	output->wlr_output = wlr_output;
	output->server = server;
	output->frame.notify = output_handle_frame;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&server->outputs, &output->link);

	wlr_output_layout_add_auto(server->output_layout, wlr_output);
	wlr_output_create_global(wlr_output, server->wl_display);

	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_enabled(&state, true);
	struct wlr_output_mode *mode = wlr_output_preferred_mode(wlr_output);
	if (mode != NULL) {
		wlr_output_state_set_mode(&state, mode);
	}
	wlr_output_commit_state(wlr_output, &state);
	wlr_output_state_finish(&state);
}

static void server_handle_present_surface(struct wl_listener *listener,
		void *data) {
	struct fullscreen_server *server =
		wl_container_of(listener, server, present_surface);
	struct wlr_fullscreen_shell_v1_present_surface_event *event = data;

	struct fullscreen_output *output;
	wl_list_for_each(output, &server->outputs, link) {
		if (event->output == NULL || event->output == output->wlr_output) {
			output_set_surface(output, event->surface);
		}
	}
}

int main(int argc, char *argv[]) {
	wlr_log_init(WLR_DEBUG, NULL);

	char *startup_cmd = NULL;

	int c;
	while ((c = getopt(argc, argv, "s:")) != -1) {
		switch (c) {
		case 's':
			startup_cmd = optarg;
			break;
		default:
			printf("usage: %s [-s startup-command]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (optind < argc) {
		printf("usage: %s [-s startup-command]\n", argv[0]);
		return EXIT_FAILURE;
	}

	struct fullscreen_server server = {0};
	server.wl_display = wl_display_create();
	server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.wl_display), NULL);
	server.renderer = wlr_renderer_autocreate(server.backend);
	wlr_renderer_init_wl_display(server.renderer, server.wl_display);
	server.allocator = wlr_allocator_autocreate(server.backend,
		server.renderer);

	wlr_compositor_create(server.wl_display, 5, server.renderer);

	server.output_layout = wlr_output_layout_create(server.wl_display);

	wl_list_init(&server.outputs);
	server.new_output.notify = server_handle_new_output;
	wl_signal_add(&server.backend->events.new_output, &server.new_output);

	server.fullscreen_shell = wlr_fullscreen_shell_v1_create(server.wl_display);
	server.present_surface.notify = server_handle_present_surface;
	wl_signal_add(&server.fullscreen_shell->events.present_surface,
		&server.present_surface);

	const char *socket = wl_display_add_socket_auto(server.wl_display);
	if (!socket) {
		wl_display_destroy(server.wl_display);
		return EXIT_FAILURE;
	}

	if (!wlr_backend_start(server.backend)) {
		wl_display_destroy(server.wl_display);
		return EXIT_FAILURE;
	}

	setenv("WAYLAND_DISPLAY", socket, true);
	if (startup_cmd != NULL) {
		if (fork() == 0) {
			execl("/bin/sh", "/bin/sh", "-c", startup_cmd, (void *)NULL);
		}
	}

	wlr_log(WLR_INFO, "Running Wayland compositor on WAYLAND_DISPLAY=%s",
			socket);
	wl_display_run(server.wl_display);

	wl_display_destroy_clients(server.wl_display);
	wl_display_destroy(server.wl_display);
	return 0;
}
