#version 450

layout (input_attachment_index = 0, binding = 0) uniform subpassInput in_color;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 out_color;

float linear_channel_to_srgb(float x) {
	return max(min(x * 12.92, 0.04045), 1.055 * pow(x, 1. / 2.4) - 0.055);
}

vec4 linear_color_to_srgb(vec4 color) {
	if (color.a == 0) {
		return vec4(0);
	}
	color.rgb /= color.a;
	color.rgb = vec3(
		linear_channel_to_srgb(color.r),
		linear_channel_to_srgb(color.g),
		linear_channel_to_srgb(color.b)
	);
	color.rgb *= color.a;
	return color;
}

void main() {
	vec4 val = subpassLoad(in_color).rgba;
	out_color = linear_color_to_srgb(val);
}
