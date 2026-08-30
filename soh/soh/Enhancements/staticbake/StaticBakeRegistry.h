#ifndef SOH_STATIC_BAKE_REGISTRY_H
#define SOH_STATIC_BAKE_REGISTRY_H

#include "z64.h"

#ifdef __cplusplus
extern "C" {
#endif

// Offer a freshly initialised compiled-in room's opaque display lists to libultraship's static
// mesh cache (sturdy-bassoon#40 Stage 1). Inert unless SOH_STATIC_BAKE=1 is in the environment.
//
// Called from the compiled-in branch of the room load, which is reached only by scenes this fork
// defines in C. That is the whole of the vanilla-safety argument: the cache is keyed by display
// list pointer, which is only sound for addresses that are stable C symbols, and a vanilla scene's
// display lists never reach this function so they can never be registered.
void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx);

#ifdef __cplusplus
}
#endif

#endif // SOH_STATIC_BAKE_REGISTRY_H
