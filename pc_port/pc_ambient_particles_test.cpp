// Ambient motes are released on a budget rather than per frame, and released
// into a shape rather than a box. Both of those are easy to get subtly wrong in
// ways that only show as "it looks odd when I turn" or "it thickens at high
// frame rates", so they are pinned here.
#include "pc_ambient_particles.h"

#include <cmath>
#include <cstdio>

namespace {
int failures = 0;

void check(bool ok, const char* what)
{
	if (!ok) {
		std::printf("FAIL: %s\n", what);
		failures++;
	}
}

// Runs a whole second at a given frame rate and counts what came out.
int releasedOverASecond(int fps)
{
	float buffer[3 * 64];
	int total = 0;
	const float dt = 1.0f / float(fps);
	for (int i = 0; i < fps; i++) {
		total += pc_ambient_tick(dt, 64, buffer);
	}
	return total;
}
}

int main()
{
	float buffer[3 * 64];

	// Off is off, and must cost nothing to ask.
	{
		pc_ambient_set_density(0);
		pc_ambient_reset();
		check(pc_ambient_tick(1.0f, 64, buffer) == 0, "density zero releases nothing");
		check(releasedOverASecond(60) == 0, "density zero stays silent for a whole second");
	}

	// The whole point of a per-second budget: the same amount of atmosphere
	// whether the game is running at 30 or at 120.
	{
		pc_ambient_set_density(2);
		pc_ambient_reset();
		const int at30 = releasedOverASecond(30);
		pc_ambient_reset();
		const int at60 = releasedOverASecond(60);
		pc_ambient_reset();
		const int at120 = releasedOverASecond(120);
		check(at30 > 0, "something is released at 30 fps");
		check(std::abs(at30 - at60) <= 1, "the rate does not change between 30 and 60 fps");
		check(std::abs(at60 - at120) <= 1, "the rate does not change between 60 and 120 fps");
	}

	// Denser settings release more.
	{
		pc_ambient_reset();
		pc_ambient_set_density(1);
		const int sparse = releasedOverASecond(60);
		pc_ambient_set_density(3);
		pc_ambient_reset();
		const int dense = releasedOverASecond(60);
		check(dense > sparse, "a denser setting releases more");
	}

	// A stall must not bank spawns and then dump them. This is what a loading
	// screen looks like from here.
	{
		pc_ambient_set_density(3);
		pc_ambient_reset();
		const int afterStall = pc_ambient_tick(30.0f, 64, buffer);
		check(afterStall <= 4, "a long stall does not release a cloud");
	}

	// Changing density mid-play must not carry the old budget across.
	{
		pc_ambient_set_density(3);
		pc_ambient_tick(0.9f, 64, buffer);
		pc_ambient_set_density(1);
		const int justAfter = pc_ambient_tick(0.001f, 64, buffer);
		check(justAfter == 0, "changing density drops the accumulated budget");
	}

	// Positions land around the focus, in a round column rather than a box: a
	// box lets the corners reach half again as far as the sides, which reads as
	// motes appearing in diagonal clumps when the camera turns.
	{
		pc_ambient_set_density(3);
		pc_ambient_reset();
		pc_ambient_set_focus(1000.0f, 200.0f, -500.0f);
		float maxPlanar = 0.0f;
		float minHeight = 1e9f, maxHeight = -1e9f;
		bool everyHeightAbove = true;
		for (int i = 0; i < 400; i++) {
			const int n = pc_ambient_tick(0.05f, 64, buffer);
			for (int k = 0; k < n; k++) {
				const float dx = buffer[k * 3 + 0] - 1000.0f;
				const float dy = buffer[k * 3 + 1] - 200.0f;
				const float dz = buffer[k * 3 + 2] + 500.0f;
				const float planar = std::sqrt(dx * dx + dz * dz);
				if (planar > maxPlanar) maxPlanar = planar;
				if (dy < minHeight) minHeight = dy;
				if (dy > maxHeight) maxHeight = dy;
				if (dy <= 0.0f) everyHeightAbove = false;
			}
		}
		check(maxPlanar > 100.0f, "motes spread out around the focus");
		check(maxPlanar <= 700.0f + 1.0f, "the spread is a circle, not a square");
		check(everyHeightAbove, "motes stay above the focus point");
		check(minHeight >= 40.0f - 0.01f, "motes do not hug the ground");
		check(maxHeight <= 420.0f + 0.01f, "motes stay within the band");
	}

	// Never write past what the caller offered.
	{
		pc_ambient_set_density(3);
		pc_ambient_reset();
		const int n = pc_ambient_tick(30.0f, 2, buffer);
		check(n <= 2, "the caller's limit is respected");
		check(pc_ambient_tick(1.0f, 0, buffer) == 0, "a zero limit releases nothing");
		check(pc_ambient_tick(1.0f, 8, nullptr) == 0, "a null buffer releases nothing");
	}

	if (failures == 0) std::printf("pc_ambient_particles_test: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
