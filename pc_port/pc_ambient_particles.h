#ifndef PC_AMBIENT_PARTICLES_H
#define PC_AMBIENT_PARTICLES_H

/*
 * Ambient particles: motes drifting through the air around the player.
 *
 * The first attempt released whole effects from the game's catalogue, and it
 * looked exactly like what it was -- gameplay feedback going off for no reason.
 * Every one of those 335 effects was authored to be noticed.
 *
 * These are individual particles instead, through the simple-particle path in
 * zen::simplePtclManager: a texture, a lifetime, a velocity and an acceleration.
 * That path is alive and updated every frame; only the inline wrapper in
 * particleManager was marked unused, which is what made it look missing.
 *
 * The module decides what to release and how it should move. The caller does
 * the spawning, since it is the part that can reach the effect manager. That
 * keeps the timing, the placement and the motion testable with no stage loaded.
 */

#ifdef __cplusplus
extern "C" {
#endif

/// One mote, fully described. Velocities and accelerations are per update, and
/// the lifetime is in updates, because that is how the particle manager
/// integrates them.
struct PcAmbientMote {
	float x, y, z;
	float vx, vy, vz;
	float ax, ay, az;
	float size;
	float rotSpeed;
	short lifeFrames;
};

/// 0 off, 1 sparse, 2 normal, 3 dense.
void pc_ambient_set_density(int density);
int pc_ambient_get_density(void);

/// Where the player is looking, so motes are released around that rather than
/// around the origin. Called once per tick before pc_ambient_tick.
void pc_ambient_set_focus(float x, float y, float z);

/**
 * @brief Decides what to release this tick.
 *
 * Writes up to maxMotes descriptions into outMotes and returns how many were
 * written. Returns zero when the mod is off, which is the common case.
 *
 * @param deltaSeconds Frame time. Spawning is per second rather than per frame
 *                     so the air does not thicken with the frame rate.
 */
int pc_ambient_tick(float deltaSeconds, int maxMotes, struct PcAmbientMote* outMotes);

/// Drops the accumulated spawn budget. Called when a stage ends, so a long
/// loading screen does not release a whole cloud on the first frame after it.
void pc_ambient_reset(void);

#ifdef __cplusplus
}
#endif

#endif // PC_AMBIENT_PARTICLES_H
