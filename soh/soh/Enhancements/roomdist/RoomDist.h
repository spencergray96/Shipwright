#ifndef SOH_ENHANCEMENTS_ROOM_DIST_H
#define SOH_ENHANCEMENTS_ROOM_DIST_H

/*
 * Distance-based room selection - the Exp 4 prototype for sturdy-bassoon#6 (Option D, trigger half).
 *
 * OoT changes rooms when Link walks through an En_Holl transition actor: an invisible plane, placed
 * by the scene author, that calls Room_RequestNewRoom when he is inside a narrow band in front of
 * it. That works, but the planes are *data*: a full 6x6 chunk split needs roughly 420 of them and
 * TransitionActorContext counts them in a u8, so the mechanism runs out of room before the map
 * does. This is the alternative: pick the room whose centre is nearest Link and drive the engine's
 * own Room_RequestNewRoom / Room_FinishRoomChange path when it differs from the current one. One
 * distance comparison per room per tick, no scene data at all.
 *
 * Where the room centres come from: the scene's static-collision bounding box, cut into an N x N
 * row-major grid, N = sqrt(numRooms). That is exactly the split tools/terrain's Blender back-end
 * produces (`--rooms N`; room_blocks() in gen_blender_scene.py, rows north to south then columns
 * west to east), so for the fixtures this experiment measures the derived centres are the real
 * ones. It is a prototype's stand-in for a table a real Option D generator would emit alongside the
 * rooms - the trigger itself does not care where the centres came from, and arming refuses any
 * scene whose room count is not a perfect square rather than guessing.
 *
 * Hysteresis: with none, the nearest-centre test flips exactly on the perpendicular bisector
 * between two centres, so a Link standing on a boundary can flap between rooms every tick. The
 * `hysteresis` argument is how much closer (in world units) the candidate has to be before the
 * change is taken; the switch then happens hysteresis/2 units past the boundary, because moving a
 * unit past the bisector changes the difference of the two distances by two units.
 *
 * Everything here is inert until `agenttest roomdist` arms it, and `gRoomDistOn` is the only thing
 * the engine tests while it is off (one byte, read once per En_Holl update).
 */

#include <stdint.h>

struct PlayState;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The hot-path gate. 0 = the engine runs exactly the code it ran before this file existed.
 * While it is 1, EnHoll_Update stands down: the two triggers are measured one at a time rather
 * than fighting over the same room context.
 */
extern uint8_t gRoomDistOn;

/* `agenttest roomdist <hysteresis>`. Returns 0 on success, nonzero if the current scene's rooms
 * cannot be laid out on a square grid (or there is no scene). */
int32_t RoomDist_Configure(float hysteresis);
void RoomDist_Disable(void);

/* One-line description of the live configuration, for the marker and the command's output. */
const char* RoomDist_Describe(void);

/*
 * Pops the most recent trigger event, if any, into `buf` (a printf-ready fragment: what it did,
 * where Link was, and the two distances that decided it). Returns 1 if an event was written.
 * The agent-test hook polls this once per tick and puts it on the marker channel, so this file
 * needs no file I/O of its own.
 */
int32_t RoomDist_TakeEvent(char* buf, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif /* SOH_ENHANCEMENTS_ROOM_DIST_H */
