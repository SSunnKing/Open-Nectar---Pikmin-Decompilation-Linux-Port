#ifndef PC_TICK_PROFILER_H
#define PC_TICK_PROFILER_H

// CPU cost of one logical tick, split into the parts that would have to run
// twice as often for 60 Hz gameplay.
//
// The GPU side is already measured (PIKMIN_PERF_STATS) and is comfortable at
// 1080p. The open question for 60 FPS gameplay is the CPU: a tick has to fit
// in 16.6 ms, and nothing so far says whether it does. This answers that.
//
// It reports the worst case as prominently as the mean. A mean of 9 ms with a
// p99 of 24 ms is not a 60 Hz tick; it is a 60 Hz tick that stutters, which is
// worse to play than an honest 30.
//
// Off unless PIKMIN_TICK_STATS=1. The accumulator holds no clock of its own so
// it can be tested without one: call sites time the span and hand over the
// milliseconds.

#include <cstdint>
#include <string>

enum PcTickRegion {
	kPcTickUpdate = 0,     // game logic: AI, physics, navigation
	kPcTickRenderAll,      // building the frame's GX display lists
	kPcTickDoneRender,     // submitting them and presenting
	kPcTickWhole,          // the three above end to end, per tick

	// Inside renderall, where the cost turned out to be. Per frame, summed
	// over every GX primitive: the port emits one draw per primitive, each
	// preceded by ~150 uniform writes.
	kPcTickGfxUniforms,    // ms/frame writing uniforms
	kPcTickGfxVbo,         // ms/frame uploading vertices
	kPcTickGfxDraw,        // ms/frame in glDrawArrays itself
	kPcTickGfxDrawCount,   // draws per frame — a count, not milliseconds
	kPcTickGfxVertsPerDraw,// mean vertices per draw — a count, not milliseconds

	// PERF-NATIVE-002 step 1: how much consecutive work actually shares state.
	// Batching only pays if draws arrive in runs; these say how long the runs
	// are and what breaks them. Counts, not milliseconds.
	kPcTickGfxPrimCount,   // GX primitives per frame, i.e. draws before batching
	kPcTickGfxRunLength,   // mean primitives per batch (draws per run of identical state)
	kPcTickGfxRunLongest,  // longest such run in the frame
	kPcTickGfxGlBreakPct,  // % of run breaks caused by GL pipeline state, not material

	// Display-list faults per frame, split by severity because they are not
	// the same thing. A desync (unsupported opcode, truncated vertex stream)
	// abandons the rest of the list and can draw garbage; an out-of-range
	// PNMTXIDX is a per-vertex warning the parser recovers from, and a model
	// carrying one logs thousands per frame while rendering correctly.
	kPcTickGxDlDesync,     // fatal: parser lost the stream
	kPcTickGxBadMtxIdx,    // benign: PNMTXIDX out of range, per vertex

	// Vertices whose position is not a finite, plausible coordinate. Reading
	// from a stale or freed vertex array yields exactly this, and it draws as
	// triangles stretching off screen -- 3D wrecked, 2D overlays untouched.
	kPcTickGxWildVerts,

	kPcTickRegionCount,
};

const char* pc_tick_region_name(PcTickRegion region);

// Reads PIKMIN_TICK_STATS once. Call sites test this before timing anything so
// the profiler costs nothing when it is off.
bool pc_tick_profiler_enabled();

void pc_tick_profiler_record(PcTickRegion region, double milliseconds);
void pc_tick_profiler_reset();

struct PcTickStats {
	uint32_t samples = 0;
	double mean      = 0.0;
	double median    = 0.0;
	double p95       = 0.0;
	double p99       = 0.0;
	double worst     = 0.0;
	// Share of samples that did not fit the budget handed to the query, 0..1.
	double overBudget = 0.0;
};

// budgetMs is what a tick is allowed to cost: 16.6 for 60 Hz, 33.3 for 30 Hz.
PcTickStats pc_tick_profiler_stats(PcTickRegion region, double budgetMs);

// One block of text for the console, including the verdict against budgetMs.
std::string pc_tick_profiler_report(double budgetMs);

#endif // PC_TICK_PROFILER_H
