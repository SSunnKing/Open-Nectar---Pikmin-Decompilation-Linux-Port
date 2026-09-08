#pragma once

#include <string>

// Screen-space post-processing.
//
// The port already renders the scene to its own framebuffer -- that is what
// the render scale setting drives -- and blits it to the window afterwards.
// This module owns what happens in between.
//
// Like pc_tev_shader, it only builds GLSL text and answers questions about the
// effect set. Every GL call lives in pc_gfx.cpp, where the entry points are.
// That keeps the interesting decisions -- which effects are on, what the shader
// should therefore contain, whether the pass is worth running at all -- testable
// without a window or a driver.

/// One post-process configuration. Effects are additive: the generated shader
/// contains exactly the ones switched on and nothing else, so a pass with two
/// effects costs two effects rather than the worst case.
struct PcPostEffects {
	// Colour grading. Cheap, needs only the scene colour, and it is the effect
	// that proves the whole path works end to end.
	bool colourGrading = false;
	float gamma        = 1.0f;   // 0.5 .. 2.0, 1.0 is untouched
	float brightness   = 0.0f;   // -0.5 .. 0.5, 0.0 is untouched
	float saturation   = 1.0f;   // 0.0 .. 2.0, 1.0 is untouched

	bool operator==(const PcPostEffects& o) const
	{
		return colourGrading == o.colourGrading && gamma == o.gamma
		    && brightness == o.brightness && saturation == o.saturation;
	}
	bool operator!=(const PcPostEffects& o) const { return !(*this == o); }
};

/**
 * @brief Whether the pass is worth running at all.
 *
 * With nothing switched on, the scene blits straight to the window exactly as
 * it did before this module existed. A pass that copies the frame to say
 * nothing about it is pure cost, and on the port's reference GPU that cost is
 * not free.
 */
bool pc_post_any_enabled(const PcPostEffects& fx);

/**
 * @brief True when the shader will sample the depth buffer.
 *
 * Depth is not always available: the port falls back to a renderbuffer where a
 * driver will not give it a depth texture, and a renderbuffer cannot be read.
 * Effects that need depth must be dropped in that case rather than assumed.
 */
bool pc_post_needs_depth(const PcPostEffects& fx);

/// Vertex shader for the fullscreen pass. Generates its own geometry from
/// gl_VertexID, so the pass needs no vertex buffer of its own.
const char* pc_post_vertex_shader();

/// Fragment shader containing only the effects that are switched on.
std::string pc_post_build_fragment_shader(const PcPostEffects& fx);
