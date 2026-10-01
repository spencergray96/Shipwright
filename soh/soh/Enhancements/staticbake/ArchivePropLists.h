#ifndef SOH_STATIC_BAKE_ARCHIVE_PROP_LISTS_H
#define SOH_STATIC_BAKE_ARCHIVE_PROP_LISTS_H

#include "z64.h"

#include <stdint.h>

// Archive prop lists (sturdy-bassoon#171 slice A; ADRs 2026-09-29-archive-props-baked and
// 2026-09-30-prop-authoring-grid-tool-layer): display lists that live in an .o2r archive rather than
// in compiled-in C, drawn by a compiled-in room and baked like its own lists.
//
// The contract a scene's C keeps - written down for the grid tool's emitter in sturdy-bassoon's
// docs/reference/ASSET_PIPELINE.md, "Archive props":
//
//   static const char* const sFooPropLists[] = { "objects/rs_props/foo/props" };
//   ...in Foo_InitRoom, once its room is set up:
//   ArchiveProps_DeclareRoom(play, roomCtx, sFooPropLists, ARRAY_COUNT(sFooPropLists));
//
// The paths are plain archive paths - no "__OTR__" prefix, no RS bytes - and must outlive the session
// (string literals in a static array do). A room that declares nothing draws no archive lists.
//
// What happens to each path at the room load (StaticBake_RegisterRoom):
//   - in no mounted archive (no props generated yet, or a player who has not run the installer):
//     the room loads and bakes exactly as without it, and one log line says so;
//   - an empty list (no commands, or only EndDisplayList - what a map without props exports): not
//     drawn and offered to nothing, silently;
//   - otherwise it is resolved once, held, offered to the static bake keyed by its instructions'
//     address, and submitted by the room draw. A path missing INSIDE the list refuses that list's
//     recording (libultraship's OTR handlers), and the list is interpreted instead.
// Only a ROOM_SHAPE_TYPE_NORMAL room draws them (func_80095AB4); any other shape's declaration is
// ignored. Every grid-tool room is that shape.

#ifdef __cplusplus
extern "C" {
#endif

// From a compiled-in scene's InitRoom: the archive prop lists this room draws. Replaces any earlier
// declaration for the same scene and room. `paths` must outlive the session.
void ArchiveProps_DeclareRoom(PlayState* play, RoomContext* roomCtx, const char* const* paths, s32 count);

// From the room draw (func_80095AB4): the lists to submit for this room, after its gMtxClear load, in
// world space. NULL with *count = 0 when there are none - always, in a vanilla scene.
Gfx* const* ArchiveProps_RoomLists(PlayState* play, s32 roomNum, s32* count);

#ifdef __cplusplus
}

#include <string>
#include <vector>

// The registry's side (StaticBakeRegistry.cpp) and the console's (StaticBakeConsole.cpp).
namespace ArchiveProps {

// Resolve, hold and offer to the bake every list the room declared; returns how many were offered
// (an empty or missing list is not). A list held from an earlier visit in the bake group is offered
// again under the same key, which the bake reads as already held.
uint32_t OfferRoom(s32 sceneNum, s32 roomNum);

// The bake group was reset: drop every held list. They are resolved again when a room offers them.
void ReleaseHeld();

// `staticbake props`: one line per held list, after the caller's status line.
void Describe(std::vector<std::string>& lines);

} // namespace ArchiveProps
#endif

#endif // SOH_STATIC_BAKE_ARCHIVE_PROP_LISTS_H
