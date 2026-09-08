#include "pc_postprocess.h"

bool pc_post_any_enabled(const PcPostEffects& fx)
{
	if (fx.fxaa) return true;
	if (!fx.colourGrading) return false;
	// Switched on but set to neutral values is the same picture, so skip the
	// pass instead of spending a fullscreen draw reproducing the input.
	return fx.gamma != 1.0f || fx.brightness != 0.0f || fx.saturation != 1.0f;
}

bool pc_post_needs_depth(const PcPostEffects& fx)
{
	// Colour grading reads only the scene colour. Ambient occlusion, depth of
	// field and depth-based fog will change this when they arrive.
	(void)fx;
	return false;
}

const char* pc_post_vertex_shader()
{
	// One triangle covering the screen, built from the vertex index. A quad
	// would need a buffer and would rasterise its diagonal twice; this needs
	// neither a vertex buffer nor an attribute set, so the pass cannot be
	// disturbed by whatever vertex state the scene left behind.
	return "#version 330 core\n"
	       "out vec2 vUV;\n"
	       "void main() {\n"
	       "    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);\n"
	       "    vUV = p;\n"
	       "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	       "}\n";
}

std::string pc_post_build_fragment_shader(const PcPostEffects& fx)
{
	std::string src;
	src += "#version 330 core\n";
	src += "in vec2 vUV;\n";
	src += "out vec4 oColour;\n";
	src += "uniform sampler2D uScene;\n";
	if (fx.fxaa) {
		// xy is one texel, zw is the render size. Passed as a vec4 because the
		// port already loads glUniform4f and nothing else needed a vec2.
		src += "uniform vec4 uTexelSize;\n";
	}

	if (pc_post_needs_depth(fx)) {
		src += "uniform sampler2D uDepth;\n";
	}
	if (fx.colourGrading) {
		src += "uniform float uGamma;\n";
		src += "uniform float uBrightness;\n";
		src += "uniform float uSaturation;\n";
	}

	if (fx.fxaa) {
		// FXAA, Lottes' compact variant. Five taps decide whether the pixel is
		// on an edge at all and which way it runs; four more blend along it.
		//
		// The early return matters as much as the filter: most of a frame is
		// flat, and a pixel with no local contrast is left exactly as it was
		// rather than paying for four more samples to reproduce itself.
		src += "const vec3 kLuma = vec3(0.299, 0.587, 0.114);\n";
		src += "vec3 fxaaFilter(vec2 uv, vec2 rcp) {\n";
		src += "    vec3 rgbNW = texture(uScene, uv + vec2(-1.0, -1.0) * rcp).rgb;\n";
		src += "    vec3 rgbNE = texture(uScene, uv + vec2( 1.0, -1.0) * rcp).rgb;\n";
		src += "    vec3 rgbSW = texture(uScene, uv + vec2(-1.0,  1.0) * rcp).rgb;\n";
		src += "    vec3 rgbSE = texture(uScene, uv + vec2( 1.0,  1.0) * rcp).rgb;\n";
		src += "    vec3 rgbM  = texture(uScene, uv).rgb;\n";
		src += "    float lNW = dot(rgbNW, kLuma);\n";
		src += "    float lNE = dot(rgbNE, kLuma);\n";
		src += "    float lSW = dot(rgbSW, kLuma);\n";
		src += "    float lSE = dot(rgbSE, kLuma);\n";
		src += "    float lM  = dot(rgbM,  kLuma);\n";
		src += "    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n";
		src += "    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n";
		src += "    if (lMax - lMin < max(0.0312, lMax * 0.125)) return rgbM;\n";
		src += "    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)),\n";
		src += "                     ((lNW + lSW) - (lNE + lSE)));\n";
		// Without the reduction term a near-flat edge produces an enormous
		// direction and the filter smears across half the screen.
		src += "    float reduce = max((lNW + lNE + lSW + lSE) * 0.03125, 0.0078125);\n";
		src += "    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);\n";
		src += "    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * rcp;\n";
		src += "    vec3 rgbA = 0.5 * (texture(uScene, uv + dir * (1.0 / 3.0 - 0.5)).rgb\n";
		src += "                     + texture(uScene, uv + dir * (2.0 / 3.0 - 0.5)).rgb);\n";
		src += "    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uScene, uv + dir * -0.5).rgb\n";
		src += "                                   + texture(uScene, uv + dir *  0.5).rgb);\n";
		// The wider blend is better when it stays within the local range; when
		// it does not, it has reached past the edge and the narrow one is right.
		src += "    float lB = dot(rgbB, kLuma);\n";
		src += "    return (lB < lMin || lB > lMax) ? rgbA : rgbB;\n";
		src += "}\n";
	}

	src += "void main() {\n";
	if (fx.fxaa) {
		// Antialiasing first, grading afterwards: grading is a per-pixel tone
		// curve, so applying it to the resolved colour is the same as applying
		// it to each sample, and this way it costs one evaluation instead of
		// nine.
		src += "    vec3 c = fxaaFilter(vUV, uTexelSize.xy);\n";
	} else {
		src += "    vec3 c = texture(uScene, vUV).rgb;\n";
	}

	if (fx.colourGrading) {
		// Gamma first, on positive values only: pow() of a negative is
		// undefined, and a scene colour should never be negative but a driver
		// is not obliged to agree.
		src += "    c = pow(max(c, vec3(0.0)), vec3(1.0 / uGamma));\n";
		src += "    c += uBrightness;\n";
		// Rec. 709 luma, so desaturating keeps the apparent brightness rather
		// than dragging reds and blues down at different rates.
		src += "    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));\n";
		src += "    c = mix(vec3(luma), c, uSaturation);\n";
		src += "    c = clamp(c, 0.0, 1.0);\n";
	}

	src += "    oColour = vec4(c, 1.0);\n";
	src += "}\n";
	return src;
}
