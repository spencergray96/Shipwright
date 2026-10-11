#ifndef SOH_STATIC_BAKE_REGISTRY_H
#define SOH_STATIC_BAKE_REGISTRY_H

#include "z64.h"
#include "soh/cvar_prefixes.h"

// The saved setting behind the switch (sturdy-bassoon#153): 1 bakes, 0 interprets. On by default.
// Written by the Settings > Graphics checkbox and StaticBake_SetSetting (the human `staticbake on|off`);
// SOH_STATIC_BAKE=0|1 in the environment overrides it for the whole session.
#define CVAR_STATIC_BAKE CVAR_SETTING("StaticBake")
#define STATIC_BAKE_DEFAULT 1

// The saved wind (sturdy-bassoon#209 W1; libultraship's fast/StaticMeshCache.h, "Wind in the replay"):
// amplitude (world units of swing at the hem), frequency (Hz), wavelength (world units), yaw (degrees,
// where it blows to) and ripple (radians). Each one unset is the owner's default, libultraship's
// StaticBakeWind. Not "StaticBake.Wind...": CVAR_STATIC_BAKE is a value, and a CVar name nests in the
// saved config at its dots.
#define CVAR_STATIC_BAKE_WIND(name) CVAR_SETTING("StaticBakeWind." name)
#define CVAR_STATIC_BAKE_WIND_AMPLITUDE CVAR_STATIC_BAKE_WIND("Amplitude")
#define CVAR_STATIC_BAKE_WIND_FREQUENCY CVAR_STATIC_BAKE_WIND("Frequency")
#define CVAR_STATIC_BAKE_WIND_WAVELENGTH CVAR_STATIC_BAKE_WIND("Wavelength")
#define CVAR_STATIC_BAKE_WIND_YAW CVAR_STATIC_BAKE_WIND("Yaw")
#define CVAR_STATIC_BAKE_WIND_RIPPLE CVAR_STATIC_BAKE_WIND("Ripple")

// The saved wobble (sturdy-bassoon#216 W; libultraship's fast/StaticMeshCache.h, "Wobble in the
// replay"): strength (0-1, how far a wobbling face's alpha falls) and speed (Hz). Each one unset is the
// owner's default, libultraship's StaticBakeWobble. Named apart from StaticBake for the wind's reason.
#define CVAR_STATIC_BAKE_WOBBLE(name) CVAR_SETTING("StaticBakeWobble." name)
#define CVAR_STATIC_BAKE_WOBBLE_STRENGTH CVAR_STATIC_BAKE_WOBBLE("Strength")
#define CVAR_STATIC_BAKE_WOBBLE_SPEED CVAR_STATIC_BAKE_WOBBLE("Speed")

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

// Wind in the replay (sturdy-bassoon#209 W1): apply the saved wind (CVAR_STATIC_BAKE_WIND_*). Startup
// does, and again whenever the menu or a preset may have changed one. The wind itself is a frame-level
// value in libultraship that any code may change at any time - a scripted gust, the weather - through
// Fast::StaticBakeSetWind; calling this goes back to the saved one.
void StaticBake_ApplyWindSettings(void);

// The wobble (sturdy-bassoon#216 W), the same way: apply the saved wobble (CVAR_STATIC_BAKE_WOBBLE_*).
// Code may change it at any time through Fast::StaticBakeSetWobble; calling this goes back to the saved one.
void StaticBake_ApplyWobbleSettings(void);

#ifdef __cplusplus
}

#include <fast/StaticMeshCache.h>

// The saved wind, each value unset taking the owner's default; save one (CVarSave included); clear
// them all, so the defaults hold again. For `staticbake wind` (StaticBakeConsole.cpp).
Fast::StaticBakeWind StaticBake_WindSettings();
void StaticBake_SaveWindSettings(const Fast::StaticBakeWind& wind);
void StaticBake_ClearWindSettings();

// The same three for the saved wobble (#216 W), for `staticbake wobble`.
Fast::StaticBakeWobble StaticBake_WobbleSettings();
void StaticBake_SaveWobbleSettings(const Fast::StaticBakeWobble& wobble);
void StaticBake_ClearWobbleSettings();
#endif

#endif // SOH_STATIC_BAKE_REGISTRY_H
