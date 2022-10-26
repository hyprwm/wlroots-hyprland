#extension GL_OES_EGL_image_external : require

precision mediump float;
varying vec2 v_texcoord;
uniform samplerExternalOES texture0;
uniform float alpha;

void main() {
	gl_FragColor = texture2D(texture0, v_texcoord) * alpha;
}
