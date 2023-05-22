uniform mat3 proj;
attribute vec2 pos;
attribute vec2 texcoord;
varying vec2 v_texcoord;

void main() {
	gl_Position = vec4(vec3(pos, 1.0) * proj, 1.0);
	v_texcoord = texcoord;
}
