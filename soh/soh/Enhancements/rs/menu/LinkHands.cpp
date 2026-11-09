/*
 * LinkHands.cpp - the scroll's hands, cut from Link's own model (sturdy-bassoon#299). LinkHands.h has the design.
 *
 * What was measured to write this, so the next reader does not have to (from the extracted oot.o2r; the run
 * record in sturdy-bassoon docs/test-runs/ has the script):
 *   - LIMB FRAMES. The forearm runs along its own +x: its child, the hand, sits at (754, 0, 0) adult and
 *     (514, 0, 0) child. The upper arm is 919 adult / 581 child long (the forearm's own offset). The hand's
 *     fingers run along the HAND's +y, not +x: an adult fist spans x -308..315, y 2..679, z -302..262 (child
 *     -210..200, -2..499, -206..175), so a straight wrist is a quarter turn about z.
 *   - WELDS. The forearm list loads matrix slot 10 (upper arm) for its elbow and then 11 (itself); the upper arm
 *     loads 17 for its shoulder and then 10; the hand lists load nothing (they draw under whatever is loaded).
 *     The right arm is 13, 14, 15. The plates: Plate1 (the bracer) loads the forearm's slot, Plate2 (open) and
 *     Plate3 (fist) the hand's; the child's Goron bracelet loads the LEFT forearm's only.
 *   - COLOUR. Only the upper arm combines with ENVIRONMENT - the tunic's sleeve. The forearm and hand are
 *     TEXEL0 x SHADE then PRIMITIVE (white): skin, the white sleeve and the leather bracer, the same in every
 *     tunic. So the tunic shows on the hands only if the upper arm is on screen.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-11-07
 */

#include "LinkHandsDraw.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>

#include "PauseLink.h"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
extern Color_RGB8 sTunicColors[];
extern Color_RGB8 sGauntletColors[];
}

// --- the data: hand sets and arm pieces ------------------------------------------------------------------------

constexpr int32_t kGripCount = 2; // RS grip index: 0 fist, 1 open
static const char* const kGripNames[kGripCount] = { "fist", "open" };

// One age's arms. Index [side] is the SCREEN side, and screen left is Link's left: the POV is his.
struct RsHandSet {
    const char* id;
    const char* upper[2];
    const char* forearm[2];
    const char* hand[2][kGripCount];
    // The point of the hand, in its own model units, that sits on the grip - the middle of the fist, the
    // middle of the open hand's palm - from the bounding boxes above. Tuned by eye, like the pose.
    float gripPoint[kGripCount][3];
    float defaultScale;
};

static const RsHandSet kHandSets[2] = {
    { "adult",
      { gLinkAdultLeftShoulderNearDL, gLinkAdultRightShoulderNearDL },
      { gLinkAdultLeftArmNearDL, gLinkAdultRightArmNearDL },
      { { gLinkAdultLeftHandClosedNearDL, gLinkAdultLeftHandNearDL },
        { gLinkAdultRightHandClosedNearDL, gLinkAdultRightHandNearDL } },
      { { 0.0f, 340.0f, -20.0f }, { 35.0f, 430.0f, 40.0f } },
      0.07f },
    { "child",
      { gLinkChildLeftShoulderNearDL, gLinkChildRightShoulderNearDL },
      { gLinkChildLeftForearmNearDL, gLinkChildRightForearmNearDL },
      { { gLinkChildLeftFistNearDL, gLinkChildLeftHandNearDL },
        { gLinkChildRightHandClosedNearDL, gLinkChildRightHandNearDL } },
      { { -5.0f, 250.0f, -15.0f }, { 32.0f, 275.0f, 18.0f } },
      0.09f },
};

// An arm piece: what the save has Link wearing that shows on his arms. ADD A ROW HERE for a new one - nothing
// else in this file names a piece.
enum RsArmStage {
    RS_ARM_TINT,    // an environment colour set before the limbs (the tunic's sleeve)
    RS_ARM_OVERLAY, // display lists drawn over the arm after the limbs, under the hand's matrix
};
constexpr int32_t kSetAdult = 1 << 0;
constexpr int32_t kSetChild = 1 << 1;
constexpr int32_t kPieceLists = 2;

struct RsArmPiece {
    const char* id;
    int32_t sets; // kSetAdult | kSetChild
    RsArmStage stage;
    bool (*worn)();
    bool (*colour)(Color_RGB8* out); // the environment colour; null for none
    const char* dl[2][kGripCount][kPieceLists]; // [side][grip][n], null where there is nothing
};

static int32_t SaveTunic() {
    return TUNIC_EQUIP_TO_PLAYER(CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC));
}

// Player_DrawImpl's tunic colour (z_player_lib.c), cosmetics editor included, so the sleeve matches Link.
static bool TunicColour(int32_t tunic, Color_RGB8* out) {
    if (tunic < 0 || tunic >= PLAYER_TUNIC_MAX) {
        return false;
    }
    static const char* const kCosmetic[PLAYER_TUNIC_MAX] = { "Link.KokiriTunic", "Link.GoronTunic", "Link.ZoraTunic" };
    *out = sTunicColors[tunic];
    const std::string changed = std::string(CVAR_COSMETIC("")) + kCosmetic[tunic] + ".Changed";
    if (CVarGetInteger(changed.c_str(), 0)) {
        const std::string value = std::string(CVAR_COSMETIC("")) + kCosmetic[tunic] + ".Value";
        *out = CVarGetColor24(value.c_str(), sTunicColors[tunic]);
    }
    return true;
}

static bool WornKokiri() {
    return SaveTunic() == PLAYER_TUNIC_KOKIRI;
}
static bool WornGoron() {
    return SaveTunic() == PLAYER_TUNIC_GORON;
}
static bool WornZora() {
    return SaveTunic() == PLAYER_TUNIC_ZORA;
}
static bool ColourKokiri(Color_RGB8* out) {
    return TunicColour(PLAYER_TUNIC_KOKIRI, out);
}
static bool ColourGoron(Color_RGB8* out) {
    return TunicColour(PLAYER_TUNIC_GORON, out);
}
static bool ColourZora(Color_RGB8* out) {
    return TunicColour(PLAYER_TUNIC_ZORA, out);
}

// Player_DrawImpl again: silver at strength 2, gold at 3, each with its cosmetics override.
static bool GauntletColour(int32_t strength, const char* cosmetic, Color_RGB8* out) {
    *out = sGauntletColors[strength - 2];
    const std::string changed = std::string(CVAR_COSMETIC("")) + cosmetic + ".Changed";
    if (CVarGetInteger(changed.c_str(), 0)) {
        const std::string value = std::string(CVAR_COSMETIC("")) + cosmetic + ".Value";
        *out = CVarGetColor24(value.c_str(), *out);
    }
    return true;
}
static bool WornSilver() {
    return CUR_UPG_VALUE(UPG_STRENGTH) == PLAYER_STR_SILVER_G;
}
static bool WornGold() {
    return CUR_UPG_VALUE(UPG_STRENGTH) == PLAYER_STR_GOLD_G;
}
static bool ColourSilver(Color_RGB8* out) {
    return GauntletColour(PLAYER_STR_SILVER_G, "Gloves.SilverGauntlets", out);
}
static bool ColourGold(Color_RGB8* out) {
    return GauntletColour(PLAYER_STR_GOLD_G, "Gloves.GoldenGauntlets", out);
}
static bool WornBracelet() {
    return Player_GetStrength() > PLAYER_STR_NONE;
}

#define RS_GAUNTLET_LISTS                                                                                  \
    { { { gLinkAdultLeftGauntletPlate1DL, gLinkAdultLeftGauntletPlate3DL },                                \
        { gLinkAdultLeftGauntletPlate1DL, gLinkAdultLeftGauntletPlate2DL } },                              \
      { { gLinkAdultRightGauntletPlate1DL, gLinkAdultRightGauntletPlate3DL },                              \
        { gLinkAdultRightGauntletPlate1DL, gLinkAdultRightGauntletPlate2DL } } }

static const RsArmPiece kArmPieces[] = {
    { "kokiri_tunic", kSetAdult | kSetChild, RS_ARM_TINT, WornKokiri, ColourKokiri, {} },
    { "goron_tunic", kSetAdult | kSetChild, RS_ARM_TINT, WornGoron, ColourGoron, {} },
    { "zora_tunic", kSetAdult | kSetChild, RS_ARM_TINT, WornZora, ColourZora, {} },
    { "silver_gauntlets", kSetAdult, RS_ARM_OVERLAY, WornSilver, ColourSilver, RS_GAUNTLET_LISTS },
    { "golden_gauntlets", kSetAdult, RS_ARM_OVERLAY, WornGold, ColourGold, RS_GAUNTLET_LISTS },
    // The child's bracelet is on his LEFT forearm only (it loads slot 11), so the right hand has none.
    { "goron_bracelet",
      kSetChild,
      RS_ARM_OVERLAY,
      WornBracelet,
      nullptr,
      { { { gLinkChildGoronBraceletDL, nullptr }, { gLinkChildGoronBraceletDL, nullptr } }, {} } },
};
constexpr int32_t kArmPieceCount = (int32_t)(sizeof(kArmPieces) / sizeof(kArmPieces[0]));

// --- the live tuning, as CVars so `set` and a restart keep it -------------------------------------------------

#define CVAR_RS_HAND(name) CVAR_ENHANCEMENT("RsMenuHand." name)

static const char* const kSideKey[2] = { "L", "R" };

// THE LEVEL TURN (owner look 3, contact sheet 18-19). Through the turn each arm is rigid with its roll, and the right
// arm swings about 180 degrees round its hand; at about 165 game units it was too short to leave the screen at the
// diagonals, so its far end - the open rim of the sleeve - hung in mid-air. Two levers, both live:
//   REACH lengthens the upper arm along its own axis, about the elbow, so the arm leaves the screen at every angle.
//     The upper arm is the part that is off screen, so the stretch is hardly seen.
//   TURN BEND bends the arm at the WRIST by up to this many degrees mid-turn and not at all at either end (a sine of
//     the turn's blend), so the swinging forearm heads for the nearest edge sooner. Per side; 0 switches it off. It
//     was asked for at the elbow, but mid-turn the elbow is already off screen: a 40-degree elbow bend left every
//     tick of the contact sheet unchanged. The bend is about the hand's x, which the default rot (y -90) turns onto
//     the screen's z, so it bends the arm in the screen's plane.
constexpr float kDefaultReach = 2.5f;
// Right only: the right hand is the one that swings round to the top, and leading its swing by 30 degrees shortens the
// time its arm spends pointing sideways. The left hand ends pointing down as it began, so it gets none.
static const float kDefaultTurnBend[2] = { 0.0f, 30.0f };

// The default pose, from the owner's first live session (2026-11-08): hand upright (fingers up the screen, the hand
// limb's +y), a straight wrist (the forearm's +x turned onto the hand's +y) and a straight elbow, so the arm hangs
// straight down from the grip and off the bottom edge - and each hand turned a quarter about the screen's vertical
// (rot y -90), so the backs of both hands face the screen's outer edges. One sign for both: the left hand showed
// its back and the right its palm, so the same turn sends the two backs opposite ways.
static const RsLinkHandPose kDefaultPose = { { 0.0f, -90.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 90.0f }, 0.0f, 0.0f,
                                              0.0f };
// The open hand, the owner's pick in the same session ("much better, especially since they'll be partially hidden
// by the scroll").
constexpr int32_t kDefaultGrip = 1;
static const float kDefaultLight[3] = { -40.0f, 60.0f, 90.0f };
constexpr int32_t kDefaultAmbient = 90;

static float SideFloat(int32_t side, const char* field, float fallback) {
    char name[96];
    std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.%s"), kSideKey[side], field);
    return CVarGetFloat(name, fallback);
}

static void SetSideFloat(int32_t side, const char* field, float value) {
    char name[96];
    std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.%s"), kSideKey[side], field);
    CVarSetFloat(name, value);
}

static RsLinkHandPose ReadPose(int32_t side) {
    RsLinkHandPose p;
    static const char* const kRot[3] = { "RotX", "RotY", "RotZ" };
    static const char* const kWrist[3] = { "WristX", "WristY", "WristZ" };
    for (int32_t i = 0; i < 3; i++) {
        p.rot[i] = SideFloat(side, kRot[i], kDefaultPose.rot[i]);
        p.wrist[i] = SideFloat(side, kWrist[i], kDefaultPose.wrist[i]);
    }
    p.offset[0] = SideFloat(side, "OffsetX", kDefaultPose.offset[0]);
    p.offset[1] = SideFloat(side, "OffsetY", kDefaultPose.offset[1]);
    p.elbow = SideFloat(side, "Elbow", kDefaultPose.elbow);
    p.vturn = SideFloat(side, "VTurn", kDefaultPose.vturn);
    p.turnBend = SideFloat(side, "TurnBend", kDefaultTurnBend[side]);
    return p;
}

static bool UseBoxes() {
    return CVarGetInteger(CVAR_RS_HAND("Boxes"), 0) != 0;
}
static int32_t Grip() {
    return std::clamp(CVarGetInteger(CVAR_RS_HAND("Grip"), kDefaultGrip), 0, kGripCount - 1);
}
static bool Upper() {
    return CVarGetInteger(CVAR_RS_HAND("Upper"), 1) != 0;
}
static float Reach() {
    return std::max(0.1f, CVarGetFloat(CVAR_RS_HAND("Reach"), kDefaultReach));
}
static bool Occlude() {
    return CVarGetInteger(CVAR_RS_HAND("Occlude"), 1) != 0;
}
// 10 game units: the open hand at the owner's pose lies wholly in front of the rolls' axis at 0 and wholly behind at
// 30; at 10 the thumb and fingertips wrap over the roll and the palm goes behind it (agent check, 2026-11-08).
constexpr float kDefaultPaper = 10.0f;
static float Paper() {
    return CVarGetFloat(CVAR_RS_HAND("Paper"), kDefaultPaper);
}
constexpr int32_t kCutTris = 0;  // the hand split by triangle (default)
constexpr int32_t kCutPixel = 1; // the hand cut by depth per pixel (the first holding build)
static int32_t Cut() {
    return std::clamp(CVarGetInteger(CVAR_RS_HAND("Cut"), kCutTris), kCutTris, kCutPixel);
}
static bool Trim() {
    return CVarGetInteger(CVAR_RS_HAND("Trim"), 1) != 0;
}
static float SetScale(int32_t set) {
    return CVarGetFloat(set == 0 ? CVAR_RS_HAND("ScaleAdult") : CVAR_RS_HAND("ScaleChild"),
                        kHandSets[set].defaultScale);
}
static void ReadLight(float dir[3], int32_t* ambient) {
    dir[0] = CVarGetFloat(CVAR_RS_HAND("LightX"), kDefaultLight[0]);
    dir[1] = CVarGetFloat(CVAR_RS_HAND("LightY"), kDefaultLight[1]);
    dir[2] = CVarGetFloat(CVAR_RS_HAND("LightZ"), kDefaultLight[2]);
    *ambient = std::clamp(CVarGetInteger(CVAR_RS_HAND("Ambient"), kDefaultAmbient), 0, 255);
}

// --- the skeleton: which limbs own which matrix slot ------------------------------------------------------------

struct RsSkeletonInfo {
    int32_t age = -1;
    int32_t slots = 0;
    int32_t slotOf[PLAYER_LIMB_MAX] = {}; // -1 for a limb with no display list
    Vec3s jointPos[PLAYER_LIMB_MAX] = {};
};
static RsSkeletonInfo sSkel;
static int32_t sSkelLoads = 0;

// SkelAnime_DrawFlexLimbLod's order: the limb, then its child's subtree, then its sibling's. A slot goes to every
// limb whose near list is non-null, whether or not it is drawn - which is what the 0x0D offsets inside the lists
// were authored against.
static void WalkLimbs(void** skeleton, int32_t limbIndex, int32_t* next) {
    LodLimb* limb = (LodLimb*)SEGMENTED_TO_VIRTUAL(skeleton[limbIndex]);
    const int32_t id = limbIndex + 1;
    if (id < PLAYER_LIMB_MAX) {
        sSkel.slotOf[id] = limb->dLists[0] != nullptr ? (*next)++ : -1;
        sSkel.jointPos[id] = limb->jointPos;
    }
    if (limb->child != LIMB_DONE) {
        WalkLimbs(skeleton, limb->child, next);
    }
    if (limb->sibling != LIMB_DONE) {
        WalkLimbs(skeleton, limb->sibling, next);
    }
}

static bool LoadSkeleton(PlayState* play) {
    void** skeleton = nullptr;
    int32_t dListCount = 0;
    int32_t age = -1;
    if (!RsPauseLink_Skeleton(play, &skeleton, &dListCount, &age)) {
        return false;
    }
    if (age == sSkel.age && sSkel.slots > 0) {
        return true;
    }
    sSkel = RsSkeletonInfo();
    for (int32_t i = 0; i < PLAYER_LIMB_MAX; i++) {
        sSkel.slotOf[i] = -1;
    }
    int32_t next = 0;
    WalkLimbs(skeleton, 0, &next);
    sSkel.slots = std::max(next, dListCount);
    sSkel.age = age;
    sSkelLoads++;
    return sSkel.slots > 0;
}

// --- each list's vertices, for the drawn extents ----------------------------------------------------------------
//
// The extents are measured over the vertices the drawn lists actually load, each through the matrix it is drawn
// under (a vertex loaded after a 0x0D matrix load takes that slot's), so the cursor box and `menu dump` can be
// checked against the arm as drawn rather than against a number typed beside it. Parsed once per list.

struct RsListVerts {
    std::vector<int8_t> slot; // -1: drawn under the limb's own matrix
    std::vector<Vec3s> pos;
};
static std::unordered_map<const Gfx*, RsListVerts> sListVerts;

// The vertices a G_VTX_OTR_HASH loads. The interpreter caches the resolved pointer into w1 the first time it runs
// the command (gfx_vtx_hash_handler_custom), and tells the two apart the same way: an "offset" above 0xFFFFF is a
// pointer. A trimmed copy (below) is built with pointers in w1 from the start.
static const Vtx* HashVerts(const Gfx* g) {
    const uintptr_t offset = g->words.w1;
    if (offset > 0xFFFFF) {
        return (const Vtx*)offset;
    }
    const Gfx* h = g + 1;
    const uint64_t hash = ((uint64_t)h->words.w0 << 32) + (uint64_t)(uint32_t)h->words.w1;
    const char* base = (const char*)ResourceGetDataByCrc(hash);
    return base != nullptr ? (const Vtx*)(base + offset) : nullptr;
}

// The 128-bit forms (DisplayListFactory.cpp): the command's second word is part of it.
static bool IsWide(uint8_t op) {
    return op == (uint8_t)G_VTX_OTR_HASH || op == (uint8_t)G_SETTIMG_OTR_HASH || op == (uint8_t)G_DL_OTR_HASH ||
           op == (uint8_t)G_MARKER || op == (uint8_t)G_MTX_OTR || op == (uint8_t)G_BRANCH_Z_OTR ||
           op == (uint8_t)G_MOVEMEM_OTR;
}

static const RsListVerts& ListVerts(const Gfx* list) {
    auto it = sListVerts.find(list);
    if (it != sListVerts.end()) {
        return it->second;
    }
    RsListVerts& out = sListVerts[list];
    const Gfx* g = list;
    int8_t slot = -1;
    for (int32_t i = 0; g != nullptr && i < 4096; i++, g++) {
        const uint8_t op = (uint8_t)(g->words.w0 >> 24);
        if (op == (uint8_t)G_ENDDL) {
            break;
        }
        if (op == (uint8_t)G_MTX) {
            const uintptr_t addr = g->words.w1;
            slot = (addr >> 24) == 0x0D ? (int8_t)(((addr & 0xFFFFFF) & ~(uintptr_t)1) / sizeof(Mtx)) : -1;
        } else if (op == (uint8_t)G_VTX_OTR_HASH) {
            const int32_t n = (int32_t)((g->words.w0 >> 12) & 0xFF);
            const Vtx* v = HashVerts(g);
            for (int32_t k = 0; v != nullptr && k < n; k++) {
                out.slot.push_back(slot);
                out.pos.push_back({ v[k].v.ob[0], v[k].v.ob[1], v[k].v.ob[2] });
            }
        }
        if (IsWide(op)) {
            g++;
        }
    }
    return out;
}

// --- sleeve trims: vertices moved in a copy of a list ------------------------------------------------------------
//
// CALL 5 OF THE OWNER'S SESSION, the "rogue tunic triangle". Each upper arm's tunic sleeve has one vertex that
// flares well out from the rest, 15 model units short of the elbow: (702, 10, +-408) adult against +-220..278 for
// its neighbours, (567, 10, +-302) child against +-138..160. On Link that flap sits against his body and is never
// seen; cut out and turned a quarter (rot y -90) it sticks straight out sideways, left on the left arm and right on
// the right - exactly what the owner saw. It is NOT the shoulder's weld to the torso (slot 17): those five vertices
// are at the far, shoulder end of the upper arm, off the bottom of the screen.
//
// The trim pulls that one vertex in to its neighbours' depth, in a COPY of the list with its own vertex arrays: the
// game's own data is shared with Link in the world and is never written. A row is a set, a side, the vertex as the
// list has it and where it goes. `menu hand trim off` draws the lists untouched, for an A/B.
struct RsVertexTrim {
    int32_t set;  // 0 adult, 1 child
    int32_t side; // 0 left, 1 right
    Vec3s from;
    Vec3s to;
};
static const RsVertexTrim kSleeveTrims[] = {
    { 0, 0, { 702, 10, 408 }, { 702, 10, 250 } },
    { 0, 1, { 702, 10, -408 }, { 702, 10, -250 } },
    { 1, 0, { 567, 10, 302 }, { 567, 10, 160 } },
    { 1, 1, { 567, 10, -303 }, { 567, 10, -160 } },
};

struct RsTrimmedList {
    std::vector<Gfx> gfx;
    std::vector<std::unique_ptr<Vtx[]>> verts;
    int32_t moved = 0;
};
static std::unordered_map<const Gfx*, RsTrimmedList> sTrimmed;

// The list to draw for `source` on this set and side: a trimmed copy if a row matches one of its vertices, else the
// source itself. Built once per list; `moved` counts the vertices the rows moved.
static const Gfx* TrimmedList(const Gfx* source, int32_t set, int32_t side, int32_t* moved) {
    *moved = 0;
    if (source == nullptr || !Trim()) {
        return source;
    }
    auto it = sTrimmed.find(source);
    if (it == sTrimmed.end()) {
        RsTrimmedList& out = sTrimmed[source];
        for (const Gfx* g = source; out.gfx.size() < 4096; g++) {
            const uint8_t op = (uint8_t)(g->words.w0 >> 24);
            out.gfx.push_back(*g);
            if (op == (uint8_t)G_ENDDL) {
                break;
            }
            if (op == (uint8_t)G_VTX_OTR_HASH) {
                const int32_t n = (int32_t)((g->words.w0 >> 12) & 0xFF);
                const Vtx* v = HashVerts(g);
                if (v != nullptr) {
                    std::unique_ptr<Vtx[]> copy(new Vtx[n]);
                    std::memcpy(copy.get(), v, n * sizeof(Vtx));
                    for (int32_t k = 0; k < n; k++) {
                        for (const RsVertexTrim& t : kSleeveTrims) {
                            Vtx& c = copy[k];
                            if (t.set == set && t.side == side && c.v.ob[0] == t.from.x && c.v.ob[1] == t.from.y &&
                                c.v.ob[2] == t.from.z) {
                                c.v.ob[0] = t.to.x;
                                c.v.ob[1] = t.to.y;
                                c.v.ob[2] = t.to.z;
                                out.moved++;
                            }
                        }
                    }
                    // A pointer in w1 is what the interpreter's own cache leaves there, so it reads this as one.
                    out.gfx.back().words.w1 = (uintptr_t)copy.get();
                    out.verts.push_back(std::move(copy));
                }
            }
            if (IsWide(op)) {
                g++;
                out.gfx.push_back(*g);
            }
        }
        it = sTrimmed.find(source);
    }
    if (it->second.moved == 0) {
        return source;
    }
    *moved = it->second.moved;
    return it->second.gfx.data();
}

// --- status ------------------------------------------------------------------------------------------------------

static RsLinkHandsStatus sStatus = [] {
    RsLinkHandsStatus s{};
    s.set = "none";
    s.grip = kGripNames[kDefaultGrip];
    s.variant = "none";
    return s;
}();

bool RsLinkHands_Enabled() {
    return !UseBoxes();
}

void RsLinkHands_DrewBoxes(int32_t side, const float extent[4]) {
    sStatus.set = "boxes";
    sStatus.dlists[side] = 0;
    sStatus.verts[side] = 0;
    for (int32_t k = 0; k < 4; k++) {
        sStatus.extent[side][k] = extent[k];
    }
}

// --- drawing -----------------------------------------------------------------------------------------------------

constexpr float kDeg = 3.14159265358979f / 180.0f;
// The hands' ortho: the menu's x and y, deep in z. Link's limbs sit within a few hundred game units of z 0 at any
// pose the knobs reach, and a near plane well in front keeps them at the near end of the depth range too.
constexpr float kHandsNear = -600.0f;
constexpr float kHandsFar = 1400.0f;

void RsLinkHands_PushDepthClear(std::vector<Gfx>& dl, GraphicsContext* gfxCtx) {
    if (!RsLinkHands_Enabled()) {
        return;
    }
    // A partial depth clear: the interpreter skips a FULL-screen fill of the depth image as redundant with its
    // own clear at frame start (Interpreter::GfxDpFillRectangle), so this is two rectangles each one column short
    // of the band, which between them cover all of it. A depth fill only ever resets to far there (it is a
    // scissored depth clear, whatever the fill colour), which is why the scroll's depth is drawn, not filled.
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, gZBuffer));
    dl.push_back(gsDPSetCycleType(G_CYC_FILL));
    dl.push_back(gsDPSetRenderMode(G_RM_NOOP, G_RM_NOOP2));
    dl.push_back(gsDPSetFillColor((GPACK_ZDZ(G_MAXFBZ, 0) << 16) | GPACK_ZDZ(G_MAXFBZ, 0)));
    dl.push_back(gsDPFillRectangle(0, 0, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 1));
    dl.push_back(gsDPFillRectangle(1, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1));
    dl.push_back(gsDPPipeSync());
    dl.push_back(gsDPSetColorImage(G_IM_FMT_RGBA, G_IM_SIZ_16b, SCREEN_WIDTH, gfxCtx->curFrameBuffer));
}

// TWO DEPTH RANGES under one x and y (owner look 2, RED). The scroll's depth-only quads and the hand's BACK triangles
// are drawn in the far half of the depth range; the forearm, the sleeve, the bracer and the hand's FRONT triangles in
// the near half. Each half keeps its own order, so the arm still sorts against itself - but nothing in the near half
// can ever be hidden by the scroll, and the forearm is no longer cut by the roll it hangs past. Both are this ortho
// with its clip z squeezed into one half ([0, 1] behind, [-1, 0] in front).
static Mtx* HandsOrtho(GraphicsContext* gfxCtx, bool front) {
    MtxF mf;
    guOrthoF(mf.mf, -(f32)(SCREEN_WIDTH / 2), (f32)(SCREEN_WIDTH / 2), -(f32)(SCREEN_HEIGHT / 2),
             (f32)(SCREEN_HEIGHT / 2), kHandsNear, kHandsFar, 1.0f);
    // Row vectors: clip z is the third column; an ortho's w is 1, so the offset goes on the last row.
    for (int32_t i = 0; i < 4; i++) {
        mf.mf[i][2] *= 0.5f;
    }
    mf.mf[3][2] += front ? -0.5f : 0.5f;
    Mtx* m = (Mtx*)Graph_Alloc(gfxCtx, sizeof(Mtx));
    guMtxF2L(mf.mf, m);
    return m;
}

void RsLinkHands_PushProjection(GraphicsContext* gfxCtx, std::vector<Gfx>& dl) {
    dl.push_back(gsSPMatrix(HandsOrtho(gfxCtx, false), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
}

// Depth, and no colour. The interpreter draws a surface INVISIBLE (alpha forced to 0, blended) when the second
// cycle's blender is CLR_IN x 0 + CLR_MEM x 1-A (Interpreter's `invisible` and `use_alpha` tests read exactly these
// bits); Z_UPD still writes its depth. The art is cut to its own alpha by the threshold, which runs before the
// alpha is forced to 0, so the hands go behind the paper's torn edge and not behind the empty corners round it.
// POINT sampled (owner look 2, BLUE): bilinear, the threshold kept the faint alpha fringe half a texel past the art's
// edge - about 5 px at a 1808-wide window - and cut the fingertips short of the roll they poke past.
#define RS_RM_HAND_OCCLUDER(clk)     Z_CMP | Z_UPD | IM_RD | CVG_DST_FULL | ZMODE_OPA | FORCE_BL | GBL_c##clk(G_BL_CLR_IN, G_BL_0, G_BL_CLR_MEM, G_BL_1MA)

bool RsLinkHands_PushOccluderState(std::vector<Gfx>& dl, bool textured) {
    if (!RsLinkHands_Enabled() || !Occlude()) {
        return false;
    }
    dl.push_back(gsDPPipeSync());
    if (textured) {
        dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
        dl.push_back(gsDPSetCombineMode(G_CC_MODULATERGBA_PRIM, G_CC_MODULATERGBA_PRIM));
        dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
    } else {
        dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF));
        dl.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE));
    }
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_POINT | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  (textured ? G_AC_THRESHOLD : G_AC_NONE) | G_ZS_PIXEL | RS_RM_HAND_OCCLUDER(1) |
                                      RS_RM_HAND_OCCLUDER(2)));
    dl.push_back(gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH));
    return true;
}

// THE PER-TRIANGLE SPLIT (owner look 2, GREEN). A copy of a hand list, made each frame in frame memory, keeping only
// the triangles on one side of the scroll's plane: BACK keeps those whose centre is behind z 0 (screen space, after
// the paper knob's step back), FRONT the rest. So the cut runs along the model's own edges - no depth-aliased
// staircase across a finger - and is picked again from the pose every frame, so no table goes stale when the
// owner turns a hand. Link's hand lists draw with G_TRI2 only (21 in the adult open hand); a TRI2 whose two halves
// fall on different sides becomes the one TRI1 that stays. `count` gains the triangles kept. Null when none are.
template <typename SlotMf>
static const Gfx* SplitList(GraphicsContext* gfxCtx, const Gfx* src, const MtxF* own, const SlotMf& mfForSlot, bool back,
                            int32_t* count) {
    size_t words = 0;
    for (const Gfx* g = src; words < 4096; g++) {
        words++;
        const uint8_t op = (uint8_t)(g->words.w0 >> 24);
        if (op == (uint8_t)G_ENDDL) {
            break;
        }
        if (IsWide(op)) {
            g++;
            words++;
        }
    }
    Gfx* out = (Gfx*)Graph_Alloc(gfxCtx, words * sizeof(Gfx));
    float z[64] = {};
    int8_t slot = -1;
    int32_t kept = 0;
    auto behind = [&](uint32_t w) {
        const int32_t a = (int32_t)((w >> 16) & 0xFF) / 2, b = (int32_t)((w >> 8) & 0xFF) / 2, c = (int32_t)(w & 0xFF) / 2;
        return z[a & 63] + z[b & 63] + z[c & 63] < 0.0f;
    };
    size_t n = 0;
    for (const Gfx* g = src; n < words; g++) {
        const uint8_t op = (uint8_t)(g->words.w0 >> 24);
        Gfx copy = *g;
        if (op == (uint8_t)G_MTX) {
            const uintptr_t addr = g->words.w1;
            slot = (addr >> 24) == 0x0D ? (int8_t)(((addr & 0xFFFFFF) & ~(uintptr_t)1) / sizeof(Mtx)) : -1;
        } else if (op == (uint8_t)G_VTX_OTR_HASH) {
            const int32_t nv = (int32_t)((g->words.w0 >> 12) & 0xFF);
            const int32_t v0 = (int32_t)((g->words.w0 >> 1) & 0x7F) - nv;
            const Vtx* v = HashVerts(g);
            const MtxF* mf = mfForSlot(slot, own);
            for (int32_t k = 0; v != nullptr && k < nv; k++) {
                Vec3f p = { (float)v[k].v.ob[0], (float)v[k].v.ob[1], (float)v[k].v.ob[2] };
                Vec3f d;
                SkinMatrix_Vec3fMtxFMultXYZ((MtxF*)mf, &p, &d);
                z[(v0 + k) & 63] = d.z;
            }
        } else if (op == (uint8_t)G_TRI1) {
            if (behind((uint32_t)g->words.w0) != back) {
                copy = gsSPNoOp();
            } else {
                kept++;
            }
        } else if (op == (uint8_t)G_TRI2) {
            const bool keepA = behind((uint32_t)g->words.w0) == back;
            const bool keepB = behind((uint32_t)g->words.w1) == back;
            if (keepA && !keepB) {
                copy.words.w0 = ((uintptr_t)G_TRI1 << 24) | (g->words.w0 & 0xFFFFFF);
                copy.words.w1 = 0;
            } else if (!keepA && keepB) {
                copy.words.w0 = ((uintptr_t)G_TRI1 << 24) | (g->words.w1 & 0xFFFFFF);
                copy.words.w1 = 0;
            } else if (!keepA && !keepB) {
                copy = gsSPNoOp();
            }
            kept += (keepA ? 1 : 0) + (keepB ? 1 : 0);
        }
        out[n++] = copy;
        if (op == (uint8_t)G_ENDDL) {
            break;
        }
        if (IsWide(op) && n < words) {
            g++;
            out[n++] = *g;
        }
    }
    *count += kept;
    return kept > 0 ? out : nullptr;
}

static void ToScreen(const MtxF* mf, const Vec3s& p, float ext[4]) {
    Vec3f src = { (float)p.x, (float)p.y, (float)p.z };
    Vec3f dst;
    SkinMatrix_Vec3fMtxFMultXYZ((MtxF*)mf, &src, &dst);
    const float sx = dst.x + (float)(SCREEN_WIDTH / 2);
    const float sy = (float)(SCREEN_HEIGHT / 2) - dst.y;
    ext[0] = std::min(ext[0], sx);
    ext[1] = std::min(ext[1], sy);
    ext[2] = std::max(ext[2], sx);
    ext[3] = std::max(ext[3], sy);
}

void RsLinkHands_Draw(PlayState* play, GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int32_t side, float gripX,
                      float gripY, float levelBlend) {
    const int32_t set = LINK_IS_ADULT ? 0 : 1;
    const RsHandSet& hs = kHandSets[set];
    const int32_t grip = Grip();
    const RsLinkHandPose pose = ReadPose(side);
    const float scale = SetScale(set);
    const bool loaded = LoadSkeleton(play);

    // The limbs on this side: Link's left on the screen's left.
    const int32_t limbUpper = side == 0 ? PLAYER_LIMB_L_SHOULDER : PLAYER_LIMB_R_SHOULDER;
    const int32_t limbFore = side == 0 ? PLAYER_LIMB_L_FOREARM : PLAYER_LIMB_R_FOREARM;
    const int32_t limbHand = side == 0 ? PLAYER_LIMB_L_HAND : PLAYER_LIMB_R_HAND;
    const Vec3s zero = { 0, 0, 0 };
    const Vec3s& jpHand = loaded ? sSkel.jointPos[limbHand] : zero;
    const Vec3s& jpFore = loaded ? sSkel.jointPos[limbFore] : zero;
    const Vec3s& jpUpper = loaded ? sSkel.jointPos[limbUpper] : zero;
    const int32_t slots = loaded ? sSkel.slots : 1;
    // One more than the skeleton's slots: the last is where a limb with no slot (a skeleton that did not load) has
    // its ToMtx written, so the op is still emitted and the interpolator replays into memory that outlives the call.
    Mtx* mtx = (Mtx*)Graph_Alloc(gfxCtx, (slots + 1) * sizeof(Mtx));

    // THE CHAIN, the same ops on every call: grip -> hand -> forearm -> upper arm -> every other slot.
    MtxF mfHand, mfFore, mfUpper, mfRest;
    const int32_t slotHand = loaded ? sSkel.slotOf[limbHand] : -1;
    const int32_t slotFore = loaded ? sSkel.slotOf[limbFore] : -1;
    const int32_t slotUpper = loaded ? sSkel.slotOf[limbUpper] : -1;
    auto toMtx = [&](int32_t slot) {
        Matrix_ToMtx(slot >= 0 && slot < slots ? &mtx[slot] : &mtx[slots], (char*)__FILE__, __LINE__);
    };

    Matrix_Push();
    // The paper knob moves the hand, not the paper: the scroll's depth is z 0 wherever it is drawn, so a hand set
    // back by `paper` puts that much more of itself behind it. Always emitted, 0 or not.
    const float paper = Paper();
    Matrix_Translate(gripX - (float)(SCREEN_WIDTH / 2) + pose.offset[0],
                     (float)(SCREEN_HEIGHT / 2) - gripY - pose.offset[1], -paper, MTXMODE_APPLY);
    Matrix_RotateZ((pose.rot[2] + pose.vturn * levelBlend) * kDeg, MTXMODE_APPLY);
    Matrix_RotateY(pose.rot[1] * kDeg, MTXMODE_APPLY);
    Matrix_RotateX(pose.rot[0] * kDeg, MTXMODE_APPLY);
    Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
    Matrix_Translate(-hs.gripPoint[grip][0], -hs.gripPoint[grip][1], -hs.gripPoint[grip][2], MTXMODE_APPLY);
    toMtx(slotHand);
    Matrix_Get(&mfHand);
    // The forearm, turned at the wrist and stepped back so its wrist point lands on the hand's origin. First the
    // turn's bend: sin(pi * blend) is 0 at both ends of the level turn and 1 at its middle.
    const float turnBend = std::sin(3.14159265f * std::clamp(levelBlend, 0.0f, 1.0f));
    Matrix_RotateX(pose.turnBend * turnBend * kDeg, MTXMODE_APPLY);
    Matrix_RotateZ(pose.wrist[2] * kDeg, MTXMODE_APPLY);
    Matrix_RotateY(pose.wrist[1] * kDeg, MTXMODE_APPLY);
    Matrix_RotateX(pose.wrist[0] * kDeg, MTXMODE_APPLY);
    Matrix_Translate(-(float)jpHand.x, -(float)jpHand.y, -(float)jpHand.z, MTXMODE_APPLY);
    toMtx(slotFore);
    Matrix_Get(&mfFore);
    // The upper arm, bent at the elbow (the forearm's origin) and stepped back the same way.
    Matrix_RotateZ(pose.elbow * kDeg, MTXMODE_APPLY);
    // Reach: the upper arm stretched along its own x about the elbow (this point), before stepping back to its origin.
    const float reach = Reach();
    Matrix_Scale(reach, 1.0f, 1.0f, MTXMODE_APPLY);
    Matrix_Translate(-(float)jpFore.x, -(float)jpFore.y, -(float)jpFore.z, MTXMODE_APPLY);
    toMtx(slotUpper);
    Matrix_Get(&mfUpper);
    // Everything else - the torso the shoulder welds to - one step further back, unrotated.
    Matrix_Translate(-(float)jpUpper.x, -(float)jpUpper.y, -(float)jpUpper.z, MTXMODE_APPLY);
    Matrix_Get(&mfRest);
    for (int32_t s = 0; s < slots; s++) {
        if (!loaded || (s != slotHand && s != slotFore && s != slotUpper)) {
            Matrix_ToMtx(&mtx[s], (char*)__FILE__, __LINE__);
        }
    }
    Matrix_Pop();

    sStatus.set = hs.id;
    sStatus.grip = kGripNames[grip];
    sStatus.upper = Upper();
    sStatus.scale = scale;
    sStatus.pose[side] = pose;
    sStatus.slots = loaded ? sSkel.slots : 0;
    sStatus.limbSlot[side][0] = slotUpper;
    sStatus.limbSlot[side][1] = slotFore;
    sStatus.limbSlot[side][2] = slotHand;
    sStatus.loads = sSkelLoads;
    ReadLight(sStatus.light, &sStatus.ambient);
    sStatus.occlude = Occlude();
    sStatus.paper = paper;
    sStatus.reach = reach;
    sStatus.trim = Trim();
    sStatus.trimmed[side] = 0;
    if (!loaded || slotHand < 0 || slotFore < 0 || slotUpper < 0) {
        sStatus.dlists[side] = 0;
        sStatus.verts[side] = 0;
        return;
    }

    // --- the state: Link's own (setup 25 - two-cycle, fog, z, lit, culled - as Player_DrawPauseImpl has it) ---
    Gfx setup[4];
    Gfx* setupEnd = Gfx_SetupDL(setup, SETUPDL_25);
    dl.push_back(gsDPPipeSync());
    for (Gfx* g = setup; g < setupEnd; g++) {
        dl.push_back(*g);
    }
    dl.push_back(gsSPSegment(0x0C, gCullBackDList));
    dl.push_back(gsSPSegment(0x0D, mtx));
    // Fog pushed to the far plane: the lists turn fog on themselves (G_FOG and a FOG_SHADE_A blender), so it has
    // to be made to read zero. NOT the pause portrait's 997-1000 (Gfx_SetFog2): 128000/3 overflows the s16
    // multiplier gsSPFogPosition packs, and under this ortho the wrapped pair fogged every vertex fully to the fog
    // colour - the first build drew both arms solid black. 990-1000 packs as 12800 and -12544, which is at or
    // below zero for every depth the ortho can produce (post-divide z up to +1).
    dl.push_back(gsDPSetFogColor(0, 0, 0, 0));
    dl.push_back(gsSPFogPosition(990, 1000));
    // One directional light, fixed to the screen (light directions are in view space), and an ambient.
    Lights1* lights = (Lights1*)Graph_Alloc(gfxCtx, sizeof(Lights1));
    std::memset(lights, 0, sizeof(Lights1));
    {
        const float* d = sStatus.light;
        const float len = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float k = len > 0.0f ? 120.0f / len : 0.0f;
        const u8 amb = (u8)sStatus.ambient;
        for (int32_t c = 0; c < 3; c++) {
            lights->a.l.col[c] = lights->a.l.colc[c] = amb;
            lights->l[0].l.col[c] = lights->l[0].l.colc[c] = 255;
            lights->l[0].l.dir[c] = (s8)(d[c] * k);
        }
    }
    dl.push_back(gsSPNumLights(NUMLIGHTS_1));
    dl.push_back(gsSPLight(&lights->l[0], 1));
    dl.push_back(gsSPLight(&lights->a, 2));

    // Which pieces are worn, in table order, for the variant string and the draw.
    std::string variant;
    bool worn[kArmPieceCount] = {};
    for (int32_t i = 0; i < kArmPieceCount; i++) {
        const RsArmPiece& piece = kArmPieces[i];
        worn[i] = (piece.sets & (1 << set)) != 0 && piece.worn();
        if (worn[i]) {
            variant += (variant.empty() ? "" : "+") + std::string(piece.id);
        }
    }
    sStatus.variant = variant.empty() ? "none" : variant;

    // The draw, as a list of items in two passes (owner look 2). A HAND item is a list under the hand's own matrix
    // - the hand and its gauntlet plate - and is split by triangle; every other item (sleeve, forearm, bracer,
    // bracelet) is drawn whole in front. Items carry their environment colour (the tunic for the sleeve, the metal
    // for a plate) because the two passes draw them in a different order from the table.
    struct Item {
        const char* name;
        int32_t slot;
        const MtxF* own;
        bool trim;
        bool hasColour;
        Color_RGB8 colour;
        bool hand;
    };
    std::vector<Item> items;
    Color_RGB8 tunic{};
    bool hasTunic = false;
    for (int32_t i = 0; i < kArmPieceCount; i++) {
        if (worn[i] && kArmPieces[i].stage == RS_ARM_TINT && kArmPieces[i].colour != nullptr &&
            kArmPieces[i].colour(&tunic)) {
            hasTunic = true;
        }
    }
    if (Upper()) {
        items.push_back({ hs.upper[side], slotUpper, &mfUpper, true, hasTunic, tunic, false });
    }
    items.push_back({ hs.forearm[side], slotFore, &mfFore, false, false, {}, false });
    items.push_back({ hs.hand[side][grip], slotHand, &mfHand, false, false, {}, true });
    for (int32_t i = 0; i < kArmPieceCount; i++) {
        const RsArmPiece& piece = kArmPieces[i];
        if (!worn[i] || piece.stage != RS_ARM_OVERLAY) {
            continue;
        }
        Color_RGB8 c{};
        const bool hasColour = piece.colour != nullptr && piece.colour(&c);
        for (int32_t n = 0; n < kPieceLists; n++) {
            if (piece.dl[side][grip][n] != nullptr) {
                items.push_back({ piece.dl[side][grip][n], slotHand, &mfHand, false, hasColour, c, false });
            }
        }
    }

    auto mfForSlot = [&](int8_t slot, const MtxF* own) -> const MtxF* {
        if (slot < 0) {
            return own;
        }
        if (slot == slotHand) {
            return &mfHand;
        }
        if (slot == slotFore) {
            return &mfFore;
        }
        if (slot == slotUpper) {
            return &mfUpper;
        }
        return &mfRest;
    };
    // Resolve each item once: a resource name is a string, which only the OPEN_DISPS gSPDisplayList turns into a
    // by-name load; pushed raw into this heap list, the interpreter would run the path's bytes as commands. A plate
    // is a HAND item when every vertex it loads is under the hand's slot (Plate2 and Plate3; Plate1, the bracer, is
    // under the forearm's).
    std::vector<const Gfx*> resolved(items.size(), nullptr);
    float ext[4] = { 1e9f, 1e9f, -1e9f, -1e9f };
    int32_t lists = 0;
    int32_t verts = 0;
    for (size_t i = 0; i < items.size(); i++) {
        const Gfx* list = (const Gfx*)ResourceGetDataByName(items[i].name);
        if (list == nullptr) {
            continue;
        }
        if (items[i].trim) {
            int32_t moved = 0;
            list = TrimmedList(list, set, side, &moved);
            sStatus.trimmed[side] += moved;
        }
        resolved[i] = list;
        const RsListVerts& lv = ListVerts(list);
        bool underHand = !lv.slot.empty();
        for (size_t k = 0; k < lv.pos.size(); k++) {
            ToScreen(mfForSlot(lv.slot[k], items[i].own), lv.pos[k], ext);
            if (lv.slot[k] != slotHand && !(lv.slot[k] < 0 && items[i].own == &mfHand)) {
                underHand = false;
            }
        }
        items[i].hand = items[i].hand || underHand;
        lists++;
        verts += (int32_t)lv.pos.size();
    }

    const bool occlude = Occlude();
    const bool byTris = occlude && Cut() == kCutTris;
    int32_t front = 0;
    int32_t back = 0;
    auto emit = [&](size_t i, const Gfx* list) {
        dl.push_back(gsDPPipeSync());
        if (items[i].hasColour) {
            dl.push_back(gsDPSetEnvColor(items[i].colour.r, items[i].colour.g, items[i].colour.b, 0));
        }
        dl.push_back(gsSPMatrix(&mtx[items[i].slot], G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
        dl.push_back(gsSPDisplayList((Gfx*)list));
    };
    // PASS 1, behind (the far half, tested against the scroll): the hand's back triangles; with `cut pixel` the whole
    // hand, cut by depth per pixel as the first holding build did.
    if (occlude) {
        for (size_t i = 0; i < items.size(); i++) {
            if (resolved[i] == nullptr || !items[i].hand) {
                continue;
            }
            if (!byTris) {
                emit(i, resolved[i]);
                continue;
            }
            const Gfx* half = SplitList(gfxCtx, resolved[i], items[i].own, mfForSlot, true, &back);
            if (half != nullptr) {
                emit(i, half);
            }
        }
    }
    // PASS 2, in front (the near half): the arm whole, and the hand's front triangles - or the whole hand when the
    // scroll hides nothing.
    dl.push_back(gsSPMatrix(HandsOrtho(gfxCtx, true), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));
    for (size_t i = 0; i < items.size(); i++) {
        if (resolved[i] == nullptr) {
            continue;
        }
        if (!items[i].hand || !occlude) {
            emit(i, resolved[i]);
        } else if (byTris) {
            const Gfx* half = SplitList(gfxCtx, resolved[i], items[i].own, mfForSlot, false, &front);
            if (half != nullptr) {
                emit(i, half);
            }
        }
    }
    // The far half back, for whatever comes next under this projection: the next side's roll and its depth.
    dl.push_back(gsSPMatrix(HandsOrtho(gfxCtx, false), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));

    sStatus.tris[side][0] = front;
    sStatus.tris[side][1] = back;
    sStatus.dlists[side] = lists;
    sStatus.verts[side] = verts;
    for (int32_t k = 0; k < 4; k++) {
        sStatus.extent[side][k] = ext[k];
    }
}

RsLinkHandsStatus RsLinkHands_Status() {
    return sStatus;
}

// --- the console: `menu hand ...` -------------------------------------------------------------------------------

std::string RsLinkHands_Describe() {
    const RsLinkHandsStatus& s = sStatus;
    char buf[1024];
    int32_t n = std::snprintf(buf, sizeof(buf),
                              "set=%s drawn=%s grip=%s variant=%s upper=%d scale=%.4f slots=%d loads=%d "
                              "light=%.0f,%.0f,%.0f ambient=%d occlude=%d cut=%s paper=%.1f reach=%.2f trim=%d trimmed=%d,%d "
                              "tris_front=%d,%d tris_back=%d,%d",
                              UseBoxes() ? "boxes" : kHandSets[LINK_IS_ADULT ? 0 : 1].id, s.set, kGripNames[Grip()],
                              s.variant.c_str(), Upper() ? 1 : 0,
                              s.scale, s.slots, s.loads, s.light[0], s.light[1], s.light[2], s.ambient,
                              Occlude() ? 1 : 0, Cut() == kCutTris ? "tris" : "pixel", Paper(), Reach(), Trim() ? 1 : 0,
                              s.trimmed[0], s.trimmed[1], s.tris[0][0], s.tris[1][0], s.tris[0][1], s.tris[1][1]);
    for (int32_t side = 0; side < 2 && n > 0 && n < (int32_t)sizeof(buf); side++) {
        const RsLinkHandPose p = ReadPose(side);
        const char* k = side == 0 ? "l" : "r";
        n += std::snprintf(buf + n, sizeof(buf) - n,
                           " %s_rot=%.1f,%.1f,%.1f %s_offset=%.1f,%.1f %s_wrist=%.1f,%.1f,%.1f %s_elbow=%.1f "
                           "%s_vturn=%.1f %s_turnbend=%.1f %s_lists=%d %s_verts=%d %s_extent=%.1f,%.1f,%.1f,%.1f %s_slots=%d,%d,%d",
                           k, p.rot[0], p.rot[1], p.rot[2], k, p.offset[0], p.offset[1], k, p.wrist[0], p.wrist[1],
                           p.wrist[2], k, p.elbow, k, p.vturn, k, p.turnBend, k, s.dlists[side], k, s.verts[side], k,
                           s.extent[side][0], s.extent[side][1], s.extent[side][2], s.extent[side][3], k,
                           s.limbSlot[side][0], s.limbSlot[side][1], s.limbSlot[side][2]);
    }
    return buf;
}

namespace {

void Addl(std::vector<std::string>& lines, const char* op, int32_t rc, const char* error) {
    char head[96];
    if (error != nullptr) {
        std::snprintf(head, sizeof(head), "op=hand %s result=error error=%s ", op, error);
    } else {
        std::snprintf(head, sizeof(head), "op=hand %s rc=%d ", op, rc);
    }
    lines.push_back(std::string(head) + RsLinkHands_Describe());
}

bool ParseFloat(const std::string& word, float* out) {
    if (word.empty()) {
        return false;
    }
    char* end = nullptr;
    const float v = std::strtof(word.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(v)) {
        return false;
    }
    *out = v;
    return true;
}

// `l`, `r` or `both` -> a side mask.
bool ParseSides(const std::string& word, int32_t* mask) {
    if (word == "l" || word == "left") {
        *mask = 1;
    } else if (word == "r" || word == "right") {
        *mask = 2;
    } else if (word == "both") {
        *mask = 3;
    } else {
        return false;
    }
    return true;
}

// `<sides> <v>...` into fields, e.g. rot both 0 0 10.
int32_t SetSideFields(const std::vector<std::string>& args, const char* op, const char* const* fields, int32_t count,
                      std::vector<std::string>& lines) {
    int32_t mask = 0;
    if (args.size() != (size_t)(2 + count) || !ParseSides(args[1], &mask)) {
        Addl(lines, op, 1, "usage");
        return 1;
    }
    float v[3] = {};
    for (int32_t i = 0; i < count; i++) {
        if (!ParseFloat(args[2 + i], &v[i])) {
            Addl(lines, op, 1, "not_a_number");
            return 1;
        }
    }
    for (int32_t side = 0; side < 2; side++) {
        if (mask & (1 << side)) {
            for (int32_t i = 0; i < count; i++) {
                SetSideFloat(side, fields[i], v[i]);
            }
        }
    }
    Addl(lines, op, 0, nullptr);
    return 0;
}

void ClearAll() {
    static const char* const kFields[] = { "RotX",   "RotY",   "RotZ",    "WristX",  "WristY",
                                           "WristZ", "OffsetX", "OffsetY", "Elbow",   "VTurn",
                                           "TurnBend" };
    for (int32_t side = 0; side < 2; side++) {
        for (const char* f : kFields) {
            char name[96];
            std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.%s"), kSideKey[side], f);
            CVarClear(name);
        }
    }
    for (const char* name : { CVAR_RS_HAND("Grip"), CVAR_RS_HAND("Upper"), CVAR_RS_HAND("ScaleAdult"),
                              CVAR_RS_HAND("ScaleChild"), CVAR_RS_HAND("LightX"), CVAR_RS_HAND("LightY"),
                              CVAR_RS_HAND("LightZ"), CVAR_RS_HAND("Ambient"), CVAR_RS_HAND("Boxes"),
                              CVAR_RS_HAND("Occlude"), CVAR_RS_HAND("Paper"), CVAR_RS_HAND("Trim"),
                              CVAR_RS_HAND("Cut"), CVAR_RS_HAND("Reach") }) {
        CVarClear(name);
    }
}

} // namespace

int32_t RsLinkHands_Console(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    // args[0] is the verb after `menu hand`; empty prints the state.
    if (args.empty()) {
        Addl(lines, "show", 0, nullptr);
        return 0;
    }
    const std::string& verb = args[0];
    if (verb == "set" && args.size() == 2 && (args[1] == "link" || args[1] == "boxes")) {
        CVarSetInteger(CVAR_RS_HAND("Boxes"), args[1] == "boxes" ? 1 : 0);
        Addl(lines, "set", 0, nullptr);
        return 0;
    }
    if (verb == "grip" && args.size() == 2 && (args[1] == "fist" || args[1] == "open")) {
        CVarSetInteger(CVAR_RS_HAND("Grip"), args[1] == "open" ? 1 : 0);
        Addl(lines, "grip", 0, nullptr);
        return 0;
    }
    if (verb == "upper" && args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_HAND("Upper"), args[1] == "on" ? 1 : 0);
        Addl(lines, "upper", 0, nullptr);
        return 0;
    }
    if (verb == "occlude" && args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_HAND("Occlude"), args[1] == "on" ? 1 : 0);
        Addl(lines, "occlude", 0, nullptr);
        return 0;
    }
    if (verb == "cut" && args.size() == 2 && (args[1] == "tris" || args[1] == "pixel")) {
        CVarSetInteger(CVAR_RS_HAND("Cut"), args[1] == "tris" ? kCutTris : kCutPixel);
        Addl(lines, "cut", 0, nullptr);
        return 0;
    }
    if (verb == "trim" && args.size() == 2 && (args[1] == "on" || args[1] == "off")) {
        CVarSetInteger(CVAR_RS_HAND("Trim"), args[1] == "on" ? 1 : 0);
        Addl(lines, "trim", 0, nullptr);
        return 0;
    }
    if (verb == "paper" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v)) {
            Addl(lines, "paper", 1, "not_a_number");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("Paper"), v);
        Addl(lines, "paper", 0, nullptr);
        return 0;
    }
    if (verb == "scale" && args.size() == 3 && (args[1] == "adult" || args[1] == "child")) {
        float v = 0.0f;
        if (!ParseFloat(args[2], &v) || v <= 0.0f) {
            Addl(lines, "scale", 1, "not_a_scale");
            return 1;
        }
        CVarSetFloat(args[1] == "adult" ? CVAR_RS_HAND("ScaleAdult") : CVAR_RS_HAND("ScaleChild"), v);
        Addl(lines, "scale", 0, nullptr);
        return 0;
    }
    if (verb == "rot") {
        static const char* const f[] = { "RotX", "RotY", "RotZ" };
        return SetSideFields(args, "rot", f, 3, lines);
    }
    if (verb == "wrist") {
        static const char* const f[] = { "WristX", "WristY", "WristZ" };
        return SetSideFields(args, "wrist", f, 3, lines);
    }
    if (verb == "offset") {
        static const char* const f[] = { "OffsetX", "OffsetY" };
        return SetSideFields(args, "offset", f, 2, lines);
    }
    if (verb == "elbow") {
        static const char* const f[] = { "Elbow" };
        return SetSideFields(args, "elbow", f, 1, lines);
    }
    if (verb == "turnbend") {
        static const char* const f[] = { "TurnBend" };
        return SetSideFields(args, "turnbend", f, 1, lines);
    }
    if (verb == "reach" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < 0.1f) {
            Addl(lines, "reach", 1, "not_a_reach");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("Reach"), v);
        Addl(lines, "reach", 0, nullptr);
        return 0;
    }
    if (verb == "vturn") {
        static const char* const f[] = { "VTurn" };
        return SetSideFields(args, "vturn", f, 1, lines);
    }
    if (verb == "light" && (args.size() == 4 || args.size() == 5)) {
        float v[4] = {};
        for (size_t i = 1; i < args.size(); i++) {
            if (!ParseFloat(args[i], &v[i - 1])) {
                Addl(lines, "light", 1, "not_a_number");
                return 1;
            }
        }
        CVarSetFloat(CVAR_RS_HAND("LightX"), v[0]);
        CVarSetFloat(CVAR_RS_HAND("LightY"), v[1]);
        CVarSetFloat(CVAR_RS_HAND("LightZ"), v[2]);
        if (args.size() == 5) {
            CVarSetInteger(CVAR_RS_HAND("Ambient"), std::clamp((int32_t)v[3], 0, 255));
        }
        Addl(lines, "light", 0, nullptr);
        return 0;
    }
    if (verb == "reset" && args.size() == 1) {
        ClearAll();
        Addl(lines, "reset", 0, nullptr);
        return 0;
    }
    Addl(lines, verb.c_str(), 1, "usage");
    return 1;
}
