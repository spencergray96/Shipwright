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
//   - its textures that scroll (sturdy-bassoon#187 A2) are registered when it is resolved, BEFORE it is
//     offered to the bake, so before its first draw: a baked draw keeps the rate it was recorded under.
//     They come from `<path>.scroll`, a text resource the generator writes beside the list only when a
//     texture scrolls (generate_props.py, "SCROLLS"): a comment line, then one `<image path> <du> <dv>`
//     line per texture, in texture widths a second. A list with no such file registers nothing. A file
//     with a line that does not read registers nothing either, with one warning: half a list scrolling
//     is no better than none, and silence would hide it. One file for a path's two lists (below): the
//     registry binds a rate to an image path, whichever list draws it.
//
// THE TRANSLUCENT LIST (sturdy-bassoon#216 T2): beside each declared path the archive may hold
// `<path>.xlu`, a display list of the same form holding the map's translucent pairs (materials under
// G_RM_AA_ZB_XLU_SURF2, which write no depth; generate_props.py, "THE TRANSLUCENT LIST"). It is found by
// that name, so the scene's C declares one path per list as before, and is resolved, held, registered
// with the bake and released exactly as the path's own list. The room draw submits it in the room's
// TRANSLUCENT pass (POLY_XLU_DISP), after every opaque draw of the frame - the room's own geometry and
// every actor's - so nothing opaque drawn later erases it; the path's own list stays in the opaque pass.
//   - no `.xlu` in the archive is no translucent list, SILENTLY: no line, no warning. Most lists have
//     none (a map with nothing translucent, every chunk of a stitched scene but a few), and an archive
//     generated before T2 never has one - it keeps working without being regenerated;
//   - an empty one, or one that is not a display list, is treated as the path's own would be.
//   - the translucent submission can be switched off for a same-session A/B: `staticbake xlu off`
//     (CVAR_STATIC_BAKE_PROPS_XLU, read at every draw; on by default).
//
// An ACTOR-drawn kind (a #204 flipbook) would hook the same way, but in its own Draw: its opaque list
// into POLY_OPA_DISP and its `.xlu` into POLY_XLU_DISP, both under its own matrix, the two resolved and
// held as here (so a third ArchiveProps entry point taking a path, not a room). Nothing builds one yet.
// Only a ROOM_SHAPE_TYPE_NORMAL room draws them (func_80095AB4); any other shape's declaration is
// ignored. Every grid-tool room is that shape.

// The two passes a room submits archive prop lists in: its opaque pass, and its translucent pass.
#define ARCHIVE_PROPS_PASS_OPA 0
#define ARCHIVE_PROPS_PASS_XLU 1
#define ARCHIVE_PROPS_PASSES 2

#ifdef __cplusplus
extern "C" {
#endif

// From a compiled-in scene's InitRoom: the archive prop lists this room draws. Replaces any earlier
// declaration for the same scene and room. `paths` must outlive the session.
void ArchiveProps_DeclareRoom(PlayState* play, RoomContext* roomCtx, const char* const* paths, s32 count);

// From the room draw (func_80095AB4): the lists to submit for this room in `pass` (ARCHIVE_PROPS_PASS_*),
// after that pass's gMtxClear load, in world space. NULL with *count = 0 when there are none - always,
// in a vanilla scene, and in the translucent pass while `staticbake xlu off` holds.
Gfx* const* ArchiveProps_RoomLists(PlayState* play, s32 roomNum, s32 pass, s32* count);

#ifdef __cplusplus
}

#include <string>
#include <vector>

#include "soh/cvar_prefixes.h"

// Whether the room draw submits archive prop lists' translucent halves (sturdy-bassoon#216 T2): 1, the
// default, or 0 to compare a map with and without them in one session. `staticbake xlu on|off`; from the
// human command it is saved, from `agenttest staticbake` it is not.
#define CVAR_STATIC_BAKE_PROPS_XLU CVAR_DEVELOPER_TOOLS("StaticBakePropsXlu")
#define ARCHIVE_PROPS_XLU_DEFAULT 1

// The registry's side (StaticBakeRegistry.cpp) and the console's (StaticBakeConsole.cpp).
namespace ArchiveProps {

// Resolve, hold and offer to the bake every list the room declared, and each one's `.xlu`; returns how
// many were offered (an empty or missing list is not). A list held from an earlier visit in the bake
// group is offered again under the same key, which the bake reads as already held.
uint32_t OfferRoom(s32 sceneNum, s32 roomNum);

// The bake group was reset: drop every held list. They are resolved again when a room offers them.
void ReleaseHeld();

// Whether the translucent lists are submitted, and how many the group holds. `save` (the human
// `staticbake xlu on|off`) writes and saves CVAR_STATIC_BAKE_PROPS_XLU; otherwise (`agenttest staticbake
// xlu`) the switch holds for this session only and never touches the CVar store, as on|off do.
bool XluSubmitted();
void SetXluSubmitted(bool on, bool save);
uint32_t XluLists();

// `staticbake props`: one line per held list, after the caller's status line - a path's own, and its
// `<path>.xlu` when the archive has one; `scrolls=` is how many textures its scroll file registered
// (on the opaque line), `pass=` opa or xlu.
void Describe(std::vector<std::string>& lines);

} // namespace ArchiveProps
#endif

#endif // SOH_STATIC_BAKE_ARCHIVE_PROP_LISTS_H
