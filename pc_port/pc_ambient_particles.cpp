#include "pc_ambient_particles.h"

#include <cmath>
#include <cstdint>

namespace {

// Motes released per second at each density. The particle pool is shared with
// the game's own effects, which have the better claim on it, so even the dense
// setting stays modest -- these live for seconds, so the number alive at once
// is roughly the rate times the lifetime.
const float kRatePerSecond[4] = { 0.0f, 4.0f, 10.0f, 22.0f };

// The volume they appear in, relative to what the camera is looking at.
// Sideways reach is generous because the camera pans. The band sits above the
// ground: motes hugging the floor read as dirt on the lens rather than as
// something suspended in the air.
const float kSpreadXZ   = 750.0f;
const float kHeightLow  = 30.0f;
const float kHeightHigh = 400.0f;

// Per update, because that is how the particle manager integrates: it adds
// acceleration to velocity and velocity to position once per tick, with no
// regard for elapsed time. Slow enough to drift rather than fly.
const float kDriftXZ    = 0.55f;
const float kDriftUp    = 0.22f;
const float kDriftDown  = -0.12f;
// A gentle sideways pull curves the path instead of leaving it a straight line,
// which is most of what separates a drifting mote from a moving dot.
const float kSway       = 0.006f;

const short kLifeShortest = 210;
const short kLifeLongest  = 380;

const float kSizeSmall = 3.5f;
const float kSizeLarge = 8.0f;

// Whatever accumulates beyond this is dropped. A stall or a loading screen
// would otherwise bank several seconds of spawns and release them at once.
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

float lerp(float a, float b, float t) { return a + (b - a) * t; }

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

int pc_ambient_tick(float deltaSeconds, int maxMotes, struct PcAmbientMote* outMotes)
{
	if (sDensity <= 0 || maxMotes <= 0 || outMotes == nullptr) return 0;
	if (deltaSeconds <= 0.0f) return 0;
	// A single long frame must not turn into a burst.
	if (deltaSeconds > 0.25f) deltaSeconds = 0.25f;

	sBudget += kRatePerSecond[sDensity] * deltaSeconds;
	if (sBudget > kMaxBudget) sBudget = kMaxBudget;

	int count = 0;
	while (sBudget >= 1.0f && count < maxMotes) {
		sBudget -= 1.0f;
		PcAmbientMote& m = outMotes[count];

		// Round in plan rather than square: a box lets the corners reach half
		// again as far as the sides, and that shows up as motes appearing in
		// diagonal clumps as the camera turns.
		const float angle = nextUnit() * 6.2831853f;
		const float reach = std::sqrt(nextUnit()) * kSpreadXZ;
		m.x = sFocusX + std::cos(angle) * reach;
		m.y = sFocusY + lerp(kHeightLow, kHeightHigh, nextUnit());
		m.z = sFocusZ + std::sin(angle) * reach;

		m.vx = nextSigned() * kDriftXZ;
		m.vz = nextSigned() * kDriftXZ;
		// Mostly rising, but not all: a field where everything travels the same
		// way reads as an effect, and a still one reads as dirt on the screen.
		m.vy = lerp(kDriftDown, kDriftUp, nextUnit());

		// Sideways only. A vertical pull would build up over a long life and
		// turn the drift into a fall.
		m.ax = nextSigned() * kSway;
		m.ay = 0.0f;
		m.az = nextSigned() * kSway;

		m.size = lerp(kSizeSmall, kSizeLarge, nextUnit());
		m.rotSpeed = nextSigned() * 0.02f;
		m.lifeFrames = short(kLifeShortest + int(nextUnit() * float(kLifeLongest - kLifeShortest)));

		count++;
	}
	return count;
}

} // extern "C"
