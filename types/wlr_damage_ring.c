#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <pixman.h>
#include <wlr/types/wlr_damage_ring.h>
#include <wlr/util/box.h>

#define WLR_DAMAGE_RING_MAX_RECTS 20

void wlr_damage_ring_init(struct wlr_damage_ring *ring) {
	memset(ring, 0, sizeof(*ring));

	ring->width = INT_MAX;
	ring->height = INT_MAX;

	pixman_region32_init(&ring->current);
	for (size_t i = 0; i < WLR_DAMAGE_RING_PREVIOUS_LEN; ++i) {
		pixman_region32_init(&ring->previous[i]);
	}
}

void wlr_damage_ring_finish(struct wlr_damage_ring *ring) {
	pixman_region32_fini(&ring->current);
	for (size_t i = 0; i < WLR_DAMAGE_RING_PREVIOUS_LEN; ++i) {
		pixman_region32_fini(&ring->previous[i]);
	}
}

void wlr_damage_ring_set_bounds(struct wlr_damage_ring *ring,
		int32_t width, int32_t height) {
	if (width == 0 || height == 0) {
		ring->width = INT_MAX;
		ring->height = INT_MAX;
	} else {
		ring->width = width;
		ring->height = height;
	}
	wlr_damage_ring_add_whole(ring);
}

bool wlr_damage_ring_add(struct wlr_damage_ring *ring,
		pixman_region32_t *damage) {
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
