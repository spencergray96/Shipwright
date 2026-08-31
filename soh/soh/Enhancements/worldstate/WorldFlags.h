#ifndef SOH_WORLD_FLAGS_H
#define SOH_WORLD_FLAGS_H

#include <stdint.h>

// Project-owned persistent world-state flags (sturdy-bassoon#54).
//
// Every vanilla flag store is per scene and this mod's world is one scene, which caps the whole
// game at 32 persistent switch flags and 32 chests. This store sits beside the vanilla ones as a
// SaveManager section ("worldFlags"): no vanilla struct is touched, no save format is broken, and
// older saves load clean (a missing section leaves every flag 0; an unknown section in an older
// build is warned-and-skipped by SaveManager::LoadFile).
//
// Scope rule, accepted in the #48/#54 grill: this store serves the mod's OWN actors only. Vanilla
// actors are reused for models/animation, never for persistent state.
//
// Storage is randomizerInf-shaped (z64save.h ShipSaveContextData): u16 words, bit = flag & 0xF,
// word = flag >> 4. WORLD_FLAG_MAX is safe to RAISE later without a section version bump
// (SaveManager::LoadArray default-constructs the tail); never lower it or reorder meanings.
#define WORLD_FLAG_MAX 4096
#define WORLD_FLAG_WORDS ((WORLD_FLAG_MAX + 15) / 16)

#ifdef __cplusplus
extern "C" {
#endif

// Returns nonzero if `flag` is set; 0 if clear OR out of range (out-of-range also logs an error
// and asserts in debug builds - silent-failure limits are this project's stated enemy).
int32_t Flags_GetWorldFlag(int32_t flag);

// Set / clear one flag. Out of range: logged error + debug assert, no write.
void Flags_SetWorldFlag(int32_t flag);
void Flags_UnsetWorldFlag(int32_t flag);

// Number of flags currently set (popcount over the store). For diagnostics/tests.
int32_t WorldFlags_CountSet(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_WORLD_FLAGS_H
