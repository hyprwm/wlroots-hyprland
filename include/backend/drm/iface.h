#ifndef BACKEND_DRM_IFACE_H
#define BACKEND_DRM_IFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <pixman.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

struct wlr_drm_backend;
struct wlr_drm_connector;
struct wlr_drm_crtc;
struct wlr_drm_connector_state;
struct wlr_drm_fb;

// Used to provide atomic or legacy DRM functions
struct wlr_drm_interface {
	bool (*init)(struct wlr_drm_backend *drm);
	void (*finish)(struct wlr_drm_backend *drm);
	// Commit all pending changes on a CRTC.
	bool (*crtc_commit)(struct wlr_drm_connector *conn,
		const struct wlr_drm_connector_state *state, uint32_t flags,
		bool test_only);
};

extern const struct wlr_drm_interface atomic_iface;
extern const struct wlr_drm_interface legacy_iface;
extern const struct wlr_drm_interface liftoff_iface;

bool drm_legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
	struct wlr_drm_crtc *crtc, size_t size, uint16_t *lut);

bool create_mode_blob(struct wlr_drm_backend *drm,
	struct wlr_drm_connector *conn,
	const struct wlr_drm_connector_state *state, uint32_t *blob_id);
bool create_gamma_lut_blob(struct wlr_drm_backend *drm,
	size_t size, const uint16_t *lut, uint32_t *blob_id);
bool create_fb_damage_clips_blob(struct wlr_drm_backend *drm,
	int width, int height, const pixman_region32_t *damage, uint32_t *blob_id);

#endif
