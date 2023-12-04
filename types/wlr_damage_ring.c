#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/box.h>

#define WLR_DAMAGE_RING_MAX_RECTS 20

void wlr_damage_ring_init(struct wlr_damage_ring *ring) {
	*ring = (struct wlr_damage_ring){
		.width = INT_MAX,
		.height = INT_MAX,
	};

	pixman_region32_init(&ring->current);
	for (size_t i = 0; i < WLR_DAMAGE_RING_PREVIOUS_LEN; ++i) {
		pixman_region32_init(&ring->previous[i]);
	}

	for (size_t i = 0; i < WLR_DAMAGE_RING_BUFFERS_LEN; ++i) {
		struct wlr_damage_ring_buffer *ring_buffer = &ring->buffers[i];
		wl_list_init(&ring_buffer->destroy.link);
		pixman_region32_init(&ring_buffer->damage);
	}
}

void wlr_damage_ring_finish(struct wlr_damage_ring *ring) {
	pixman_region32_fini(&ring->current);
	for (size_t i = 0; i < WLR_DAMAGE_RING_PREVIOUS_LEN; ++i) {
		pixman_region32_fini(&ring->previous[i]);
	}
	for (size_t i = 0; i < WLR_DAMAGE_RING_BUFFERS_LEN; ++i) {
		struct wlr_damage_ring_buffer *ring_buffer = &ring->buffers[i];
		wl_list_remove(&ring_buffer->destroy.link);
		pixman_region32_fini(&ring_buffer->damage);
	}
}

void wlr_damage_ring_set_bounds(struct wlr_damage_ring *ring,
		int32_t width, int32_t height) {
	if (width == 0 || height == 0) {
		width = INT_MAX;
		height = INT_MAX;
	}

	if (ring->width == width && ring->height == height) {
		return;
	}

	ring->width = width;
	ring->height = height;
	wlr_damage_ring_add_whole(ring);
}

bool wlr_damage_ring_add(struct wlr_damage_ring *ring,
		const pixman_region32_t *damage) {
	pixman_region32_t clipped;
	pixman_region32_init(&clipped);
	pixman_region32_intersect_rect(&clipped, damage,
		0, 0, ring->width, ring->height);
	bool intersects = pixman_region32_not_empty(&clipped);
	if (intersects) {
		pixman_region32_union(&ring->current, &ring->current, &clipped);
	}
	pixman_region32_fini(&clipped);
	return intersects;
}

bool wlr_damage_ring_add_box(struct wlr_damage_ring *ring,
		const struct wlr_box *box) {
	struct wlr_box clipped = {
		.x = 0,
		.y = 0,
		.width = ring->width,
		.height = ring->height,
	};
	if (wlr_box_intersection(&clipped, &clipped, box)) {
		pixman_region32_union_rect(&ring->current,
			&ring->current, clipped.x, clipped.y,
			clipped.width, clipped.height);
		return true;
	}
	return false;
}

void wlr_damage_ring_add_whole(struct wlr_damage_ring *ring) {
	pixman_region32_union_rect(&ring->current,
		&ring->current, 0, 0, ring->width, ring->height);
}

void wlr_damage_ring_rotate(struct wlr_damage_ring *ring) {
	// modular decrement
	ring->previous_idx = ring->previous_idx +
		WLR_DAMAGE_RING_PREVIOUS_LEN - 1;
	ring->previous_idx %= WLR_DAMAGE_RING_PREVIOUS_LEN;

	pixman_region32_copy(&ring->previous[ring->previous_idx], &ring->current);
	pixman_region32_clear(&ring->current);
}

void wlr_damage_ring_get_buffer_damage(struct wlr_damage_ring *ring,
		int buffer_age, pixman_region32_t *damage) {
	if (buffer_age <= 0 || buffer_age - 1 > WLR_DAMAGE_RING_PREVIOUS_LEN) {
		pixman_region32_clear(damage);
		pixman_region32_union_rect(damage, damage,
			0, 0, ring->width, ring->height);
	} else {
		pixman_region32_copy(damage, &ring->current);

		// Accumulate damage from old buffers
		for (int i = 0; i < buffer_age - 1; ++i) {
			int j = (ring->previous_idx + i) % WLR_DAMAGE_RING_PREVIOUS_LEN;
			pixman_region32_union(damage, damage, &ring->previous[j]);
		}

		// Check the number of rectangles
		int n_rects = pixman_region32_n_rects(damage);
		if (n_rects > WLR_DAMAGE_RING_MAX_RECTS) {
			pixman_box32_t *extents = pixman_region32_extents(damage);
			pixman_region32_union_rect(damage, damage,
				extents->x1, extents->y1,
				extents->x2 - extents->x1,
				extents->y2 - extents->y1);
		}
	}
}

static void damage_ring_buffer_reset_buffer(struct wlr_damage_ring_buffer *ring_buffer) {
	ring_buffer->buffer = NULL;
	wl_list_remove(&ring_buffer->destroy.link);
	wl_list_init(&ring_buffer->destroy.link);
}

static void damage_ring_handle_buffer_destroy(struct wl_listener *listener, void *data) {
	struct wlr_damage_ring_buffer *ring_buffer =
		wl_container_of(listener, ring_buffer, destroy);
	damage_ring_buffer_reset_buffer(ring_buffer);
}

void wlr_damage_ring_rotate_buffer(struct wlr_damage_ring *ring,
		struct wlr_buffer *buffer, pixman_region32_t *damage) {
	bool found = false;
	struct wlr_damage_ring_buffer *ring_buffer, *oldest = NULL;
	for (size_t i = 0; i < WLR_DAMAGE_RING_BUFFERS_LEN; i++) {
		ring_buffer = &ring->buffers[i];
		if (ring_buffer->buffer == buffer) {
			found = true;
			break;
		}
		if (oldest == NULL || ring_buffer->seq < oldest->seq) {
			oldest = ring_buffer;
		}
	}

	if (!found) {
		damage_ring_buffer_reset_buffer(oldest);
		ring_buffer = oldest;

		ring_buffer->buffer = buffer;

		ring_buffer->destroy.notify = damage_ring_handle_buffer_destroy;
		wl_signal_add(&buffer->events.destroy, &ring_buffer->destroy);

		pixman_region32_clear(damage);
		pixman_region32_union_rect(damage, damage, 0, 0, ring->width, ring->height);
	} else {
		pixman_region32_copy(damage, &ring->current);

		// Accumulate damage from old buffers
		for (size_t i = 0; i < WLR_DAMAGE_RING_BUFFERS_LEN; i++) {
			struct wlr_damage_ring_buffer *rb = &ring->buffers[i];
			if (rb->seq > ring_buffer->seq) {
				pixman_region32_union(damage, damage, &rb->damage);
			}
		}

		// Check the number of rectangles
		int n_rects = pixman_region32_n_rects(damage);
		if (n_rects > WLR_DAMAGE_RING_MAX_RECTS) {
			pixman_box32_t *extents = pixman_region32_extents(damage);
			pixman_region32_union_rect(damage, damage,
				extents->x1, extents->y1,
				extents->x2 - extents->x1,
				extents->y2 - extents->y1);
		}
	}

	ring_buffer->seq = ++ring->last_buffer_seq;
	pixman_region32_copy(&ring_buffer->damage, &ring->current);
	pixman_region32_clear(&ring->current);
}
