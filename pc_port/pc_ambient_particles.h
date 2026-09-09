#ifndef PC_AMBIENT_PARTICLES_H
#define PC_AMBIENT_PARTICLES_H

/*
 * Ambient particles: motes drifting through the air around the player.
 *
 * This is not a new renderer. The game already has a particle system with 335
 * catalogued effects, and spawning one at a position is a call it makes all the
 * time. What is missing is anything that spawns them for no reason -- every
 * existing effect is attached to an event.
 *
 * So this module owns the decision, not the drawing: how many motes to release
 * this tick and where to put them. The caller, which is the part that can reach
 * the effect manager, does the spawning. That split keeps the timing and the
 * placement testable without a stage loaded, and it means a mistake here cannot
 * do anything worse than ask for the wrong number.
 */

#ifdef __cplusplus
extern "C" {
#endif

/// 0 off, 1 sparse, 2 normal, 3 dense.
void pc_ambient_set_density(int density);
int pc_ambient_get_density(void);

/// Where the player is looking, so motes are released around that rather than
/// around the origin. Called once per tick before pc_ambient_tick.
void pc_ambient_set_focus(float x, float y, float z);

/**
 * @brief Decides what to release this tick.
 *
 * Writes up to maxPositions triples of x,y,z into outPositions and returns how
 * many were written. Returns zero when the mod is off, which is the common case
 * and costs nothing.
 *
 * @param deltaSeconds Frame time. Spawning is per second rather than per frame
 *                     so the effect does not thicken with the frame rate.
 */
int pc_ambient_tick(float deltaSeconds, int maxPositions, float* outPositions);

/// Drops the accumulated spawn budget. Called when a stage ends, so a long
/// loading screen does not release a whole cloud on the first frame after it.
void pc_ambient_reset(void);

#ifdef __cplusplus
}
#endif

#endif // PC_AMBIENT_PARTICLES_H
