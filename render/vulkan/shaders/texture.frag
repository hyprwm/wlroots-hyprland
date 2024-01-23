#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform UBO {
	layout(offset = 80) float alpha;
} data;

layout (constant_id = 0) const int TEXTURE_TRANSFORM = 0;

// Matches enum wlr_vk_texture_transform
#define TEXTURE_TRANSFORM_IDENTITY 0
#define TEXTURE_TRANSFORM_SRGB 1

float srgb_channel_to_linear(float x) {
	return mix(x / 12.92,
		pow((x + 0.055) / 1.055, 2.4),
		x > 0.04045);
}

vec4 srgb_color_to_linear(vec4 color) {
	if (color.a == 0) {
		return vec4(0);
	}
	color.rgb /= color.a;
	color.rgb = vec3(
		srgb_channel_to_linear(color.r),
		srgb_channel_to_linear(color.g),
		srgb_channel_to_linear(color.b)
	);
	color.rgb *= color.a;
	return color;
}

void main() {
	vec4 val = textureLod(tex, uv, 0);
	if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_SRGB) {
		out_color = srgb_color_to_linear(val);
	} else { // TEXTURE_TRANSFORM_IDENTITY
		out_color = val;
	}

	out_color *= data.alpha;
}
