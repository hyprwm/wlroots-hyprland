#version 450

layout(set = 0, binding = 0) uniform sampler2D tex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

layout(push_constant) uniform UBO {
	layout(offset = 80) float alpha;
} data;

layout (constant_id = 0) const int TEXTURE_TRANSFORM = 0;

#define TEXTURE_TRANSFORM_IDENTITY 0
#define TEXTURE_TRANSFORM_SRGB 1

float srgb_to_linear(float x) {
	return max(x / 12.92, pow((x + 0.055) / 1.055, 2.4));
}

void main() {
	vec4 val = textureLod(tex, uv, 0);
	if (TEXTURE_TRANSFORM == TEXTURE_TRANSFORM_SRGB) {
		out_color = vec4(
			srgb_to_linear(val.r),
			srgb_to_linear(val.g),
			srgb_to_linear(val.b),
			val.a
		);
	} else { // TEXTURE_TRANSFORM_IDENTITY
		out_color = val;
	}

	out_color *= data.alpha;
}

