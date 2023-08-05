#ifndef RENDER_VULKAN_H
#define RENDER_VULKAN_H

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <vulkan/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/interface.h>
#include <wlr/util/addon.h>
#include "util/rect_union.h"

struct wlr_vk_descriptor_pool;
struct wlr_vk_texture;

struct wlr_vk_instance {
	VkInstance instance;
	VkDebugUtilsMessengerEXT messenger;

	struct {
		PFN_vkCreateDebugUtilsMessengerEXT createDebugUtilsMessengerEXT;
		PFN_vkDestroyDebugUtilsMessengerEXT destroyDebugUtilsMessengerEXT;
	} api;
};

// Creates and initializes a vulkan instance.
// The debug parameter determines if validation layers are enabled and a
// debug messenger created.
struct wlr_vk_instance *vulkan_instance_create(bool debug);
void vulkan_instance_destroy(struct wlr_vk_instance *ini);

// Logical vulkan device state.
struct wlr_vk_device {
	struct wlr_vk_instance *instance;

	VkPhysicalDevice phdev;
	VkDevice dev;

	int drm_fd;

	bool implicit_sync_interop;
	bool sampler_ycbcr_conversion;

	// we only ever need one queue for rendering and transfer commands
	uint32_t queue_family;
	VkQueue queue;

	struct {
		PFN_vkGetMemoryFdPropertiesKHR vkGetMemoryFdPropertiesKHR;
		PFN_vkWaitSemaphoresKHR vkWaitSemaphoresKHR;
		PFN_vkGetSemaphoreCounterValueKHR vkGetSemaphoreCounterValueKHR;
		PFN_vkGetSemaphoreFdKHR vkGetSemaphoreFdKHR;
		PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFdKHR;
		PFN_vkQueueSubmit2KHR vkQueueSubmit2KHR;
	} api;

	uint32_t format_prop_count;
	struct wlr_vk_format_props *format_props;
	struct wlr_drm_format_set dmabuf_render_formats;
	struct wlr_drm_format_set dmabuf_texture_formats;

	// supported formats for textures (contains only those formats
	// that support everything we need for textures)
	uint32_t shm_format_count;
	uint32_t *shm_formats; // to implement vulkan_get_shm_texture_formats
};

// Tries to find the VkPhysicalDevice for the given drm fd.
// Might find none and return VK_NULL_HANDLE.
VkPhysicalDevice vulkan_find_drm_phdev(struct wlr_vk_instance *ini, int drm_fd);
int vulkan_open_phdev_drm_fd(VkPhysicalDevice phdev);

// Creates a device for the given instance and physical device.
struct wlr_vk_device *vulkan_device_create(struct wlr_vk_instance *ini,
	VkPhysicalDevice phdev);
void vulkan_device_destroy(struct wlr_vk_device *dev);

// Tries to find any memory bit for the given vulkan device that
// supports the given flags and is set in req_bits (e.g. if memory
// type 2 is ok, (req_bits & (1 << 2)) must not be 0.
// Set req_bits to 0xFFFFFFFF to allow all types.
int vulkan_find_mem_type(struct wlr_vk_device *device,
	VkMemoryPropertyFlags flags, uint32_t req_bits);

struct wlr_vk_format {
	uint32_t drm;
	VkFormat vk;
	bool is_srgb;
	bool is_ycbcr;
};

extern const VkImageUsageFlags vulkan_render_usage, vulkan_shm_tex_usage, vulkan_dma_tex_usage;

// Returns all known format mappings.
// Might not be supported for gpu/usecase.
const struct wlr_vk_format *vulkan_get_format_list(size_t *len);
const struct wlr_vk_format *vulkan_get_format_from_drm(uint32_t drm_format);

struct wlr_vk_format_modifier_props {
	VkDrmFormatModifierPropertiesEXT props;
	VkExtent2D max_extent;
};

struct wlr_vk_format_props {
	struct wlr_vk_format format;

	struct {
		VkExtent2D max_extent;
		VkFormatFeatureFlags features;
	} shm;

	struct {
		uint32_t render_mod_count;
		struct wlr_vk_format_modifier_props *render_mods;

		uint32_t texture_mod_count;
		struct wlr_vk_format_modifier_props *texture_mods;
	} dmabuf;
};

void vulkan_format_props_query(struct wlr_vk_device *dev,
	const struct wlr_vk_format *format);
const struct wlr_vk_format_modifier_props *vulkan_format_props_find_modifier(
	struct wlr_vk_format_props *props, uint64_t mod, bool render);
void vulkan_format_props_finish(struct wlr_vk_format_props *props);

struct wlr_vk_pipeline_layout_key {
	const struct wlr_vk_format *ycbcr_format;
	enum wlr_scale_filter_mode filter_mode;
};

struct wlr_vk_pipeline_layout {
	struct wlr_vk_pipeline_layout_key key;

	VkPipelineLayout vk;
	VkDescriptorSetLayout ds;
	VkSampler sampler;

	// for YCbCr pipelines only
	struct {
		VkSamplerYcbcrConversion conversion;
		VkFormat format;
	} ycbcr;

	struct wl_list link; // struct wlr_vk_renderer.pipeline_layouts
};

// Constants used to pick the color transform for the texture drawing
// fragment shader. Must match those in shaders/texture.frag
enum wlr_vk_texture_transform {
	WLR_VK_TEXTURE_TRANSFORM_IDENTITY = 0,
	WLR_VK_TEXTURE_TRANSFORM_SRGB = 1,
};

enum wlr_vk_shader_source {
	WLR_VK_SHADER_SOURCE_TEXTURE,
	WLR_VK_SHADER_SOURCE_SINGLE_COLOR,
};

struct wlr_vk_pipeline_key {
	struct wlr_vk_pipeline_layout_key layout;
	enum wlr_vk_shader_source source;
	enum wlr_render_blend_mode blend_mode;

	// only used if source is texture
	enum wlr_vk_texture_transform texture_transform;
};

struct wlr_vk_pipeline {
	struct wlr_vk_pipeline_key key;

	VkPipeline vk;
	const struct wlr_vk_pipeline_layout *layout;
	struct wlr_vk_render_format_setup *setup;
	struct wl_list link; // struct wlr_vk_render_format_setup
};

// For each format we want to render, we need a separate renderpass
// and therefore also separate pipelines.
struct wlr_vk_render_format_setup {
	struct wl_list link; // wlr_vk_renderer.render_format_setups
	const struct wlr_vk_format *render_format; // used in renderpass
	VkRenderPass render_pass;

	VkPipeline output_pipe;

	struct wlr_vk_renderer *renderer;
	struct wl_list pipelines; // struct wlr_vk_pipeline.link
};

// Renderer-internal represenation of an wlr_buffer imported for rendering.
struct wlr_vk_render_buffer {
	struct wlr_buffer *wlr_buffer;
	struct wlr_addon addon;
	struct wlr_vk_renderer *renderer;
	struct wlr_vk_render_format_setup *render_setup;
	struct wl_list link; // wlr_vk_renderer.buffers

	VkImage image;
	VkImageView image_view;
	VkFramebuffer framebuffer;
	uint32_t mem_count;
	VkDeviceMemory memories[WLR_DMABUF_MAX_PLANES];
	bool transitioned;

	VkImage blend_image;
	VkImageView blend_image_view;
	VkDeviceMemory blend_memory;
	VkDescriptorSet blend_descriptor_set;
	struct wlr_vk_descriptor_pool *blend_attachment_pool;
	bool blend_transitioned;
};

struct wlr_vk_command_buffer {
	VkCommandBuffer vk;
	bool recording;
	uint64_t timeline_point;
	// Textures to destroy after the command buffer completes
	struct wl_list destroy_textures; // wlr_vk_texture.destroy_link
	// Staging shared buffers to release after the command buffer completes
	struct wl_list stage_buffers; // wlr_vk_shared_buffer.link

	// For DMA-BUF implicit sync interop, may be NULL
	VkSemaphore binary_semaphore;
};

#define VULKAN_COMMAND_BUFFERS_CAP 64

// Vulkan wlr_renderer implementation on top of a wlr_vk_device.
struct wlr_vk_renderer {
	struct wlr_renderer wlr_renderer;
	struct wlr_backend *backend;
	struct wlr_vk_device *dev;

	VkCommandPool command_pool;

	VkShaderModule vert_module;
	VkShaderModule tex_frag_module;
	VkShaderModule quad_frag_module;
	VkShaderModule output_module;

	struct wl_list pipeline_layouts; // struct wlr_vk_pipeline_layout.link

	// for blend->output subpass
	VkPipelineLayout output_pipe_layout;
	VkDescriptorSetLayout output_ds_layout;
	size_t last_output_pool_size;
	struct wl_list output_descriptor_pools; // wlr_vk_descriptor_pool.link

	VkSemaphore timeline_semaphore;
	uint64_t timeline_point;

	struct wlr_vk_render_buffer *current_render_buffer;
	struct wlr_vk_command_buffer *current_command_buffer;

	VkRect2D scissor; // needed for clearing

	VkPipeline bound_pipe;

	uint32_t render_width;
	uint32_t render_height;
	float projection[9];

	size_t last_pool_size;
	struct wl_list descriptor_pools; // wlr_vk_descriptor_pool.link
	struct wl_list render_format_setups; // wlr_vk_render_format_setup.link


	struct wl_list textures; // wlr_vk_texture.link
	// Textures to return to foreign queue
	struct wl_list foreign_textures; // wlr_vk_texture.foreign_link

	struct wl_list render_buffers; // wlr_vk_render_buffer.link

	// Pool of command buffers
	struct wlr_vk_command_buffer command_buffers[VULKAN_COMMAND_BUFFERS_CAP];

	struct {
		struct wlr_vk_command_buffer *cb;
		uint64_t last_timeline_point;
		struct wl_list buffers; // wlr_vk_shared_buffer.link
	} stage;

	struct {
		bool initialized;
		uint32_t drm_format;
		uint32_t width, height;
		VkImage dst_image;
		VkDeviceMemory dst_img_memory;
	} read_pixels_cache;
};

// vertex shader push constant range data
struct wlr_vk_vert_pcr_data {
	float mat4[4][4];
	float uv_off[2];
	float uv_size[2];
};

struct wlr_vk_texture_view {
	struct wl_list link; // struct wlr_vk_texture.views
	const struct wlr_vk_pipeline_layout *layout;

	VkDescriptorSet ds;
	VkImageView image_view;
	struct wlr_vk_descriptor_pool *ds_pool;
};

struct wlr_vk_pipeline *setup_get_or_create_pipeline(
	struct wlr_vk_render_format_setup *setup,
	const struct wlr_vk_pipeline_key *key);
struct wlr_vk_pipeline_layout *get_or_create_pipeline_layout(
	struct wlr_vk_renderer *renderer,
	const struct wlr_vk_pipeline_layout_key *key);
struct wlr_vk_texture_view *vulkan_texture_get_or_create_view(
	struct wlr_vk_texture *texture,
	const struct wlr_vk_pipeline_layout *layout);

// Creates a vulkan renderer for the given device.
struct wlr_renderer *vulkan_renderer_create_for_device(struct wlr_vk_device *dev);

// stage utility - for uploading/retrieving data
// Gets an command buffer in recording state which is guaranteed to be
// executed before the next frame.
VkCommandBuffer vulkan_record_stage_cb(struct wlr_vk_renderer *renderer);

// Submits the current stage command buffer and waits until it has
// finished execution.
bool vulkan_submit_stage_wait(struct wlr_vk_renderer *renderer);

struct wlr_vk_render_pass {
	struct wlr_render_pass base;
	struct wlr_vk_renderer *renderer;
	struct wlr_vk_render_buffer *render_buffer;
	struct wlr_vk_command_buffer *command_buffer;
	struct rect_union updated_region;
	VkPipeline bound_pipeline;
	float projection[9];
	bool failed;
};

struct wlr_vk_render_pass *vulkan_begin_render_pass(struct wlr_vk_renderer *renderer,
	struct wlr_vk_render_buffer *buffer);

// Suballocates a buffer span with the given size that can be mapped
// and used as staging buffer. The allocation is implicitly released when the
// stage cb has finished execution. The start of the span will be a multiple
// of the given alignment.
struct wlr_vk_buffer_span vulkan_get_stage_span(
	struct wlr_vk_renderer *renderer, VkDeviceSize size,
	VkDeviceSize alignment);

// Tries to allocate a texture descriptor set. Will additionally
// return the pool it was allocated from when successful (for freeing it later).
struct wlr_vk_descriptor_pool *vulkan_alloc_texture_ds(
	struct wlr_vk_renderer *renderer, VkDescriptorSetLayout ds_layout,
	VkDescriptorSet *ds);

// Tries to allocate a descriptor set for the blending image. Will
// additionally return the pool it was allocated from when successful
// (for freeing it later).
struct wlr_vk_descriptor_pool *vulkan_alloc_blend_ds(
	struct wlr_vk_renderer *renderer, VkDescriptorSet *ds);

// Frees the given descriptor set from the pool its pool.
void vulkan_free_ds(struct wlr_vk_renderer *renderer,
	struct wlr_vk_descriptor_pool *pool, VkDescriptorSet ds);
struct wlr_vk_format_props *vulkan_format_props_from_drm(
	struct wlr_vk_device *dev, uint32_t drm_format);
struct wlr_vk_renderer *vulkan_get_renderer(struct wlr_renderer *r);

struct wlr_vk_command_buffer *vulkan_acquire_command_buffer(
	struct wlr_vk_renderer *renderer);
uint64_t vulkan_end_command_buffer(struct wlr_vk_command_buffer *cb,
	struct wlr_vk_renderer *renderer);
void vulkan_reset_command_buffer(struct wlr_vk_command_buffer *cb);
bool vulkan_wait_command_buffer(struct wlr_vk_command_buffer *cb,
	struct wlr_vk_renderer *renderer);

bool vulkan_sync_render_buffer(struct wlr_vk_renderer *renderer,
	struct wlr_vk_render_buffer *render_buffer, struct wlr_vk_command_buffer *cb);
bool vulkan_sync_foreign_texture(struct wlr_vk_texture *texture);

// State (e.g. image texture) associated with a surface.
struct wlr_vk_texture {
	struct wlr_texture wlr_texture;
	struct wlr_vk_renderer *renderer;
	uint32_t mem_count;
	VkDeviceMemory memories[WLR_DMABUF_MAX_PLANES];
	VkImage image;
	const struct wlr_vk_format *format;
	enum wlr_vk_texture_transform transform;
	struct wlr_vk_command_buffer *last_used_cb; // to track when it can be destroyed
	bool dmabuf_imported;
	bool owned; // if dmabuf_imported: whether we have ownership of the image
	bool transitioned; // if dma_imported: whether we transitioned it away from preinit
	bool has_alpha; // whether the image is has alpha channel
	struct wl_list foreign_link; // wlr_vk_renderer.foreign_textures
	struct wl_list destroy_link; // wlr_vk_command_buffer.destroy_textures
	struct wl_list link; // wlr_vk_renderer.textures

	// If imported from a wlr_buffer
	struct wlr_buffer *buffer;
	struct wlr_addon buffer_addon;
	// For DMA-BUF implicit sync interop
	VkSemaphore foreign_semaphores[WLR_DMABUF_MAX_PLANES];

	struct wl_list views; // struct wlr_vk_texture_ds.link
};

struct wlr_vk_texture *vulkan_get_texture(struct wlr_texture *wlr_texture);
VkImage vulkan_import_dmabuf(struct wlr_vk_renderer *renderer,
	const struct wlr_dmabuf_attributes *attribs,
	VkDeviceMemory mems[static WLR_DMABUF_MAX_PLANES], uint32_t *n_mems,
	bool for_render);
struct wlr_texture *vulkan_texture_from_buffer(
	struct wlr_renderer *wlr_renderer, struct wlr_buffer *buffer);
void vulkan_texture_destroy(struct wlr_vk_texture *texture);

struct wlr_vk_descriptor_pool {
	VkDescriptorPool pool;
	uint32_t free; // number of textures that can be allocated
	struct wl_list link; // wlr_vk_renderer.descriptor_pools
};

struct wlr_vk_allocation {
	VkDeviceSize start;
	VkDeviceSize size;
};

// List of suballocated staging buffers.
// Used to upload to/read from device local images.
struct wlr_vk_shared_buffer {
	struct wl_list link; // wlr_vk_renderer.stage.buffers or wlr_vk_command_buffer.stage_buffers
	VkBuffer buffer;
	VkDeviceMemory memory;
	VkDeviceSize buf_size;
	struct wl_array allocs; // struct wlr_vk_allocation
};

// Suballocated range on a buffer.
struct wlr_vk_buffer_span {
	struct wlr_vk_shared_buffer *buffer;
	struct wlr_vk_allocation alloc;
};

// util
const char *vulkan_strerror(VkResult err);
void vulkan_change_layout(VkCommandBuffer cb, VkImage img,
	VkImageLayout ol, VkPipelineStageFlags srcs, VkAccessFlags srca,
	VkImageLayout nl, VkPipelineStageFlags dsts, VkAccessFlags dsta);

#define wlr_vk_error(fmt, res, ...) wlr_log(WLR_ERROR, fmt ": %s (%d)", \
	vulkan_strerror(res), res, ##__VA_ARGS__)

#endif // RENDER_VULKAN_H
