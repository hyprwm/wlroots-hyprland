#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput in_color;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

float linear_to_srgb(float x) {
	return max(min(x * 12.92, 0.04045), 1.055 * pow(x, 1. / 2.4) - 0.055);
}

void main() {
	vec4 val = subpassLoad(in_color).rgba;
	out_color = vec4(
		linear_to_srgb(val.r),
		linear_to_srgb(val.g),
		linear_to_srgb(val.b),
		val.a
	);
}

