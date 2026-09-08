// The post-process pass costs a fullscreen draw every frame, so the decision
// of whether to run it at all matters as much as what it does. Both that
// decision and the generated shader are checked here, without a GL context.
#include "pc_postprocess.h"

#include <cstdio>
#include <string>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

bool contains(const std::string& hay, const char* needle)
{
	return hay.find(needle) != std::string::npos;
}
}

int main()
{
	// Nothing on: the scene must blit straight through, untouched.
	{
		PcPostEffects fx;
		check(!pc_post_any_enabled(fx), "a default effect set runs no pass");
	}

	// On but neutral is the same picture. Spending a fullscreen draw to
	// reproduce the input exactly is the one case that is pure waste.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		check(!pc_post_any_enabled(fx), "neutral grading runs no pass");
		fx.gamma = 1.2f;
		check(pc_post_any_enabled(fx), "a non-neutral gamma runs the pass");
		fx.gamma = 1.0f;
		fx.brightness = 0.1f;
		check(pc_post_any_enabled(fx), "a non-neutral brightness runs the pass");
		fx.brightness = 0.0f;
		fx.saturation = 0.0f;
		check(pc_post_any_enabled(fx), "a non-neutral saturation runs the pass");
	}

	// Off overrides its own values: turning grading off must not leave the
	// pass running because a slider is still parked somewhere non-neutral.
	{
		PcPostEffects fx;
		fx.colourGrading = false;
		fx.gamma = 1.8f;
		fx.saturation = 0.0f;
		check(!pc_post_any_enabled(fx), "switching grading off stops the pass");
	}

	// The shader carries only what is switched on. An effect nobody asked for
	// must not cost instructions, which is the whole reason this is generated
	// rather than written once with uniforms deciding everything.
	{
		PcPostEffects off;
		const std::string bare = pc_post_build_fragment_shader(off);
		check(contains(bare, "uniform sampler2D uScene"), "the scene is always sampled");
		check(!contains(bare, "uGamma"), "grading uniforms are absent when it is off");
		check(!contains(bare, "uDepth"), "depth is not sampled when nothing needs it");

		PcPostEffects grade;
		grade.colourGrading = true;
		const std::string graded = pc_post_build_fragment_shader(grade);
		check(contains(graded, "uGamma"), "grading declares its gamma uniform");
		check(contains(graded, "uBrightness"), "grading declares its brightness uniform");
		check(contains(graded, "uSaturation"), "grading declares its saturation uniform");
		check(graded.size() > bare.size(), "enabling an effect adds to the shader");
	}

	// Both stages must be complete GLSL, since a failure to compile shows up as
	// a black screen rather than as an error the player can read.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		const std::string frag = pc_post_build_fragment_shader(fx);
		const std::string vert = pc_post_vertex_shader();
		for (const std::string* src : { &vert, &frag }) {
			check(src->rfind("#version", 0) == 0, "a stage begins with its version");
			check(contains(*src, "void main"), "a stage has an entry point");
			check(contains(*src, "}"), "a stage is closed");
		}
		check(contains(vert, "gl_VertexID"), "the pass builds its own geometry");
		check(contains(frag, "oColour"), "the fragment stage writes its output");
	}

	// Depth is not always available -- the port falls back to a renderbuffer
	// where a driver will not give it a depth texture -- so anything claiming
	// to need depth has to be answerable before the pass is set up.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		check(!pc_post_needs_depth(fx), "colour grading does not need depth");
	}

	// Equality drives shader recompilation. If it missed a field the pass would
	// keep running a shader built for different settings, and the menu would
	// look broken rather than the code.
	{
		PcPostEffects a, b;
		check(a == b, "identical sets compare equal");
		b.gamma = 1.5f;
		check(a != b, "gamma is part of the comparison");
		b = a; b.brightness = 0.2f;
		check(a != b, "brightness is part of the comparison");
		b = a; b.saturation = 0.5f;
		check(a != b, "saturation is part of the comparison");
		b = a; b.colourGrading = true;
		check(a != b, "the on/off switch is part of the comparison");
	}

	if (failures == 0) std::printf("pc_postprocess_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
