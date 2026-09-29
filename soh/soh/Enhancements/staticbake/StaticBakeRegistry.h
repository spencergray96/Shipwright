#ifndef SOH_STATIC_BAKE_REGISTRY_H
#define SOH_STATIC_BAKE_REGISTRY_H

#include "z64.h"
#include "soh/cvar_prefixes.h"

// The saved setting behind the switch (sturdy-bassoon#153): 1 bakes, 0 interprets. On by default.
// Written by the Settings > Graphics checkbox and StaticBake_SetSetting (the human `staticbake on|off`);
// SOH_STATIC_BAKE=0|1 in the environment overrides it for the whole session.
#define CVAR_STATIC_BAKE CVAR_SETTING("StaticBake")
#define STATIC_BAKE_DEFAULT 1

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

// The bake's runtime switch (sturdy-bassoon#142, #153). On: registered rooms are recorded once and
// replayed from the GPU. Off: every room is interpreted, and existing bakes are kept, so switching
// back on replays them without a re-record. ON by default: it starts from SOH_STATIC_BAKE when that
// is 0 or 1, and otherwise from CVAR_STATIC_BAKE (default 1). Safe to call between frames, from any
// game-thread code.
//   StaticBake_SetActive   moves the switch for this session only (`agenttest staticbake on|off`)
//   StaticBake_SetSetting  saves CVAR_STATIC_BAKE and moves the switch (the human `staticbake on|off`)
//   StaticBake_IsActive    whether the bake runs: the switch is on AND the backend can bake
//   StaticBake_Setting     the saved setting; differs from IsActive when the environment variable
//                          decided the session, after a session-only switch, or on a backend that
//                          cannot bake
//   StaticBake_BackendSupported  whether this rendering backend can bake at all (DX11 only, the
//                          backend whose SupportsStaticBake is true). On any other the switch can
//                          be on and every room is still interpreted
void StaticBake_SetActive(int active);
void StaticBake_SetSetting(int active);
int StaticBake_IsActive(void);
int StaticBake_Setting(void);
int StaticBake_BackendSupported(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_STATIC_BAKE_REGISTRY_H
