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
// list pointer, which is only sound for addresses that stay put - stable C symbols, or an archive
// prop list whose resource the registry holds (ArchivePropLists.h, sturdy-bassoon#171) - and a
// vanilla scene's display lists never reach this function so they can never be registered.
//
// Also offers the room's archive prop lists: the ones its InitRoom declared (ArchiveProps_DeclareRoom).
//
// What it keeps (sturdy-bassoon#157): everything registered in the scene's BAKE GROUP - its step-warp
// group, RsWarp_SceneGroup - until a room of another group registers, which frees it all. So a
// return from an underground scene to its overworld replays the overworld's bakes on the first frame
// instead of recording them. See StaticBakeRegistry.cpp for why and what it costs.
void StaticBake_RegisterRoom(PlayState* play, RoomContext* roomCtx);

// Free every bake and registration now, whatever the group, then register the current room again if
// it is a compiled-in one - so it records on its next draw. `staticbake reset`: for timing a cold
// load (reset, then enter the scene), and for anyone who suspects a kept bake.
void StaticBake_Reset(void);
// The bake group the registry holds (the smallest scene id in it), -1 when nothing is held; and how
// many scenes' rooms have registered since the last reset.
int StaticBake_Group(void);
int StaticBake_HeldScenes(void);
// Join two scenes' bake groups for the rest of the session (`staticbake link`), as if a step warp ran
// between them. For measuring a kept return on content no warp reaches yet - the at-scale F2P fixture
// has no warp tiles. Links only merge, and nothing removes one short of a restart.
void StaticBake_Link(int sceneA, int sceneB);
int StaticBake_Links(void);

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
