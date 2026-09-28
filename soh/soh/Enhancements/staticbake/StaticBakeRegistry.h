#ifndef SOH_STATIC_BAKE_REGISTRY_H
#define SOH_STATIC_BAKE_REGISTRY_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Offer a freshly initialised compiled-in room's opaque display lists to libultraship's static
// mesh cache (sturdy-bassoon#40 Stage 1). Always registers; whether they are baked is the switch
// below.
//
// Called from the compiled-in branch of the room load, which is reached only by scenes this fork
// defines in C. That is the whole of the vanilla-safety argument: the cache is keyed by display
// list pointer, which is only sound for addresses that are stable C symbols, and a vanilla scene's
// display lists never reach this function so they can never be registered.
void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx);

// The bake's runtime switch (sturdy-bassoon#142). On: registered rooms are recorded once and replayed
// from the GPU. Off: every room is interpreted, and existing bakes are kept, so switching back on
// replays them without a re-record. Starts on when SOH_STATIC_BAKE=1 is in the environment, off
// otherwise. Safe to call between frames, from any game-thread code - the `staticbake` console
// command is one caller, and a menu or CVar setting (sturdy-bassoon#153) is meant to be the next.
void StaticBake_SetActive(int active);
int StaticBake_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_STATIC_BAKE_REGISTRY_H
