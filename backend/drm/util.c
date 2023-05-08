#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <drm.h>
#include <libdisplay-info/cvt.h>
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "backend/drm/util.h"

int32_t calculate_refresh_rate(const drmModeModeInfo *mode) {
	int32_t refresh = (mode->clock * 1000000LL / mode->htotal +
		mode->vtotal / 2) / mode->vtotal;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE) {
		refresh *= 2;
	}

	if (mode->flags & DRM_MODE_FLAG_DBLSCAN) {
		refresh /= 2;
	}

	if (mode->vscan > 1) {
		refresh /= mode->vscan;
	}

	return refresh;
}

enum wlr_output_mode_aspect_ratio get_picture_aspect_ratio(const drmModeModeInfo *mode) {
	switch (mode->flags & DRM_MODE_FLAG_PIC_AR_MASK) {
	case DRM_MODE_FLAG_PIC_AR_NONE:
		return WLR_OUTPUT_MODE_ASPECT_RATIO_NONE;
	case DRM_MODE_FLAG_PIC_AR_4_3:
		return WLR_OUTPUT_MODE_ASPECT_RATIO_4_3;
	case DRM_MODE_FLAG_PIC_AR_16_9:
		return WLR_OUTPUT_MODE_ASPECT_RATIO_16_9;
	case DRM_MODE_FLAG_PIC_AR_64_27:
		return WLR_OUTPUT_MODE_ASPECT_RATIO_64_27;
	case DRM_MODE_FLAG_PIC_AR_256_135:
		return WLR_OUTPUT_MODE_ASPECT_RATIO_256_135;
	default:
		wlr_log(WLR_ERROR, "Unknown mode picture aspect ratio: %u",
			mode->flags & DRM_MODE_FLAG_PIC_AR_MASK);
		return WLR_OUTPUT_MODE_ASPECT_RATIO_NONE;
	}
}

void parse_edid(struct wlr_drm_connector *conn, size_t len, const uint8_t *data) {
	struct wlr_output *output = &conn->output;

	free(output->make);
	free(output->model);
	free(output->serial);
	output->make = NULL;
	output->model = NULL;
	output->serial = NULL;

	struct di_info *info = di_info_parse_edid(data, len);
	if (info == NULL) {
		wlr_log(WLR_ERROR, "Failed to parse EDID");
		return;
	}

	const struct di_edid *edid = di_info_get_edid(info);
	const struct di_edid_vendor_product *vendor_product = di_edid_get_vendor_product(edid);
	char pnp_id[] = {
		vendor_product->manufacturer[0],
		vendor_product->manufacturer[1],
		vendor_product->manufacturer[2],
		'\0',
	};
	const char *manu = get_pnp_manufacturer(vendor_product->manufacturer);
	if (!manu) {
		manu = pnp_id;
	}
	output->make = strdup(manu);

	output->model = di_info_get_model(info);
	output->serial = di_info_get_serial(info);

	di_info_destroy(info);
}

const char *drm_connector_status_str(drmModeConnection status) {
	switch (status) {
	case DRM_MODE_CONNECTED:
		return "connected";
	case DRM_MODE_DISCONNECTED:
		return "disconnected";
	case DRM_MODE_UNKNOWNCONNECTION:
		return "unknown";
	}
	return "<unsupported>";
}

static bool is_taken(size_t n, const uint32_t arr[static n], uint32_t key) {
	for (size_t i = 0; i < n; ++i) {
		if (arr[i] == key) {
			return true;
		}
	}
	return false;
}

/*
 * Store all of the non-recursive state in a struct, so we aren't literally
 * passing 12 arguments to a function.
 */
struct match_state {
	const size_t num_objs;
	const uint32_t *restrict objs;
	const size_t num_res;
	size_t score;
	size_t replaced;
	uint32_t *restrict res;
	uint32_t *restrict best;
	const uint32_t *restrict orig;
	bool exit_early;
};

/*
 * skips: The number of SKIP elements encountered so far.
 * score: The number of resources we've matched so far.
 * replaced: The number of changes from the original solution.
 * i: The index of the current element.
 *
 * This tries to match a solution as close to st->orig as it can.
 *
 * Returns whether we've set a new best element with this solution.
 */
static bool match_obj_(struct match_state *st, size_t skips, size_t score, size_t replaced, size_t i) {
	// Finished
	if (i >= st->num_res) {
		if (score > st->score ||
				(score == st->score && replaced < st->replaced)) {
			st->score = score;
			st->replaced = replaced;
			memcpy(st->best, st->res, sizeof(st->best[0]) * st->num_res);

			st->exit_early = (st->score == st->num_res - skips
					|| st->score == st->num_objs)
					&& st->replaced == 0;

			return true;
		} else {
			return false;
		}
	}

	if (st->orig[i] == SKIP) {
		st->res[i] = SKIP;
		return match_obj_(st, skips + 1, score, replaced, i + 1);
	}

	bool has_best = false;

	/*
	 * Attempt to use the current solution first, to try and avoid
	 * recalculating everything
	 */
	if (st->orig[i] != UNMATCHED && !is_taken(i, st->res, st->orig[i])) {
		st->res[i] = st->orig[i];
		size_t obj_score = st->objs[st->res[i]] != 0 ? 1 : 0;
		if (match_obj_(st, skips, score + obj_score, replaced, i + 1)) {
			has_best = true;
		}
	}
	if (st->orig[i] == UNMATCHED) {
		st->res[i] = UNMATCHED;
		if (match_obj_(st, skips, score, replaced, i + 1)) {
			has_best = true;
		}
	}
	if (st->exit_early) {
		return true;
	}

	if (st->orig[i] != UNMATCHED) {
		++replaced;
	}

	for (size_t candidate = 0; candidate < st->num_objs; ++candidate) {
		// We tried this earlier
		if (candidate == st->orig[i]) {
			continue;
		}

		// Not compatible
		if (!(st->objs[candidate] & (1 << i))) {
			continue;
		}

		// Already taken
		if (is_taken(i, st->res, candidate)) {
			continue;
		}

		st->res[i] = candidate;
		size_t obj_score = st->objs[candidate] != 0 ? 1 : 0;
		if (match_obj_(st, skips, score + obj_score, replaced, i + 1)) {
			has_best = true;
		}

		if (st->exit_early) {
			return true;
		}
	}

	if (has_best) {
		return true;
	}

	// Maybe this resource can't be matched
	st->res[i] = UNMATCHED;
	return match_obj_(st, skips, score, replaced, i + 1);
}

size_t match_obj(size_t num_objs, const uint32_t objs[static restrict num_objs],
		size_t num_res, const uint32_t res[static restrict num_res],
		uint32_t out[static restrict num_res]) {
	uint32_t solution[num_res];
	for (size_t i = 0; i < num_res; ++i) {
		solution[i] = UNMATCHED;
	}

	struct match_state st = {
		.num_objs = num_objs,
		.num_res = num_res,
		.score = 0,
		.replaced = SIZE_MAX,
		.objs = objs,
		.res = solution,
		.best = out,
		.orig = res,
		.exit_early = false,
	};

	match_obj_(&st, 0, 0, 0, 0);
	return st.score;
}

void generate_cvt_mode(drmModeModeInfo *mode, int hdisplay, int vdisplay,
		float vrefresh) {
	// TODO: depending on capabilities advertised in the EDID, use reduced
	// blanking if possible (and update sync polarity)
	struct di_cvt_options options = {
		.red_blank_ver = DI_CVT_REDUCED_BLANKING_NONE,
		.h_pixels = hdisplay,
		.v_lines = vdisplay,
		.ip_freq_rqd = vrefresh ? vrefresh : 60,
	};
	struct di_cvt_timing timing;
	di_cvt_compute(&timing, &options);

	uint16_t hsync_start = hdisplay + timing.h_front_porch;
	uint16_t vsync_start = timing.v_lines_rnd + timing.v_front_porch;
	uint16_t hsync_end = hsync_start + timing.h_sync;
	uint16_t vsync_end = vsync_start + timing.v_sync;

	*mode = (drmModeModeInfo){
		.clock = roundf(timing.act_pixel_freq * 1000),
		.hdisplay = hdisplay,
		.vdisplay = timing.v_lines_rnd,
		.hsync_start = hsync_start,
		.vsync_start = vsync_start,
		.hsync_end = hsync_end,
		.vsync_end = vsync_end,
		.htotal = hsync_end + timing.h_back_porch,
		.vtotal = vsync_end + timing.v_back_porch,
		.vrefresh = roundf(timing.act_frame_rate),
		.flags = DRM_MODE_FLAG_NHSYNC | DRM_MODE_FLAG_PVSYNC,
	};
	snprintf(mode->name, sizeof(mode->name), "%dx%d", hdisplay, vdisplay);
}
