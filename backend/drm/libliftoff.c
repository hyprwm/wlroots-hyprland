#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <libliftoff.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wlr/util/log.h>

#include "backend/drm/drm.h"
#include "backend/drm/iface.h"

static bool init(struct wlr_drm_backend *drm) {
	// TODO: lower log level
	liftoff_log_set_priority(LIFTOFF_DEBUG);

	int drm_fd = fcntl(drm->fd, F_DUPFD_CLOEXEC, 0);
	if (drm_fd < 0) {
		wlr_log_errno(WLR_ERROR, "fcntl(F_DUPFD_CLOEXEC) failed");
		return false;
	}

	drm->liftoff = liftoff_device_create(drm_fd);
	if (!drm->liftoff) {
		wlr_log(WLR_ERROR, "Failed to create liftoff device");
		close(drm_fd);
		return false;
	}

	for (size_t i = 0; i < drm->num_planes; i++) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		if (plane->initial_crtc_id != 0) {
			continue;
		}
		plane->liftoff = liftoff_plane_create(drm->liftoff, plane->id);
		if (plane->liftoff == NULL) {
			wlr_log(WLR_ERROR, "Failed to create liftoff plane");
			return false;
		}
	}

	for (size_t i = 0; i < drm->num_crtcs; i++) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];

		crtc->liftoff = liftoff_output_create(drm->liftoff, crtc->id);
		if (!crtc->liftoff) {
			wlr_log(WLR_ERROR, "Failed to create liftoff output");
			return false;
		}

		crtc->liftoff_composition_layer = liftoff_layer_create(crtc->liftoff);
		if (!crtc->liftoff_composition_layer) {
			wlr_log(WLR_ERROR, "Failed to create liftoff composition layer");
			return false;
		}
		liftoff_output_set_composition_layer(crtc->liftoff,
			crtc->liftoff_composition_layer);

		if (crtc->primary) {
			crtc->primary->liftoff_layer = liftoff_layer_create(crtc->liftoff);
			if (!crtc->primary->liftoff_layer) {
				wlr_log(WLR_ERROR, "Failed to create liftoff layer for primary plane");
				return false;
			}
		}

		if (crtc->cursor) {
			crtc->cursor->liftoff_layer = liftoff_layer_create(crtc->liftoff);
			if (!crtc->cursor->liftoff_layer) {
				wlr_log(WLR_ERROR, "Failed to create liftoff layer for cursor plane");
				return false;
			}
		}
	}

	return true;
}

static bool register_planes_for_crtc(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	// When performing the first modeset on a CRTC, we need to be a bit careful
	// when it comes to planes: we don't want to allow libliftoff to make use
	// of planes currently already in-use on another CRTC. We need to wait for
	// a modeset to happen on the other CRTC before being able to use these.
	for (size_t i = 0; i < drm->num_planes; i++) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		if (plane->liftoff != NULL || plane->initial_crtc_id != crtc->id) {
			continue;
		}
		plane->liftoff = liftoff_plane_create(drm->liftoff, plane->id);
		if (plane->liftoff == NULL) {
			wlr_log(WLR_ERROR, "Failed to create liftoff plane");
			return false;
		}
	}
	return true;
}

static void finish(struct wlr_drm_backend *drm) {
	for (size_t i = 0; i < drm->num_crtcs; i++) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];

		if (crtc->primary) {
			liftoff_layer_destroy(crtc->primary->liftoff_layer);
		}
		if (crtc->cursor) {
			liftoff_layer_destroy(crtc->cursor->liftoff_layer);
		}

		liftoff_layer_destroy(crtc->liftoff_composition_layer);
		liftoff_output_destroy(crtc->liftoff);
	}

	for (size_t i = 0; i < drm->num_planes; i++) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		liftoff_plane_destroy(plane->liftoff);
	}

	liftoff_device_destroy(drm->liftoff);
}

static bool add_prop(drmModeAtomicReq *req, uint32_t obj,
		uint32_t prop, uint64_t val) {
	if (drmModeAtomicAddProperty(req, obj, prop, val) < 0) {
		wlr_log_errno(WLR_ERROR, "drmModeAtomicAddProperty failed");
		return false;
	}
	return true;
}

static void commit_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (*current != 0) {
		drmModeDestroyPropertyBlob(drm->fd, *current);
	}
	*current = next;
}

static void rollback_blob(struct wlr_drm_backend *drm,
		uint32_t *current, uint32_t next) {
	if (*current == next) {
		return;
	}
	if (next != 0) {
		drmModeDestroyPropertyBlob(drm->fd, next);
	}
}

static bool set_plane_props(struct wlr_drm_plane *plane,
		struct liftoff_layer *layer, struct wlr_drm_fb *fb, int32_t x, int32_t y, uint64_t zpos) {
	if (fb == NULL) {
		wlr_log(WLR_ERROR, "Failed to acquire FB for plane %"PRIu32, plane->id);
		return false;
	}

	uint32_t width = fb->wlr_buf->width;
	uint32_t height = fb->wlr_buf->height;

	// The SRC_* properties are in 16.16 fixed point
	return liftoff_layer_set_property(layer, "zpos", zpos) == 0 &&
		liftoff_layer_set_property(layer, "SRC_X", 0) == 0 &&
		liftoff_layer_set_property(layer, "SRC_Y", 0) == 0 &&
		liftoff_layer_set_property(layer, "SRC_W", (uint64_t)width << 16) == 0 &&
		liftoff_layer_set_property(layer, "SRC_H", (uint64_t)height << 16) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_X", (uint64_t)x) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_Y", (uint64_t)y) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_W", width) == 0 &&
		liftoff_layer_set_property(layer, "CRTC_H", height) == 0 &&
		liftoff_layer_set_property(layer, "FB_ID", fb->id) == 0;
}

static bool disable_plane(struct wlr_drm_plane *plane) {
	return liftoff_layer_set_property(plane->liftoff_layer, "FB_ID", 0) == 0;
}

static uint64_t to_fp16(double v) {
	return (uint64_t)round(v * (1 << 16));
}

static bool set_layer_props(struct wlr_drm_backend *drm,
		const struct wlr_output_layer_state *state, uint64_t zpos,
		struct wl_array *fb_damage_clips_arr) {
	struct wlr_drm_layer *layer = get_drm_layer(drm, state->layer);

	uint32_t width = 0, height = 0;
	if (state->buffer != NULL) {
		width = state->buffer->width;
		height = state->buffer->height;
	}

	struct wlr_drm_fb *fb = layer->pending_fb;
	int ret = 0;
	if (state->buffer == NULL) {
		ret = liftoff_layer_set_property(layer->liftoff, "FB_ID", 0);
	} else if (fb == NULL) {
		liftoff_layer_set_fb_composited(layer->liftoff);
	} else {
		ret = liftoff_layer_set_property(layer->liftoff, "FB_ID", fb->id);
	}
	if (ret != 0) {
		return false;
	}

	uint64_t crtc_x = (uint64_t)state->dst_box.x;
	uint64_t crtc_y = (uint64_t)state->dst_box.y;
	uint64_t crtc_w = (uint64_t)state->dst_box.width;
	uint64_t crtc_h = (uint64_t)state->dst_box.height;

	struct wlr_fbox src_box = state->src_box;
	if (wlr_fbox_empty(&src_box)) {
		src_box = (struct wlr_fbox){
			.width = width,
			.height = height,
		};
	}

	uint64_t src_x = to_fp16(src_box.x);
	uint64_t src_y = to_fp16(src_box.y);
	uint64_t src_w = to_fp16(src_box.width);
	uint64_t src_h = to_fp16(src_box.height);

	uint32_t fb_damage_clips = 0;
	if (state->damage != NULL) {
		uint32_t *ptr = wl_array_add(fb_damage_clips_arr, sizeof(fb_damage_clips));
		if (ptr == NULL) {
			return false;
		}
		create_fb_damage_clips_blob(drm, width, height,
			state->damage, &fb_damage_clips);
		*ptr = fb_damage_clips;
	}

	return
		liftoff_layer_set_property(layer->liftoff, "zpos", zpos) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "CRTC_X", crtc_x) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "CRTC_Y", crtc_y) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "CRTC_W", crtc_w) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "CRTC_H", crtc_h) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "SRC_X", src_x) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "SRC_Y", src_y) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "SRC_W", src_w) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "SRC_H", src_h) == 0 &&
		liftoff_layer_set_property(layer->liftoff, "FB_DAMAGE_CLIPS", fb_damage_clips) == 0;
}

static bool devid_from_fd(int fd, dev_t *devid) {
	struct stat stat;
	if (fstat(fd, &stat) != 0) {
		wlr_log_errno(WLR_ERROR, "fstat failed");
		return false;
	}
	*devid = stat.st_rdev;
	return true;
}

static void update_layer_feedback(struct wlr_drm_backend *drm,
		struct wlr_drm_layer *layer) {
	bool changed = false;
	for (size_t i = 0; i < drm->num_planes; i++) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		bool is_candidate = liftoff_layer_is_candidate_plane(layer->liftoff,
			plane->liftoff);
		if (layer->candidate_planes[i] != is_candidate) {
			layer->candidate_planes[i] = is_candidate;
			changed = true;
		}
	}
	if (!changed) {
		return;
	}

	dev_t target_device;
	if (!devid_from_fd(drm->fd, &target_device)) {
		return;
	}

	struct wlr_drm_format_set formats = {0};
	for (size_t i = 0; i < drm->num_planes; i++) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		if (!layer->candidate_planes[i]) {
			continue;
		}

		for (size_t j = 0; j < plane->formats.len; j++) {
			const struct wlr_drm_format *format = &plane->formats.formats[j];
			for (size_t k = 0; k < format->len; k++) {
				wlr_drm_format_set_add(&formats, format->format,
					format->modifiers[k]);
			}
		}
	}

	struct wlr_output_layer_feedback_event event = {
		.target_device = target_device,
		.formats = &formats,
	};
	wl_signal_emit_mutable(&layer->wlr->events.feedback, &event);

	wlr_drm_format_set_finish(&formats);
}

static bool crtc_commit(struct wlr_drm_connector *conn,
		const struct wlr_drm_connector_state *state, uint32_t flags,
		bool test_only) {
	struct wlr_drm_backend *drm = conn->backend;
	struct wlr_output *output = &conn->output;
	struct wlr_drm_crtc *crtc = conn->crtc;

	bool modeset = state->modeset;
	bool active = state->active;

	if (modeset && !register_planes_for_crtc(drm, crtc)) {
		return false;
	}

	uint32_t mode_id = crtc->mode_id;
	if (modeset) {
		if (!create_mode_blob(drm, conn, state, &mode_id)) {
			return false;
		}
	}

	uint32_t gamma_lut = crtc->gamma_lut;
	if (state->base->committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		// Fallback to legacy gamma interface when gamma properties are not
		// available (can happen on older Intel GPUs that support gamma but not
		// degamma).
		if (crtc->props.gamma_lut == 0) {
			if (!drm_legacy_crtc_set_gamma(drm, crtc,
					state->base->gamma_lut_size,
					state->base->gamma_lut)) {
				return false;
			}
		} else {
			if (!create_gamma_lut_blob(drm, state->base->gamma_lut_size,
					state->base->gamma_lut, &gamma_lut)) {
				return false;
			}
		}
	}

	struct wl_array fb_damage_clips_arr = {0};

	uint32_t primary_fb_damage_clips = 0;
	if ((state->base->committed & WLR_OUTPUT_STATE_DAMAGE) &&
			crtc->primary->props.fb_damage_clips != 0) {
		uint32_t *ptr = wl_array_add(&fb_damage_clips_arr, sizeof(primary_fb_damage_clips));
		if (ptr == NULL) {
			return false;
		}
		create_fb_damage_clips_blob(drm, state->primary_fb->wlr_buf->width,
			state->primary_fb->wlr_buf->height, &state->base->damage,
			&primary_fb_damage_clips);
		*ptr = primary_fb_damage_clips;
	}

	bool prev_vrr_enabled =
		output->adaptive_sync_status == WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED;
	bool vrr_enabled = prev_vrr_enabled;
	if ((state->base->committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED) &&
			drm_connector_supports_vrr(conn)) {
		vrr_enabled = state->base->adaptive_sync_enabled;
	}

	if (test_only) {
		flags |= DRM_MODE_ATOMIC_TEST_ONLY;
	}
	if (modeset) {
		flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
	} else if (!test_only && (state->base->committed & WLR_OUTPUT_STATE_BUFFER)) {
		// The wlr_output API requires non-modeset commits with a new buffer to
		// wait for the frame event. However compositors often perform
		// non-modesets commits without a new buffer without waiting for the
		// frame event. In that case we need to make the KMS commit blocking,
		// otherwise the kernel will error out with EBUSY.
		flags |= DRM_MODE_ATOMIC_NONBLOCK;
	}

	drmModeAtomicReq *req = drmModeAtomicAlloc();
	if (req == NULL) {
		wlr_log(WLR_ERROR, "drmModeAtomicAlloc failed");
		return false;
	}

	bool ok = add_prop(req, conn->id, conn->props.crtc_id,
		active ? crtc->id : 0);
	if (modeset && active && conn->props.link_status != 0) {
		ok = ok && add_prop(req, conn->id, conn->props.link_status,
			DRM_MODE_LINK_STATUS_GOOD);
	}
	if (active && conn->props.content_type != 0) {
		ok = ok && add_prop(req, conn->id, conn->props.content_type,
			DRM_MODE_CONTENT_TYPE_GRAPHICS);
	}
	// TODO: set "max bpc"
	ok = ok &&
		add_prop(req, crtc->id, crtc->props.mode_id, mode_id) &&
		add_prop(req, crtc->id, crtc->props.active, active);
	if (active) {
		if (crtc->props.gamma_lut != 0) {
			ok = ok && add_prop(req, crtc->id, crtc->props.gamma_lut, gamma_lut);
		}
		if (crtc->props.vrr_enabled != 0) {
			ok = ok && add_prop(req, crtc->id, crtc->props.vrr_enabled, vrr_enabled);
		}
		ok = ok &&
			set_plane_props(crtc->primary, crtc->primary->liftoff_layer, state->primary_fb, 0, 0, 0) &&
			set_plane_props(crtc->primary, crtc->liftoff_composition_layer, state->primary_fb, 0, 0, 0);
		liftoff_layer_set_property(crtc->primary->liftoff_layer,
			"FB_DAMAGE_CLIPS", primary_fb_damage_clips);
		liftoff_layer_set_property(crtc->liftoff_composition_layer,
			"FB_DAMAGE_CLIPS", primary_fb_damage_clips);

		if (state->base->committed & WLR_OUTPUT_STATE_LAYERS) {
			for (size_t i = 0; i < state->base->layers_len; i++) {
				const struct wlr_output_layer_state *layer_state = &state->base->layers[i];
				ok = ok && set_layer_props(drm, layer_state, i + 1,
					&fb_damage_clips_arr);
			}
		}

		if (crtc->cursor) {
			if (drm_connector_is_cursor_visible(conn)) {
				ok = ok && set_plane_props(crtc->cursor, crtc->cursor->liftoff_layer,
					get_next_cursor_fb(conn), conn->cursor_x, conn->cursor_y,
					wl_list_length(&crtc->layers) + 1);
			} else {
				ok = ok && disable_plane(crtc->cursor);
			}
		}
	} else {
		ok = ok && disable_plane(crtc->primary);
		if (crtc->cursor) {
			ok = ok && disable_plane(crtc->cursor);
		}
	}

	if (!ok) {
		goto out;
	}

	int ret = liftoff_output_apply(crtc->liftoff, req, flags);
	if (ret != 0) {
		wlr_drm_conn_log(conn, test_only ? WLR_DEBUG : WLR_ERROR,
			"liftoff_output_apply failed: %s", strerror(-ret));
		ok = false;
		goto out;
	}

	if (crtc->cursor &&
			liftoff_layer_needs_composition(crtc->cursor->liftoff_layer)) {
		wlr_drm_conn_log(conn, WLR_DEBUG, "Failed to scan-out cursor plane");
		ok = false;
		goto out;
	}

	ret = drmModeAtomicCommit(drm->fd, req, flags, drm);
	if (ret != 0) {
		wlr_drm_conn_log_errno(conn, test_only ? WLR_DEBUG : WLR_ERROR,
			"Atomic commit failed");
		ok = false;
		goto out;
	}

	if (state->base->committed & WLR_OUTPUT_STATE_LAYERS) {
		for (size_t i = 0; i < state->base->layers_len; i++) {
			struct wlr_output_layer_state *layer_state = &state->base->layers[i];
			struct wlr_drm_layer *layer = get_drm_layer(drm, layer_state->layer);
			layer_state->accepted =
				!liftoff_layer_needs_composition(layer->liftoff);
			if (!test_only && !layer_state->accepted) {
				update_layer_feedback(drm, layer);
			}
		}
	}

out:
	drmModeAtomicFree(req);

	if (ok && !test_only) {
		commit_blob(drm, &crtc->mode_id, mode_id);
		commit_blob(drm, &crtc->gamma_lut, gamma_lut);

		if (vrr_enabled != prev_vrr_enabled) {
			output->adaptive_sync_status = vrr_enabled ?
				WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED :
				WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
			wlr_drm_conn_log(conn, WLR_DEBUG, "VRR %s",
				vrr_enabled ? "enabled" : "disabled");
		}
	} else {
		rollback_blob(drm, &crtc->mode_id, mode_id);
		rollback_blob(drm, &crtc->gamma_lut, gamma_lut);
	}

	uint32_t *fb_damage_clips_ptr;
	wl_array_for_each(fb_damage_clips_ptr, &fb_damage_clips_arr) {
		if (drmModeDestroyPropertyBlob(drm->fd, *fb_damage_clips_ptr) != 0) {
			wlr_log_errno(WLR_ERROR, "Failed to destroy FB_DAMAGE_CLIPS property blob");
		}
	}

	return ok;
}

const struct wlr_drm_interface liftoff_iface = {
	.init = init,
	.finish = finish,
	.crtc_commit = crtc_commit,
};
