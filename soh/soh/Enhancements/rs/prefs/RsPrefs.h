#ifndef SOH_RS_PREFS_H
#define SOH_RS_PREFS_H

#include <stdint.h>

// PER-SAVE PLAYER PREFERENCES (sturdy-bassoon#94). Today there is exactly one: the FLOOR
// CONVENTION, which decides whether `rs/` prose calls the storey at ground level the "ground
// floor" (UK) or the "first floor" (US).
//
// WHY THIS IS PER SAVE AND NOT A CVAR. A CVar in shipofharkinian.json is per INSTALL. The
// requirement is per FILE: one player keeping a British file and an American file to see how the
// other half reads, or two people sharing a machine, is the case this has to serve - and the
// setting is chosen at file creation, which is a save-file event, not a settings-menu event.
//
// WHY ITS OWN SECTION AND NOT A FIELD IN `quests` OR A WORLD FLAG. Adding a field to the `quests`
// section is a LAYOUT change, which needs version 2 plus keeping the v1 loader registered forever
// (QuestStore.cpp says so in as many words) - a whole version bump on the quest section to carry a
// byte that has nothing to do with quests. A world flag is WORLD STATE and lives under the
// debug/production band rules, so `quest debugwipe` would either clear a player preference or need
// an exception carved for it. And gSaveContext is off the table for the reasons WorldFlags.cpp
// records: no vanilla struct change, no SaveContext resize, invisible to savestates and Anchor
// memcpys.
//
// An ENUM rather than a bool, at the same one byte on disk. There are two conventions today and no
// third is coming, but an enum absorbs one without a section version bump, at the price of one
// validation branch. SERIALIZED VALUES CAN NEVER BE RENUMBERED either way, so this was decided
// once: UK is 0, because the source material is British and 0 is what every save written before
// this landed reads as.
//
// The grammar that turns a storey INDEX into a storey LABEL is FloorText.h; this file only holds
// the setting.

#define RS_FLOOR_CONVENTION_UK 0
#define RS_FLOOR_CONVENTION_US 1
#define RS_FLOOR_CONVENTION_COUNT 2

#define RS_FLOOR_CONVENTION_DEFAULT RS_FLOOR_CONVENTION_UK

#ifdef __cplusplus
extern "C" {
#endif

// The live setting. Always in [0, RS_FLOOR_CONVENTION_COUNT) - a value outside the enum cannot get
// in here, because both the setter and the loader range-check first.
int32_t RsPrefs_GetFloorConvention(void);

// Out of range: logged error + debug assert, no write. Same shape as the QuestStore accessors, and
// for the same reason - a silent clamp would make a typo look like it worked.
void RsPrefs_SetFloorConvention(int32_t convention);

// "uk" / "us", or "?" for a value outside the enum. Console words, not prose.
const char* RsPrefs_FloorConventionName(int32_t convention);

// 1 when the live value came from a save file's `rsPrefs` section, 0 when it is the default this
// build starts every file at. Both are legitimate states, and only one of them is evidence that
// persistence worked - which is why `region get` reports which.
int32_t RsPrefs_FloorConventionIsLoaded(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_PREFS_H
