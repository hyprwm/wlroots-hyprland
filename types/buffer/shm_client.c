#include <assert.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/interfaces/wlr_buffer.h>
#include "render/pixel_format.h"
#include "types/wlr_buffer.h"

static const struct wlr_buffer_impl shm_client_buffer_impl;

static bool buffer_is_shm_client_buffer(struct wlr_buffer *buffer) {
	return buffer->impl == &shm_client_buffer_impl;
}

static struct wlr_shm_client_buffer *shm_client_buffer_from_buffer(
		struct wlr_buffer *buffer) {
	assert(buffer_is_shm_client_buffer(buffer));
	return (struct wlr_shm_client_buffer *)buffer;
}

static void shm_client_buffer_destroy(struct wlr_buffer *wlr_buffer) {
	struct wlr_shm_client_buffer *buffer =
		shm_client_buffer_from_buffer(wlr_buffer);
	wl_list_remove(&buffer->resource_destroy.link);
	wl_list_remove(&buffer->release.link);
	if (buffer->saved_shm_pool != NULL) {
		wl_shm_pool_unref(buffer->saved_shm_pool);
	}
	free(buffer);
}

static bool shm_client_buffer_begin_data_ptr_access(struct wlr_buffer *wlr_buffer,
		uint32_t flags, void **data, uint32_t *format, size_t *stride) {
	struct wlr_shm_client_buffer *buffer =
		shm_client_buffer_from_buffer(wlr_buffer);
	*format = buffer->format;
	*stride = buffer->stride;
	if (buffer->shm_buffer != NULL) {
		*data = wl_shm_buffer_get_data(buffer->shm_buffer);
		wl_shm_buffer_begin_access(buffer->shm_buffer);
	} else {
		*data = buffer->saved_data;
	}
	return true;
}

static void shm_client_buffer_end_data_ptr_access(struct wlr_buffer *wlr_buffer) {
	struct wlr_shm_client_buffer *buffer =
		shm_client_buffer_from_buffer(wlr_buffer);
	if (buffer->shm_buffer != NULL) {
		wl_shm_buffer_end_access(buffer->shm_buffer);
	}
}

static const struct wlr_buffer_impl shm_client_buffer_impl = {
	.destroy = shm_client_buffer_destroy,
	.begin_data_ptr_access = shm_client_buffer_begin_data_ptr_access,
	.end_data_ptr_access = shm_client_buffer_end_data_ptr_access,
};

static void shm_client_buffer_resource_handle_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_shm_client_buffer *buffer =
		wl_container_of(listener, buffer, resource_destroy);

	// In order to still be able to access the shared memory region, we need to
	// keep a reference to the wl_shm_pool
	buffer->saved_shm_pool = wl_shm_buffer_ref_pool(buffer->shm_buffer);
	buffer->saved_data = wl_shm_buffer_get_data(buffer->shm_buffer);

	// The wl_shm_buffer destroys itself with the wl_resource
	buffer->resource = NULL;
	buffer->shm_buffer = NULL;
	wl_list_remove(&buffer->resource_destroy.link);
	wl_list_init(&buffer->resource_destroy.link);

	// This might destroy the buffer
	wlr_buffer_drop(&buffer->base);
}

static void shm_client_buffer_handle_release(struct wl_listener *listener,
		void *data) {
	struct wlr_shm_client_buffer *buffer =
		wl_container_of(listener, buffer, release);
	if (buffer->resource != NULL) {
		wl_buffer_send_release(buffer->resource);
	}
}

struct wlr_shm_client_buffer *shm_client_buffer_get_or_create(
		struct wl_resource *resource) {
	struct wl_shm_buffer *shm_buffer = wl_shm_buffer_get(resource);
	assert(shm_buffer != NULL);

	struct wl_listener *resource_destroy_listener =
		wl_resource_get_destroy_listener(resource,
		shm_client_buffer_resource_handle_destroy);
	if (resource_destroy_listener != NULL) {
		struct wlr_shm_client_buffer *buffer =
			wl_container_of(resource_destroy_listener, buffer, resource_destroy);
		return buffer;
	}

	int32_t width = wl_shm_buffer_get_width(shm_buffer);
	int32_t height = wl_shm_buffer_get_height(shm_buffer);

	struct wlr_shm_client_buffer *buffer = calloc(1, sizeof(*buffer));
	if (buffer == NULL) {
		return NULL;
	}
	wlr_buffer_init(&buffer->base, &shm_client_buffer_impl, width, height);
	buffer->resource = resource;
	buffer->shm_buffer = shm_buffer;

	enum wl_shm_format wl_shm_format = wl_shm_buffer_get_format(shm_buffer);
	buffer->format = convert_wl_shm_format_to_drm(wl_shm_format);
	buffer->stride = wl_shm_buffer_get_stride(shm_buffer);

	buffer->resource_destroy.notify = shm_client_buffer_resource_handle_destroy;
	wl_resource_add_destroy_listener(resource, &buffer->resource_destroy);

	buffer->release.notify = shm_client_buffer_handle_release;
	wl_signal_add(&buffer->base.events.release, &buffer->release);

	return buffer;
}
