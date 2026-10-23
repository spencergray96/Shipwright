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
//   - a second layer's (sturdy-bassoon#231), TEXEL1's, come from `<path>.scroll1`, in the same form,
//     registered for tile 1 (StaticBakeSetTextureScroll with a tile): the second tile of a two-layer OoT
//     material, which scrolls one image its own way. A sidecar rather than a field on `.scroll`, so a
//     build from before #231, which refuses a scroll file whole for a line it cannot read, never opens it
//     and still scrolls tile 0. Each file stands alone: one that does not read leaves the other registered.
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
// ACTOR-DRAWN COPIES (sturdy-bassoon#204 C3/C4a, the animated-props ADR's decision 52): a flipbook placement
// is drawn by its own Rs_AnimProp, not by the room. The generator writes each such copy's pose lists, in
// world space, and a table beside the path, `<path>.anim` (generate_props.py, FLIPBOOKS):
//
//   # flipbook copies for <path> (#204 C3): <id> <check> <frameStep> <startPose> <startCycle> <frame>:<cycles> ...
//   p7f3a 12345 2 1 3 0:5 1:4
//
// one line a copy, the line's index being the copy's (its row's params). The copy's lists are found by
// convention beside the path: `<dir>/copies/<id>/f<frame>` for each pose and `<dir>/copies/<id>/rest`, each
// with an optional `.xlu`, exactly as a room's. ArchiveProps_AnimCopy resolves and holds them as a room's are
// (registered with the bake and the mips, released with the bake group), and the actor submits them in its
// own Draw, under gMtxClear: the opaque list into POLY_OPA_DISP, its `.xlu` into POLY_XLU_DISP.
//   - a path with an `.anim` registers its scroll files even when its own list is empty: a map whose only
//     scrolling texture is drawn by actors still scrolls it;
//   - no `.anim`, no such line, a line whose check is not the row's (a row and an archive from two exports),
//     or a missing pose list: no copy, and one log line a cause. The actor draws nothing and says so.
// Only a ROOM_SHAPE_TYPE_NORMAL room draws them (func_80095AB4); any other shape's declaration is
// ignored. Every grid-tool room is that shape.

// The two passes a room submits archive prop lists in: its opaque pass, and its translucent pass.
#define ARCHIVE_PROPS_PASS_OPA 0
#define ARCHIVE_PROPS_PASS_XLU 1
#define ARCHIVE_PROPS_PASSES 2

// An actor-drawn copy (ACTOR-DRAWN COPIES above): its clock, from its `.anim` line, and its lists.
#define ARCHIVE_PROPS_ANIM_POSES_MAX 16
typedef struct ArchivePropsAnimCopy {
    s32 poseCount; // its poses, 2..ARCHIVE_PROPS_ANIM_POSES_MAX; rest is not one of them
    s32 frameStep; // RS's: past the last pose step back this many; out of range (RS's -1), stop on rest
    s32 startPose; // where RS's random start puts it: a pose, and a cycle within it
    s32 startCycle;
    s32 frames[ARCHIVE_PROPS_ANIM_POSES_MAX]; // each pose's RS frame, for reports
    s32 cycles[ARCHIVE_PROPS_ANIM_POSES_MAX]; // each pose's length, 20 ms client cycles
    // [pose][pass], pose poseCount being rest; NULL where the copy has no such list (an empty opaque list, or
    // no `.xlu`): nothing to submit.
    Gfx* lists[ARCHIVE_PROPS_ANIM_POSES_MAX + 1][ARCHIVE_PROPS_PASSES];
} ArchivePropsAnimCopy;

// Why ArchiveProps_AnimCopy found no copy (it returns NULL and writes one of these).
typedef enum ArchivePropsAnimMiss {
    ARCHIVE_PROPS_ANIM_OK,
    ARCHIVE_PROPS_ANIM_NO_LIST,  // the room declares no list at that index
    ARCHIVE_PROPS_ANIM_NO_TABLE, // the list has no `.anim`, or it does not read
    ARCHIVE_PROPS_ANIM_NO_LINE,  // the table has no line at that index
    ARCHIVE_PROPS_ANIM_CHECK,    // the line's check is not the row's
    ARCHIVE_PROPS_ANIM_NO_POSE,  // a pose's opaque list is missing or not a display list
} ArchivePropsAnimMiss;

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

// From an actor-drawn copy (Rs_AnimProp): copy `copy` of the room's declared list `list`, whose row carries
// `check`. The copy, held until the bake group goes (its address stable until then), or NULL with *miss
// saying why. Resolves on the first ask; later asks are a lookup.
const ArchivePropsAnimCopy* ArchiveProps_AnimCopy(PlayState* play, s32 roomNum, s32 list, s32 copy, s32 check,
                                                  s32* miss);
// Bumped whenever held lists are released (a new bake group, `staticbake reset`): a copy fetched under an
// older generation is gone - freed - and its holder must fetch it again before reading it, drawn or not.
u32 ArchiveProps_Generation(void);
// Whether the translucent halves are submitted (`staticbake xlu`): the room's gate, which a copy keeps too.
s32 ArchiveProps_XluSubmitted(void);
const char* ArchiveProps_AnimMissName(s32 miss);

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

// Actor-drawn copies (#204): the lists they hold, and one copy's lists by the bake's state (for `animprop`).
uint32_t AnimLists();
void AnimEntryStates(const ArchivePropsAnimCopy& copy, uint32_t* baked, uint32_t* rejected, uint32_t* unbaked);

// `staticbake props`: one line per held list, after the caller's status line - a path's own, and its
// `<path>.xlu` when the archive has one; `scrolls=` is how many textures its scroll file registered
// (on the opaque line), `pass=` opa or xlu.
void Describe(std::vector<std::string>& lines);

} // namespace ArchiveProps
#endif

#endif // SOH_STATIC_BAKE_ARCHIVE_PROP_LISTS_H
