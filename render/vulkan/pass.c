#include <assert.h>
#include <drm_fourcc.h>
#include <stdlib.h>
#include <wlr/util/log.h>

#include "render/vulkan.h"
#include "types/wlr_matrix.h"

static const struct wlr_render_pass_impl render_pass_impl;

static struct wlr_vk_render_pass *get_render_pass(struct wlr_render_pass *wlr_pass) {
	assert(wlr_pass->impl == &render_pass_impl);
	struct wlr_vk_render_pass *pass = wl_container_of(wlr_pass, pass, base);
	return pass;
}

static void bind_pipeline(struct wlr_vk_render_pass *pass, VkPipeline pipeline) {
	if (pipeline == pass->bound_pipeline) {
		return;
	}

	vkCmdBindPipeline(pass->command_buffer->vk, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	pass->bound_pipeline = pipeline;
}

static void get_clip_region(struct wlr_vk_render_pass *pass,
		const pixman_region32_t *in, pixman_region32_t *out) {
	if (in != NULL) {
		pixman_region32_init(out);
		pixman_region32_copy(out, in);
	} else {
		struct wlr_buffer *buffer = pass->render_buffer->wlr_buffer;
		pixman_region32_init_rect(out, 0, 0, buffer->width, buffer->height);
	}
}

static void convert_pixman_box_to_vk_rect(const pixman_box32_t *box, VkRect2D *rect) {
	*rect = (VkRect2D){
		.offset = { .x = box->x1, .y = box->y1 },
		.extent = { .width = box->x2 - box->x1, .height = box->y2 - box->y1 },
	};
}

static float color_to_linear(float non_linear) {
	// See https://www.w3.org/Graphics/Color/srgb
	return (non_linear > 0.04045) ?
		pow((non_linear + 0.055) / 1.055, 2.4) :
		non_linear / 12.92;
}

static void mat3_to_mat4(const float mat3[9], float mat4[4][4]) {
	memset(mat4, 0, sizeof(float) * 16);
	mat4[0][0] = mat3[0];
	mat4[0][1] = mat3[1];
	mat4[0][3] = mat3[2];

	mat4[1][0] = mat3[3];
	mat4[1][1] = mat3[4];
	mat4[1][3] = mat3[5];

	mat4[2][2] = 1.f;
	mat4[3][3] = 1.f;
}

static bool render_pass_submit(struct wlr_render_pass *wlr_pass) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	struct wlr_vk_renderer *renderer = pass->renderer;
	struct wlr_vk_command_buffer *render_cb = pass->command_buffer;
	struct wlr_vk_render_buffer *render_buffer = pass->render_buffer;
	struct wlr_vk_command_buffer *stage_cb = NULL;
	VkSemaphoreSubmitInfoKHR *render_wait = NULL;
	bool device_lost = false;

	if (pass->failed) {
		goto error;
	}

	if (vulkan_record_stage_cb(renderer) == VK_NULL_HANDLE) {
		goto error;
	}

	stage_cb = renderer->stage.cb;
	assert(stage_cb != NULL);
	renderer->stage.cb = NULL;

	if (render_buffer->blend_image) {
		// Apply output shader to map blend image to actual output image
		vkCmdNextSubpass(render_cb->vk, VK_SUBPASS_CONTENTS_INLINE);

		int width = pass->render_buffer->wlr_buffer->width;
		int height = pass->render_buffer->wlr_buffer->height;

		float final_matrix[9] = {
			width, 0, -1,
			0, height, -1,
			0, 0, 0,
		};
		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		mat3_to_mat4(final_matrix, vert_pcr_data.mat4);

		bind_pipeline(pass, render_buffer->render_setup->output_pipe);
		vkCmdPushConstants(render_cb->vk, renderer->output_pipe_layout,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
		vkCmdBindDescriptorSets(render_cb->vk,
			VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->output_pipe_layout,
			0, 1, &render_buffer->blend_descriptor_set, 0, NULL);

		const pixman_region32_t *clip = rect_union_evaluate(&pass->updated_region);
		int clip_rects_len;
		const pixman_box32_t *clip_rects = pixman_region32_rectangles(
			clip, &clip_rects_len);
		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(render_cb->vk, 0, 1, &rect);
			vkCmdDraw(render_cb->vk, 4, 1, 0, 0);
		}
	}

	vkCmdEndRenderPass(render_cb->vk);

	// insert acquire and release barriers for dmabuf-images
	uint32_t barrier_count = wl_list_length(&renderer->foreign_textures) + 1;
	VkImageMemoryBarrier *acquire_barriers = calloc(barrier_count, sizeof(*acquire_barriers));
	VkImageMemoryBarrier *release_barriers = calloc(barrier_count, sizeof(*release_barriers));
	render_wait = calloc(barrier_count * WLR_DMABUF_MAX_PLANES, sizeof(*render_wait));
	if (acquire_barriers == NULL || release_barriers == NULL || render_wait == NULL) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		free(acquire_barriers);
		free(release_barriers);
		free(render_wait);
		goto error;
	}

	struct wlr_vk_texture *texture, *tmp_tex;
	size_t idx = 0;
	uint32_t render_wait_len = 0;
	wl_list_for_each_safe(texture, tmp_tex, &renderer->foreign_textures, foreign_link) {
		VkImageLayout src_layout = VK_IMAGE_LAYOUT_GENERAL;
		if (!texture->transitioned) {
			src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			texture->transitioned = true;
		}

		// acquire
		acquire_barriers[idx] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.dstQueueFamilyIndex = renderer->dev->queue_family,
			.image = texture->image,
			.oldLayout = src_layout,
			.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.srcAccessMask = 0, // ignored anyways
			.dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1,
		};

		// release
		release_barriers[idx] = (VkImageMemoryBarrier){
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = renderer->dev->queue_family,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.image = texture->image,
			.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = 0, // ignored anyways
			.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.subresourceRange.layerCount = 1,
			.subresourceRange.levelCount = 1,
		};

		++idx;

		if (!vulkan_sync_foreign_texture(texture)) {
			wlr_log(WLR_ERROR, "Failed to wait for foreign texture DMA-BUF fence");
		} else {
			for (size_t i = 0; i < WLR_DMABUF_MAX_PLANES; i++) {
				if (texture->foreign_semaphores[i] != VK_NULL_HANDLE) {
					assert(render_wait_len < barrier_count * WLR_DMABUF_MAX_PLANES);
					render_wait[render_wait_len++] = (VkSemaphoreSubmitInfoKHR){
						.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
						.semaphore = texture->foreign_semaphores[i],
						.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
					};
				}
			}
		}

		wl_list_remove(&texture->foreign_link);
		texture->owned = false;
	}

	// also add acquire/release barriers for the current render buffer
	VkImageLayout src_layout = VK_IMAGE_LAYOUT_GENERAL;
	if (!render_buffer->transitioned) {
		src_layout = VK_IMAGE_LAYOUT_PREINITIALIZED;
		render_buffer->transitioned = true;
	}

	if (render_buffer->blend_image) {
		// The render pass changes the blend image layout from
		// color attachment to read only, so on each frame, before
		// the render pass starts, we change it back
		VkImageLayout blend_src_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		if (!render_buffer->blend_transitioned) {
			blend_src_layout = VK_IMAGE_LAYOUT_UNDEFINED;
			render_buffer->blend_transitioned = true;
		}

		VkImageMemoryBarrier blend_acq_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
			.image = render_buffer->blend_image,
			.oldLayout = blend_src_layout,
			.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
			.srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
			.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.layerCount = 1,
				.levelCount = 1,
			},
		};
		vkCmdPipelineBarrier(stage_cb->vk, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
			VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
			0, 0, NULL, 0, NULL, 1, &blend_acq_barrier);
	}

	// acquire render buffer before rendering
	acquire_barriers[idx] = (VkImageMemoryBarrier){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.dstQueueFamilyIndex = renderer->dev->queue_family,
		.image = render_buffer->image,
		.oldLayout = src_layout,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcAccessMask = 0, // ignored anyways
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
	};

	// release render buffer after rendering
	release_barriers[idx] = (VkImageMemoryBarrier){
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
		.srcQueueFamilyIndex = renderer->dev->queue_family,
		.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
		.image = render_buffer->image,
		.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
		.newLayout = VK_IMAGE_LAYOUT_GENERAL,
		.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
			VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
		.dstAccessMask = 0, // ignored anyways
		.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
		.subresourceRange.layerCount = 1,
		.subresourceRange.levelCount = 1,
	};

	++idx;

	vkCmdPipelineBarrier(stage_cb->vk, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
		VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		0, 0, NULL, 0, NULL, barrier_count, acquire_barriers);

	vkCmdPipelineBarrier(render_cb->vk, VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT,
		VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL,
		barrier_count, release_barriers);

	free(acquire_barriers);
	free(release_barriers);

	// No semaphores needed here.
	// We don't need a semaphore from the stage/transfer submission
	// to the render submissions since they are on the same queue
	// and we have a renderpass dependency for that.
	uint64_t stage_timeline_point = vulkan_end_command_buffer(stage_cb, renderer);
	if (stage_timeline_point == 0) {
		goto error;
	}

	VkCommandBufferSubmitInfoKHR stage_cb_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
		.commandBuffer = stage_cb->vk,
	};
	VkSemaphoreSubmitInfoKHR stage_signal = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
		.semaphore = renderer->timeline_semaphore,
		.value = stage_timeline_point,
	};
	VkSubmitInfo2KHR stage_submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &stage_cb_info,
		.signalSemaphoreInfoCount = 1,
		.pSignalSemaphoreInfos = &stage_signal,
	};

	VkSemaphoreSubmitInfoKHR stage_wait;
	if (renderer->stage.last_timeline_point > 0) {
		stage_wait = (VkSemaphoreSubmitInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
			.semaphore = renderer->timeline_semaphore,
			.value = renderer->stage.last_timeline_point,
			.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
		};

		stage_submit.waitSemaphoreInfoCount = 1;
		stage_submit.pWaitSemaphoreInfos = &stage_wait;
	}

	renderer->stage.last_timeline_point = stage_timeline_point;

	uint64_t render_timeline_point = vulkan_end_command_buffer(render_cb, renderer);
	if (render_timeline_point == 0) {
		goto error;
	}

	uint32_t render_signal_len = 1;
	VkSemaphoreSubmitInfoKHR render_signal[2] = {0};
	render_signal[0] = (VkSemaphoreSubmitInfoKHR){
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
		.semaphore = renderer->timeline_semaphore,
		.value = render_timeline_point,
	};
	if (renderer->dev->implicit_sync_interop) {
		if (render_cb->binary_semaphore == VK_NULL_HANDLE) {
			VkExportSemaphoreCreateInfo export_info = {
				.sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
				.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
			};
			VkSemaphoreCreateInfo semaphore_info = {
				.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
				.pNext = &export_info,
			};
			VkResult res = vkCreateSemaphore(renderer->dev->dev, &semaphore_info,
				NULL, &render_cb->binary_semaphore);
			if (res != VK_SUCCESS) {
				wlr_vk_error("vkCreateSemaphore", res);
				goto error;
			}
		}

		render_signal[render_signal_len++] = (VkSemaphoreSubmitInfoKHR){
			.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO_KHR,
			.semaphore = render_cb->binary_semaphore,
		};
	}

	VkCommandBufferSubmitInfoKHR render_cb_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO_KHR,
		.commandBuffer = render_cb->vk,
	};
	VkSubmitInfo2KHR render_submit = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2_KHR,
		.waitSemaphoreInfoCount = render_wait_len,
		.pWaitSemaphoreInfos = render_wait,
		.commandBufferInfoCount = 1,
		.pCommandBufferInfos = &render_cb_info,
		.signalSemaphoreInfoCount = render_signal_len,
		.pSignalSemaphoreInfos = render_signal,
	};

	VkSubmitInfo2KHR submit_info[] = { stage_submit, render_submit };
	VkResult res = renderer->dev->api.vkQueueSubmit2KHR(renderer->dev->queue, 2, submit_info, VK_NULL_HANDLE);

	if (res != VK_SUCCESS) {
		device_lost = res == VK_ERROR_DEVICE_LOST;
		wlr_vk_error("vkQueueSubmit", res);
		goto error;
	}

	free(render_wait);

	struct wlr_vk_shared_buffer *stage_buf, *stage_buf_tmp;
	wl_list_for_each_safe(stage_buf, stage_buf_tmp, &renderer->stage.buffers, link) {
		if (stage_buf->allocs.size == 0) {
			continue;
		}
		wl_list_remove(&stage_buf->link);
		wl_list_insert(&stage_cb->stage_buffers, &stage_buf->link);
	}

	if (!vulkan_sync_render_buffer(renderer, render_buffer, render_cb)) {
		wlr_log(WLR_ERROR, "Failed to sync render buffer");
	}

	wlr_buffer_unlock(render_buffer->wlr_buffer);
	rect_union_finish(&pass->updated_region);
	free(pass);
	return true;

error:
	free(render_wait);
	vulkan_reset_command_buffer(stage_cb);
	vulkan_reset_command_buffer(render_cb);
	wlr_buffer_unlock(render_buffer->wlr_buffer);
	rect_union_finish(&pass->updated_region);
	free(pass);

	if (device_lost) {
		wl_signal_emit_mutable(&renderer->wlr_renderer.events.lost, NULL);
	}

	return false;
}

static void render_pass_mark_box_updated(struct wlr_vk_render_pass *pass,
		const struct wlr_box *box) {
	if (!pass->render_buffer->blend_image) {
		return;
	}

	pixman_box32_t pixman_box = {
		.x1 = box->x,
		.x2 = box->x + box->width,
		.y1 = box->y,
		.y2 = box->y + box->height,
	};
	rect_union_add(&pass->updated_region, pixman_box);
}

static void render_pass_add_rect(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_rect_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	VkCommandBuffer cb = pass->command_buffer->vk;

	// Input color values are given in sRGB space, shader expects
	// them in linear space. The shader does all computation in linear
	// space and expects in inputs in linear space since it outputs
	// colors in linear space as well (and vulkan then automatically
	// does the conversion for out sRGB render targets).
	float linear_color[] = {
		color_to_linear(options->color.r),
		color_to_linear(options->color.g),
		color_to_linear(options->color.b),
		options->color.a, // no conversion for alpha
	};

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	// Record regions possibly updated for use in second subpass
	for (int i = 0; i < clip_rects_len; i++) {
		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &options->box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	struct wlr_box box;
	wlr_render_rect_options_get_box(options, pass->render_buffer->wlr_buffer, &box);

	switch (options->blend_mode) {
	case WLR_RENDER_BLEND_MODE_PREMULTIPLIED:;
		float proj[9], matrix[9];
		wlr_matrix_identity(proj);
		wlr_matrix_project_box(matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL, 0, proj);
		wlr_matrix_multiply(matrix, pass->projection, matrix);

		struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
			pass->render_buffer->render_setup,
			&(struct wlr_vk_pipeline_key) {
				.source = WLR_VK_SHADER_SOURCE_SINGLE_COLOR,
				.layout = { .ycbcr_format = NULL },
			});
		if (!pipe) {
			pass->failed = true;
			break;
		}

		struct wlr_vk_vert_pcr_data vert_pcr_data = {
			.uv_off = { 0, 0 },
			.uv_size = { 1, 1 },
		};
		mat3_to_mat4(matrix, vert_pcr_data.mat4);

		bind_pipeline(pass, pipe->vk);
		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
		vkCmdPushConstants(cb, pipe->layout->vk,
			VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vert_pcr_data), sizeof(float) * 4,
			linear_color);

		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(cb, 0, 1, &rect);
			vkCmdDraw(cb, 4, 1, 0, 0);
		}
		break;
	case WLR_RENDER_BLEND_MODE_NONE:;
		VkClearAttachment clear_att = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.colorAttachment = 0,
			.clearValue.color.float32 = {
				linear_color[0],
				linear_color[1],
				linear_color[2],
				linear_color[3],
			},
		};
		VkClearRect clear_rect = {
			.rect = {
				.offset = { box.x, box.y },
				.extent = { box.width, box.height },
			},
			.layerCount = 1,
		};
		for (int i = 0; i < clip_rects_len; i++) {
			VkRect2D rect;
			convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
			vkCmdSetScissor(cb, 0, 1, &rect);
			vkCmdClearAttachments(cb, 1, &clear_att, 1, &clear_rect);
		}
		break;
	}

	pixman_region32_fini(&clip);
}

static void render_pass_add_texture(struct wlr_render_pass *wlr_pass,
		const struct wlr_render_texture_options *options) {
	struct wlr_vk_render_pass *pass = get_render_pass(wlr_pass);
	struct wlr_vk_renderer *renderer = pass->renderer;
	struct wlr_vk_render_buffer *render_buffer = pass->render_buffer;
	VkCommandBuffer cb = pass->command_buffer->vk;

	struct wlr_vk_texture *texture = vulkan_get_texture(options->texture);
	assert(texture->renderer == renderer);

	if (texture->dmabuf_imported && !texture->owned) {
		// Store this texture in the list of textures that need to be
		// acquired before rendering and released after rendering.
		// We don't do it here immediately since barriers inside
		// a renderpass are suboptimal (would require additional renderpass
		// dependency and potentially multiple barriers) and it's
		// better to issue one barrier for all used textures anyways.
		texture->owned = true;
		assert(texture->foreign_link.prev == NULL);
		assert(texture->foreign_link.next == NULL);
		wl_list_insert(&renderer->foreign_textures, &texture->foreign_link);
	}

	struct wlr_fbox src_box;
	wlr_render_texture_options_get_src_box(options, &src_box);
	struct wlr_box dst_box;
	wlr_render_texture_options_get_dst_box(options, &dst_box);
	float alpha = wlr_render_texture_options_get_alpha(options);

	pixman_region32_t clip;
	get_clip_region(pass, options->clip, &clip);

	float proj[9], matrix[9];
	wlr_matrix_identity(proj);
	wlr_matrix_project_box(matrix, &dst_box, options->transform, 0, proj);
	wlr_matrix_multiply(matrix, pass->projection, matrix);

	struct wlr_vk_vert_pcr_data vert_pcr_data = {
		.uv_off = {
			src_box.x / options->texture->width,
			src_box.y / options->texture->height,
		},
		.uv_size = {
			src_box.width / options->texture->width,
			src_box.height / options->texture->height,
		},
	};
	mat3_to_mat4(matrix, vert_pcr_data.mat4);

	struct wlr_vk_pipeline *pipe = setup_get_or_create_pipeline(
		render_buffer->render_setup,
		&(struct wlr_vk_pipeline_key) {
			.source = WLR_VK_SHADER_SOURCE_TEXTURE,
			.layout = {
				.ycbcr_format = texture->format->is_ycbcr ? texture->format : NULL,
				.filter_mode = options->filter_mode,
			},
			.texture_transform = texture->transform,
			.blend_mode = !texture->has_alpha && alpha == 1.0 ?
				WLR_RENDER_BLEND_MODE_NONE : options->blend_mode,
		});
	if (!pipe) {
		pass->failed = true;
		return;
	}

	struct wlr_vk_texture_view *view =
		vulkan_texture_get_or_create_view(texture, pipe->layout);
	if (!view) {
		pass->failed = true;
		return;
	}

	bind_pipeline(pass, pipe->vk);

	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
		pipe->layout->vk, 0, 1, &view->ds, 0, NULL);

	vkCmdPushConstants(cb, pipe->layout->vk,
		VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(vert_pcr_data), &vert_pcr_data);
	vkCmdPushConstants(cb, pipe->layout->vk,
		VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(vert_pcr_data), sizeof(float),
		&alpha);

	int clip_rects_len;
	const pixman_box32_t *clip_rects = pixman_region32_rectangles(&clip, &clip_rects_len);
	for (int i = 0; i < clip_rects_len; i++) {
		VkRect2D rect;
		convert_pixman_box_to_vk_rect(&clip_rects[i], &rect);
		vkCmdSetScissor(cb, 0, 1, &rect);
		vkCmdDraw(cb, 4, 1, 0, 0);

		struct wlr_box clip_box = {
			.x = clip_rects[i].x1,
			.y = clip_rects[i].y1,
			.width = clip_rects[i].x2 - clip_rects[i].x1,
			.height = clip_rects[i].y2 - clip_rects[i].y1,
		};
		struct wlr_box intersection;
		if (!wlr_box_intersection(&intersection, &dst_box, &clip_box)) {
			continue;
		}
		render_pass_mark_box_updated(pass, &intersection);
	}

	texture->last_used_cb = pass->command_buffer;
}

static const struct wlr_render_pass_impl render_pass_impl = {
	.submit = render_pass_submit,
	.add_rect = render_pass_add_rect,
	.add_texture = render_pass_add_texture,
};

struct wlr_vk_render_pass *vulkan_begin_render_pass(struct wlr_vk_renderer *renderer,
		struct wlr_vk_render_buffer *buffer) {
	struct wlr_vk_render_pass *pass = calloc(1, sizeof(*pass));
	if (pass == NULL) {
		return NULL;
	}
	wlr_render_pass_init(&pass->base, &render_pass_impl);
	pass->renderer = renderer;

	rect_union_init(&pass->updated_region);

	struct wlr_vk_command_buffer *cb = vulkan_acquire_command_buffer(renderer);
	if (cb == NULL) {
		free(pass);
		return NULL;
	}

	VkCommandBufferBeginInfo begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
	};
	VkResult res = vkBeginCommandBuffer(cb->vk, &begin_info);
	if (res != VK_SUCCESS) {
		wlr_vk_error("vkBeginCommandBuffer", res);
		vulkan_reset_command_buffer(cb);
		free(pass);
		return NULL;
	}

	int width = buffer->wlr_buffer->width;
	int height = buffer->wlr_buffer->height;
	VkRect2D rect = { .extent = { width, height } };

	VkRenderPassBeginInfo rp_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderArea = rect,
		.renderPass = buffer->render_setup->render_pass,
		.framebuffer = buffer->framebuffer,
		.clearValueCount = 0,
	};
	vkCmdBeginRenderPass(cb->vk, &rp_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdSetViewport(cb->vk, 0, 1, &(VkViewport){
		.width = width,
		.height = height,
		.maxDepth = 1,
	});

	// matrix_projection() assumes a GL coordinate system so we need
	// to pass WL_OUTPUT_TRANSFORM_FLIPPED_180 to adjust it for vulkan.
	matrix_projection(pass->projection, width, height, WL_OUTPUT_TRANSFORM_FLIPPED_180);

	wlr_buffer_lock(buffer->wlr_buffer);
	pass->render_buffer = buffer;
	pass->command_buffer = cb;
	return pass;
}
