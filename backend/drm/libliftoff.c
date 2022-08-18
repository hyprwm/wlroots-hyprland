#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <libliftoff.h>
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

		liftoff_output_destroy(crtc->liftoff);
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
		int32_t x, int32_t y, uint64_t zpos) {
	struct wlr_drm_fb *fb = plane_get_next_fb(plane);
	if (fb == NULL) {
		wlr_log(WLR_ERROR, "Failed to acquire FB");
		return false;
	}

	uint32_t width = fb->wlr_buf->width;
	uint32_t height = fb->wlr_buf->height;

	// The SRC_* properties are in 16.16 fixed point
	struct liftoff_layer *layer = plane->liftoff_layer;
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

	uint32_t fb_damage_clips = 0;
	if ((state->base->committed & WLR_OUTPUT_STATE_DAMAGE) &&
			pixman_region32_not_empty((pixman_region32_t *)&state->base->damage) &&
			crtc->primary->props.fb_damage_clips != 0) {
		int rects_len;
		const pixman_box32_t *rects = pixman_region32_rectangles(
			(pixman_region32_t *)&state->base->damage, &rects_len);
		if (drmModeCreatePropertyBlob(drm->fd, rects,
				sizeof(*rects) * rects_len, &fb_damage_clips) != 0) {
			wlr_log_errno(WLR_ERROR, "Failed to create FB_DAMAGE_CLIPS property blob");
		}
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
		ok = ok && set_plane_props(crtc->primary, 0, 0, 0);
		liftoff_layer_set_property(crtc->primary->liftoff_layer,
			"FB_DAMAGE_CLIPS", fb_damage_clips);
		if (crtc->cursor) {
			if (drm_connector_is_cursor_visible(conn)) {
				ok = ok && set_plane_props(crtc->cursor,
					conn->cursor_x, conn->cursor_y, 1);
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

	if (liftoff_layer_needs_composition(crtc->primary->liftoff_layer)) {
		wlr_drm_conn_log(conn, WLR_DEBUG, "Failed to scan-out primary plane");
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

	if (fb_damage_clips != 0 &&
			drmModeDestroyPropertyBlob(drm->fd, fb_damage_clips) != 0) {
		wlr_log_errno(WLR_ERROR, "Failed to destroy FB_DAMAGE_CLIPS property blob");
	}

	return ok;
}

const struct wlr_drm_interface liftoff_iface = {
	.init = init,
	.finish = finish,
	.crtc_commit = crtc_commit,
};
