#ifndef RENDER_PIXEL_FORMAT_H
#define RENDER_PIXEL_FORMAT_H

#include <wayland-server-protocol.h>

/**
 * Information about a pixel format.
 *
 * A pixel format is identified via its DRM four character code (see <drm_fourcc.h>).
 */
struct wlr_pixel_format_info {
	uint32_t drm_format;

	/* Equivalent of the format if it has an alpha channel,
	 * DRM_FORMAT_INVALID (0) if NA
	 */
	uint32_t opaque_substitute;

	/* Bits per pixels */
	uint32_t bpp;

	/* True if the format has an alpha channel */
	bool has_alpha;
};

/**
 * Get pixel format information from a DRM FourCC.
 *
 * NULL is returned if the pixel format is unknown.
 */
const struct wlr_pixel_format_info *drm_get_pixel_format_info(uint32_t fmt);
/**
 * Check whether a stride is large enough for a given pixel format and width.
 */
bool pixel_format_info_check_stride(const struct wlr_pixel_format_info *info,
	int32_t stride, int32_t width);

/**
 * Convert an enum wl_shm_format to a DRM FourCC.
 */
uint32_t convert_wl_shm_format_to_drm(enum wl_shm_format fmt);
/**
 * Convert a DRM FourCC to an enum wl_shm_format.
 */
enum wl_shm_format convert_drm_format_to_wl_shm(uint32_t fmt);

#endif
