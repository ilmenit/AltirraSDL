// Body shared by OpenGL 3.3 and OpenGL ES 3.0; the runtime adds #version.
in vec2 vUV;
uniform sampler2D sourceTexture;
uniform vec4 sourceSize, outputSize, frame;
out vec4 outputColor;
void main() {
	vec4 c = texture(sourceTexture, vUV);
	if (frame.w > 0.5) c.rgb = c.rgb * (255.0 / 127.0) - vec3(64.0 / 127.0);
	c.rgb = vec3(1.0) - c.rgb;
	if (frame.w > 0.5) c.rgb = (c.rgb * 127.0 + vec3(64.0)) / 255.0;
	outputColor = c;
}
