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

	// Antialiasing stands on its own: it has no neutral setting, so switching
	// it on must run the pass even with grading off and everything neutral.
	{
		PcPostEffects fx;
		fx.fxaa = true;
		check(pc_post_any_enabled(fx), "FXAA alone runs the pass");
		check(!pc_post_needs_depth(fx), "FXAA does not need depth");

		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform vec4 uTexelSize"), "FXAA declares its texel size");
		check(contains(src, "fxaaFilter"), "FXAA emits its filter");
		// The early return is what keeps the cost down on the flat majority of
		// a frame. Losing it would be invisible in a screenshot and expensive.
		check(contains(src, "return rgbM;"), "FXAA keeps its flat-area early out");
		check(!contains(src, "uGamma"), "FXAA alone brings no grading uniforms");
	}

	// Off means the filter is not in the shader at all, not that it is compiled
	// in and skipped by a branch.
	{
		PcPostEffects fx;
		fx.colourGrading = true;
		fx.gamma = 1.2f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(!contains(src, "fxaaFilter"), "grading alone brings no FXAA code");
		check(!contains(src, "uTexelSize"), "grading alone brings no texel size");
	}

	// Both together: the scene is filtered first and the tone curve applied to
	// the result, so grading must not read the raw texture when FXAA is on.
	{
		PcPostEffects fx;
		fx.fxaa = true;
		fx.colourGrading = true;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "vec3 c = fxaaFilter(vUV, uTexelSize.xy);"),
		      "with FXAA on, grading works from the filtered colour");
		check(!contains(src, "vec3 c = texture(uScene, vUV).rgb;"),
		      "the unfiltered fetch is gone when FXAA is on");
	}

	// Recompilation is driven by equality, so the new field has to be in it.
	{
		PcPostEffects a, b;
		b.fxaa = true;
		check(a != b, "antialiasing is part of the comparison");
	}

	// Bloom is the one effect that costs three extra passes, so "on" alone is
	// not enough to run it -- it has to actually contribute something.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.0f;
		check(!pc_post_bloom_active(fx), "bloom at zero intensity is not active");
		check(!pc_post_any_enabled(fx), "bloom at zero intensity runs no pass");
		fx.bloomIntensity = 0.6f;
		check(pc_post_bloom_active(fx), "bloom with intensity is active");
		check(pc_post_any_enabled(fx), "active bloom runs the pass");
		fx.bloom = false;
		check(!pc_post_bloom_active(fx), "switching bloom off overrides its intensity");
	}

	// The composite must be added and not mixed: bloom is light that scattered
	// on the way to the lens, so it arrives on top of the image rather than
	// replacing part of it.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform sampler2D uBloom"), "bloom declares its sampler");
		check(contains(src, "c += texture(uBloom, vUV).rgb * uBloomIntensity;"),
		      "bloom is added, not mixed");

		PcPostEffects off;
		const std::string bare = pc_post_build_fragment_shader(off);
		check(!contains(bare, "uBloom"), "no bloom means no bloom sampler");
	}

	// Order matters: grading is the tone curve applied to the light reaching
	// the sensor, and bloom is part of that light, so it must come first.
	{
		PcPostEffects fx;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		fx.colourGrading = true;
		fx.gamma = 1.2f;
		const std::string src = pc_post_build_fragment_shader(fx);
		const size_t bloomAt = src.find("uBloomIntensity");
		const size_t gradeAt = src.find("1.0 / uGamma");
		check(bloomAt != std::string::npos && gradeAt != std::string::npos,
		      "both effects are present");
		check(bloomAt < gradeAt, "bloom is composited before grading");
	}

	// The two helper stages are separate programs and must stand alone.
	{
		const std::string bright = pc_post_build_brightpass_shader();
		const std::string blur   = pc_post_build_blur_shader();
		for (const std::string* src : { &bright, &blur }) {
			check(src->rfind("#version", 0) == 0, "a helper stage begins with its version");
			check(contains(*src, "void main"), "a helper stage has an entry point");
			check(contains(*src, "oColour"), "a helper stage writes its output");
		}
		check(contains(bright, "uThreshold"), "the bright pass takes a threshold");
		// A hard cut makes bloom pop in and out as something drifts across the
		// threshold; weighting by how far past it went keeps that smooth.
		check(contains(bright, "excess / luma"), "the bright pass fades in rather than cutting");
		check(contains(blur, "uBlurStep"), "the blur takes a direction");
		check(contains(blur, "uSource"), "the blur reads its own source, not the scene");
	}

	// Recompilation is driven by equality, so every bloom field has to be in it.
	{
		PcPostEffects a, b;
		b.bloom = true;
		check(a != b, "the bloom switch is part of the comparison");
		b = a; b.bloomIntensity = 0.5f;
		check(a != b, "bloom intensity is part of the comparison");
		b = a; b.bloomThreshold = 0.5f;
		check(a != b, "bloom threshold is part of the comparison");
	}

	// Occlusion is the first effect that cannot run without a depth texture,
	// and the port falls back to a renderbuffer on drivers that refuse one.
	// Saying so has to be answerable before the pass is set up.
	{
		PcPostEffects fx;
		check(!pc_post_needs_depth(fx), "an empty effect set needs no depth");
		fx.ssao = true;
		fx.ssaoIntensity = 0.8f;
		check(pc_post_ssao_active(fx), "occlusion with intensity is active");
		check(pc_post_needs_depth(fx), "occlusion needs depth");
		check(pc_post_any_enabled(fx), "active occlusion runs the pass");

		fx.ssaoIntensity = 0.0f;
		check(!pc_post_ssao_active(fx), "occlusion at zero intensity is not active");
		check(!pc_post_needs_depth(fx), "inactive occlusion does not demand depth");
		fx.ssaoIntensity = 0.8f;
		fx.ssaoRadius = 0.0f;
		check(!pc_post_ssao_active(fx), "occlusion with no radius is not active");
	}

	// Multiplied, and before bloom: occlusion is ambient light that never
	// arrived, so it scales what the surface received; bloom is light that did
	// arrive and then scattered, so it lands on top of the result.
	{
		PcPostEffects fx;
		fx.ssao = true;
		fx.ssaoIntensity = 0.8f;
		fx.bloom = true;
		fx.bloomIntensity = 0.6f;
		const std::string src = pc_post_build_fragment_shader(fx);
		check(contains(src, "uniform sampler2D uAO"), "occlusion declares its sampler");
		check(contains(src, "c *= texture(uAO, vUV).r;"), "occlusion multiplies");
		const size_t aoAt = src.find("uAO, vUV");
		const size_t bloomAt = src.find("uBloom, vUV");
		check(aoAt != std::string::npos && bloomAt != std::string::npos, "both are present");
		check(aoAt < bloomAt, "occlusion is applied before bloom");

		PcPostEffects off;
		check(!contains(pc_post_build_fragment_shader(off), "uAO"),
		      "no occlusion means no occlusion sampler");
	}

	// The occlusion stage rebuilds position and normal from depth alone, and
	// the depth range is the GameCube's. The same mistake made the fog
	// invisible once; here it would make occlusion cover everything or nothing.
	{
		const std::string src = pc_post_build_ssao_shader();
		check(src.rfind("#version", 0) == 0, "the occlusion stage begins with its version");
		check(contains(src, "void main"), "the occlusion stage has an entry point");
		check(contains(src, "n - ndc * (f - n)"),
		      "occlusion linearises with the GameCube depth range");
		check(!contains(src, "f + n - ndc"),
		      "occlusion does not use the OpenGL depth range");
		check(contains(src, "uProjInfo"), "occlusion takes the projection terms");
		check(contains(src, "dFdx(P)") && contains(src, "dFdy(P)"),
		      "the normal comes from screen-space derivatives");
		// Without the flip the normal's sign follows the quad winding and half
		// the screen occludes backwards.
		check(contains(src, "if (N.z < 0.0) N = -N;"), "the normal is forced to face the camera");
		// Without the range check a depth discontinuity reads as an occluder
		// and every silhouette gets a black halo.
		check(contains(src, "smoothstep"), "occlusion range-checks its samples");
		check(contains(src, "kKernel"), "occlusion uses a baked sample kernel");
	}

	// Recompilation is driven by equality.
	{
		PcPostEffects a, b;
		b.ssao = true;
		check(a != b, "the occlusion switch is part of the comparison");
		b = a; b.ssaoIntensity = 0.5f;
		check(a != b, "occlusion intensity is part of the comparison");
		b = a; b.ssaoRadius = 10.0f;
		check(a != b, "occlusion radius is part of the comparison");
	}

	if (failures == 0) std::printf("pc_postprocess_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
