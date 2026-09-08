#include "pc_postprocess.h"

bool pc_post_any_enabled(const PcPostEffects& fx)
{
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

	if (pc_post_needs_depth(fx)) {
		src += "uniform sampler2D uDepth;\n";
	}
	if (fx.colourGrading) {
		src += "uniform float uGamma;\n";
		src += "uniform float uBrightness;\n";
		src += "uniform float uSaturation;\n";
	}

	src += "void main() {\n";
	src += "    vec3 c = texture(uScene, vUV).rgb;\n";

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
