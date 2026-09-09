#include "pc_ambient_particles.h"

#include <cmath>
#include <cstdint>

namespace {

// Motes released per second at each density. The generator pool holds 512 and
// each effect is short-lived, so even the dense setting leaves it most of its
// headroom for the game's own effects -- which matter more than this does.
const float kRatePerSecond[4] = { 0.0f, 3.0f, 8.0f, 18.0f };

// The volume they appear in, relative to what the camera is looking at.
// Sideways reach is generous because the camera pans; the vertical band sits
// above the ground, since motes hugging the floor read as dirt on the lens
// rather than as something in the air.
const float kSpreadXZ    = 700.0f;
const float kHeightLow   = 40.0f;
const float kHeightHigh  = 420.0f;

// Whatever accumulates beyond this is dropped. A stall or a loading screen
// would otherwise bank several seconds of spawns and release them all at once.
const float kMaxBudget = 4.0f;

int sDensity = 0;
float sBudget = 0.0f;
float sFocusX = 0.0f, sFocusY = 0.0f, sFocusZ = 0.0f;

// A small deterministic generator, kept here rather than using rand(): the game
// draws from that too, and quietly changing how often it is called would shift
// every other random decision in the frame.
std::uint32_t sRandomState = 0x9E3779B9u;

float nextUnit()
{
	sRandomState = sRandomState * 1664525u + 1013904223u;
	return float((sRandomState >> 8) & 0xFFFFFF) / float(0x1000000);
}

float nextSigned() { return nextUnit() * 2.0f - 1.0f; }

} // namespace

extern "C" {

void pc_ambient_set_density(int density)
{
	if (density < 0) density = 0;
	if (density > 3) density = 3;
	if (density != sDensity) {
		sDensity = density;
		sBudget = 0.0f;
	}
}

int pc_ambient_get_density(void) { return sDensity; }

void pc_ambient_set_focus(float x, float y, float z)
{
	sFocusX = x;
	sFocusY = y;
	sFocusZ = z;
}

void pc_ambient_reset(void) { sBudget = 0.0f; }

int pc_ambient_tick(float deltaSeconds, int maxPositions, float* outPositions)
{
	if (sDensity <= 0 || maxPositions <= 0 || outPositions == nullptr) return 0;
	if (deltaSeconds <= 0.0f) return 0;
	// A single long frame must not turn into a burst.
	if (deltaSeconds > 0.25f) deltaSeconds = 0.25f;

	sBudget += kRatePerSecond[sDensity] * deltaSeconds;
	if (sBudget > kMaxBudget) sBudget = kMaxBudget;

	int count = 0;
	while (sBudget >= 1.0f && count < maxPositions) {
		sBudget -= 1.0f;
		// Round in plan rather than square: a box lets the corners reach half
		// again as far as the sides, and that shows up as motes appearing in
		// diagonal clumps as the camera turns.
		const float angle = nextUnit() * 6.2831853f;
		const float reach = std::sqrt(nextUnit()) * kSpreadXZ;
		outPositions[count * 3 + 0] = sFocusX + std::cos(angle) * reach;
		outPositions[count * 3 + 1] = sFocusY + kHeightLow + nextUnit() * (kHeightHigh - kHeightLow);
		outPositions[count * 3 + 2] = sFocusZ + std::sin(angle) * reach;
		(void)nextSigned();
		count++;
	}
	return count;
}

} // extern "C"
