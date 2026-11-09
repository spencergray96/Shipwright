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
// Owner look 8: the right's bend is off too - with the anchor's single smooth path it read better without it.
static const float kDefaultTurnBend[2] = { 0.0f, 0.0f };

// The default pose, from the owner's first live session (2026-11-08): hand upright (fingers up the screen, the hand
// limb's +y), a straight wrist (the forearm's +x turned onto the hand's +y) and a straight elbow, so the arm hangs
// straight down from the grip and off the bottom edge - and each hand turned a quarter about the screen's vertical
// (rot y -90), so the backs of both hands face the screen's outer edges. One sign for both: the left hand showed
// its back and the right its palm, so the same turn sends the two backs opposite ways.
static const RsLinkHandPose kDefaultPose = { { 0.0f, -90.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 0.0f, 90.0f }, 0.0f, 0.0f,
                                              0.0f };
// Where each grip sits, from the owner's look (2026-11-08): both 25 down the roll, the right one 5 in toward the page.
// Asymmetric on purpose. In the hand's own frame, which turns with the roll.
static const float kDefaultOffset[2][2] = { { 0.0f, 25.0f }, { -6.0f, 25.0f } };
// THE KEY TABLES (owner look 5, tuned frame by frame with `menu level hold down <t>`): through the level gesture each
// hand's offset and paper come from a table by tick, 0 to kLevelTicks (24), not from one flat value. The gesture's
// matrices are rebuilt each tick and the frame interpolator blends between ticks, so 60 and 144 fps stay smooth.
// The DOWN table is the source; the UP table, indexed by `hold up <t>`, defaults to the down entry for the same pose
// (down 24 - t), the same pose for the same turn angle, and any entry of it can be set on its own. At rest the down
// table's 0 (flat) or 24 (upright) is used. `menu hand key` and `keypaper` set one entry; `menu hand keys` prints them.
// LEFT, offsets in the hand's own frame (x, y down the roll): flat 0,25 to tick 9, then the hand climbs its roll
// through the second half of the turn.
constexpr int32_t kKeyTicks = 25;
static const float kDefaultKeyLeft[kKeyTicks][2] = {
    { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 }, { 0, 25 },
    { 0, 25 }, { 2, 25 }, { 2, 17 }, { 2, 10 }, { 2, 7 },  { 2, 6 },  { 2, 6 },  { 2, 6 },  { 2, 6 },
    { 2, 6 },  { 2, 6 },  { 2, 6 },  { 2, 6 },  { 2, 6 },  { 2, 6 },  { 2, 6 },
};
// LEFT paper (owner look 5, frame by frame): 10 to tick 11, then 14 from tick 12 - at the ramp's 10-13 over ticks
// 12-14 the index finger, skin and gauntlet alike, popped over the bottom roll; 14 keeps it behind. The thumb, which 14
// set partly behind the roll's lower edge, is exempt (THE THUMB). The RIGHT hand has no table defaults: its entries fall back to its flat
// `offset` and the global `paper` until its own look.
static const float kDefaultKeyPaperLeft[kKeyTicks] = { 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 10, 14,
                                                       14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14, 14 };
// THE ROUND ROLL (owner look 5, the GREEN edge): the rolls' depth occluders were flat quads at the paper plane, so a
// finger only just in front of that plane painted over a roll right to its edge, and the per-triangle split then drew
// whole straddling triangles over it as a sawtooth. Each roll now writes the depth of the cylinder it draws (this
// radius toward the viewer on its centre line, 0 at its edges), and `cut mixed` sends every hand triangle that is
// not wholly in front of that bulge through the depth-tested pass. Default 0 (owner look 5): paper sets the hands back
// INTO the rolls by design, so any bulge hid the left palm's base at rest and pinched the thumb upright; 2 bought little
// at rest. Per side and per tick through `menu hand keyroll`, for the frames where a bulge helps.
constexpr float kDefaultRollRadius = 0.0f;
// RIGHT, the owner's look-8 pass, frame by frame (saved in the run record as look8-hand-tuned-tables.txt): the
// fallback when the right hand is in keys mode, and where its anchors are derived from (THE ANCHOR).
static const float kDefaultKeyRight[kKeyTicks][2] = {
    { -6, 25 },        { -6, 25 },       { -6, 25 },       { -6, 25 },       { -6, 25 },       { -6, 25 },
    { -6, 25 },        { -6, 25 },       { -6, 25 },       { -6, 25 },       { -8, 26 },       { -9.2f, 24.4f },
    { -11.7f, 19.6f }, { -14.1f, 11.6f }, { -5.6f, 8.4f }, { -5.6f, 8.4f }, { -5.6f, 8.4f }, { -5.6f, 8.4f },
    { -5.6f, 8.4f },   { -5.6f, 8.4f },  { -5.6f, 8.4f },  { -5.6f, 8.4f },  { -5.6f, 8.4f },  { -5.6f, 8.4f },
    { -5.6f, 8.4f },
};
static const float kDefaultKeyPaperRight[kKeyTicks] = { 8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8,  8, 12,
                                                        12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12 };
// THE WRIST HOLE (owner look 4): the hand limb's mesh is open at the wrist - in Link's own poses the forearm's bracer
// fills it - and at the grip pose a corner of that opening, at the base of the palm beside the bracer's tip, faced the
// screen, so a speck of the parchment behind showed THROUGH the hand. It is not the paper's depth or the triangle split
// (it stays with `occlude off`), nor back-face culling (drawing both faces left it); hiding the hand's third triangle
// widens it into a hole onto the parchment. It moved with each 1-unit nudge of the grip, so no single overlap closed
// it everywhere.
// The bracer is pushed into the opening two ways, measured over a +-2-unit sweep of both grips (run record, look 4):
//   OVERLAP slides the forearm this many model units along its own axis into the hand (40 = 2.8 game units);
//   GIRTH thickens it about that axis, undone again for the upper arm.
// 40 alone left a notch on the right hand at -2; 1.1 alone, or 1.1 with 10, 20 or 30, left one at +-1 or 2.
constexpr float kDefaultOverlap = 40.0f;
constexpr float kDefaultGirth = 1.1f;
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

static float Paper();

static RsLinkHandPose ReadPose(int32_t side) {
    RsLinkHandPose p;
    static const char* const kRot[3] = { "RotX", "RotY", "RotZ" };
    static const char* const kWrist[3] = { "WristX", "WristY", "WristZ" };
    for (int32_t i = 0; i < 3; i++) {
        p.rot[i] = SideFloat(side, kRot[i], kDefaultPose.rot[i]);
        p.wrist[i] = SideFloat(side, kWrist[i], kDefaultPose.wrist[i]);
    }
    p.offset[0] = SideFloat(side, "OffsetX", kDefaultOffset[side][0]);
    p.offset[1] = SideFloat(side, "OffsetY", kDefaultOffset[side][1]);
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
// Look 5: 8 with the round rolls (`roll 2`) - at 10 the right palm's base sank into the right roll's bulge. The left
// hand's paper comes from its key table, so in practice this is the right hand's.
constexpr float kDefaultPaper = 8.0f;
static float Paper() {
    return CVarGetFloat(CVAR_RS_HAND("Paper"), kDefaultPaper);
}
static float Girth() {
    return std::clamp(CVarGetFloat(CVAR_RS_HAND("Girth"), kDefaultGirth), 0.5f, 2.0f);
}
static float Overlap() {
    return CVarGetFloat(CVAR_RS_HAND("Overlap"), kDefaultOverlap);
}
constexpr int32_t kCutTris = 0;  // the hand split by triangle at the flat plane (look 2)
constexpr int32_t kCutPixel = 1; // the hand cut by depth per pixel (the first holding build)
constexpr int32_t kCutMixed = 2; // wholly in front of the roll's bulge by triangle, the rest per pixel (default)
static const char* const kCutNames[3] = { "tris", "pixel", "mixed" };
static int32_t Cut() {
    return std::clamp(CVarGetInteger(CVAR_RS_HAND("Cut"), kCutMixed), kCutTris, kCutMixed);
}
static float RollRadius() {
    return std::max(0.0f, CVarGetFloat(CVAR_RS_HAND("Roll"), kDefaultRollRadius));
}

// One key-table entry's CVar: L.KeyD12X, R.KeyU03P.
static float KeyCVar(int32_t side, bool up, int32_t tick, char comp) {
    char field[16];
    std::snprintf(field, sizeof(field), "Key%c%02d%c", up ? 'U' : 'D', tick, comp);
    return SideFloat(side, field, NAN);
}
static void SetKeyCVar(int32_t side, bool up, int32_t tick, char comp, float v) {
    char field[16];
    std::snprintf(field, sizeof(field), "Key%c%02d%c", up ? 'U' : 'D', tick, comp);
    SetSideFloat(side, field, v);
}

// THE THUMB (owner look 5): the one digit the owner wants in front of the roll in every pose. At the left hand's paper 14
// (ticks 12 on) its base dipped behind the bottom roll's lower edge and the cut split it, while the fingers needed 14 to
// stay behind. So its triangles are drawn whole in the FRONT pass whatever the cut. They are picked by their index in
// the hand list, per set and grip, as measured with a per-triangle map (run record, look 6); a set or grip with no
// entry has no exemption. Bit n = the list's nth triangle in draw order. `menu hand thumb split` turns it off.
// Per set, grip and SIDE: the two hands' lists are ordered differently (owner look 8; the look-7 map, right half).
static const uint64_t kThumbTris[2][kGripCount][2] = {
    // adult: fist not measured; open, the thumb 36-41 on both (look 6) and its glove-covered root - 16-17 on the left
    // (look 7: the roll's lower edge sliced their top at ticks 12-16), 18-19 on the right (look 8: the gap between the
    // thumb and the glove along the roll's edge; 16-17 never draw on the right)
    { { 0, 0 }, { (0x3FULL << 36) | (1ULL << 16) | (1ULL << 17), (0x3FULL << 36) | (1ULL << 18) | (1ULL << 19) } },
    { { 0, 0 }, { 0, 0 } }, // child: not measured
};
// THE KNUCKLE, the brown face between the index finger and the palm: 5 on the left, 7 on the right. Its pass is its
// own setting per side (`menu hand knuckle <l|r> auto|front|back`): the left's went back (look 7, it poked over the
// roll at tick 11); the right's is checked split/front/back in look 9.
static const int32_t kKnuckleTri[2] = { 5, 7 };
constexpr int32_t kKnuckleAuto = 0;
constexpr int32_t kKnuckleFront = 1;
constexpr int32_t kKnuckleBack = 2;
static const char* const kKnuckleNames[3] = { "auto", "front", "back" };
// The right's 7 BACK (look 9): auto left the owner's notch at ticks 12-13; front filled it but popped a wedge over the
// roll at tick 15; back follows the roll's silhouette at every tick checked (0, 9-16).
constexpr int32_t kDefaultKnuckleRight = 2;
static int32_t KnuckleClass(int32_t side) {
    return std::clamp((int32_t)SideFloat(side, "Knuckle", side == 0 ? (float)kKnuckleBack : (float)kDefaultKnuckleRight),
                      kKnuckleAuto, kKnuckleBack);
}
// THE ANCHOR (owner look 8): per-tick nudges are applied in the hand's turned frame, so a step in them between ticks
// becomes a kink in the grip's path on screen (the right hand's t13). In anchor mode the grip is pinned to a point in
// ITS ROLL'S frame instead - along the roll (up) and in toward the page - moved from a flat anchor to an upright one on
// the turn's own eased curve, so its path is one smooth curve; the hand's turn is still the swivel. Both anchors are
// derived from the side's key table at ticks 0 and 24 unless set, so the two ends look exactly as tuned. Per side:
// `menu hand mode <l|r> anchor|keys`; the left stays on its keys (owner).
static const int32_t kDefaultAnchorMode[2] = { 0, 1 };
static bool AnchorMode(int32_t side) {
    return SideFloat(side, "Anchor", (float)kDefaultAnchorMode[side]) != 0.0f;
}
bool RsLinkHands_Rigid(int32_t side) {
    return SideFloat(side, "AnchorRigid", 0.0f) != 0.0f;
}
static float AnchorCurve(int32_t side) {
    return std::clamp(SideFloat(side, "AnchorCurve", 1.0f), 0.1f, 10.0f);
}
static bool ThumbFront() {
    return CVarGetInteger(CVAR_RS_HAND("Thumb"), 1) != 0;
}
// THE FINGERS (owner look 6, the GREEN edge): the mirror of THE THUMB. The four fingers wrap the roll from behind, so
// they are drawn ALWAYS BEHIND the scroll: in the depth-tested pass under the hand's own matrix pushed kFingerBias game
// units further back, so wherever a finger lies over the roll or the paper the scroll's alpha-cut depth hides it, and
// wherever it lies outside them it draws. The visible cut is then the roll's own silhouette, not the fingers'
// triangle edges. Picked by index like the thumb; `menu hand fingers split` turns it off.
static const uint64_t kFingerTris[2][kGripCount] = {
    // adult: fist not measured; open, the visible finger faces 25, 28, 30 and 34 - the same on both hands (look 7's
    // per-triangle map at rest and ticks 10-16; 28 is the index finger's shaded, gauntlet-coloured face). The knuckle
    // is THE KNUCKLE's.
    { 0, (1ULL << 25) | (1ULL << 28) | (1ULL << 30) | (1ULL << 34) },
    { 0, 0 }, // child: not measured
};
constexpr float kFingerBias = 200.0f;
static bool FingersBack() {
    return CVarGetInteger(CVAR_RS_HAND("Fingers"), 1) != 0;
}
// `menu hand skip <n>`: a diagnostic - the hand list's nth triangle (draw order, from 0) is drawn in no pass. One
// capture per n, diffed against the whole hand, maps which triangle is where; that is how the masks above were found.
static int32_t SkipTri() {
    return CVarGetInteger(CVAR_RS_HAND("Skip"), -1);
}

// The offset x, y, paper and roll depth for one side at one tick of one direction. See THE KEY TABLES.
static void KeyAt(int32_t side, bool up, int32_t tick, const RsLinkHandPose& pose, float out[4]) {
    tick = std::clamp(tick, 0, kKeyTicks - 1);
    if (up) {
        KeyAt(side, false, kKeyTicks - 1 - tick, pose, out);
    } else if (side == 0) {
        out[0] = kDefaultKeyLeft[tick][0];
        out[1] = kDefaultKeyLeft[tick][1];
        out[2] = kDefaultKeyPaperLeft[tick];
    } else {
        out[0] = kDefaultKeyRight[tick][0];
        out[1] = kDefaultKeyRight[tick][1];
        out[2] = kDefaultKeyPaperRight[tick];
    }
    (void)pose;
    if (!up) {
        out[3] = RollRadius();
    }
    static const char kComp[4] = { 'X', 'Y', 'P', 'R' };
    for (int32_t i = 0; i < 4; i++) {
        const float v = KeyCVar(side, up, tick, kComp[i]);
        if (!std::isnan(v)) {
            out[i] = v;
        }
    }
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

// 8: the old cut, point sampled - the default until the owner picks, because no single value is clean on both rolls
// (look 8's sweep on green, hand pixels lost outside the visible scroll at rest): point/8 loses 0 on the left and 67 on
// the right (the reported sliver); bilinear at 112-136 loses 0 on the right but a 1-pixel column (79) at the left
// roll's outer edge at every value; from about 144 the subtract breaks and both lose ~1,000. The two rolls' outer
// edges sit at different sub-texel phases.
constexpr int32_t kDefaultCutAlpha = 8;
// THE SCROLL'S EDGE (look 9): picked by the green metric - hand pixels hidden outside the visible scroll, both hands, at
// ticks 0, 3, 5, 8-16 and 24, close phase included. Point-sampled art: 0 at every tick, both hands (the art and its
// occluder now sample alike, at any sub-texel phase). Bilinear art: 5 left / 569 right in all; inset 0.25 the same;
// inset 0.5 611/941; cutalpha 112 476/269. The art's look barely changes - only its outermost edge pixel hardens.
constexpr int32_t kDefaultScrollPoint = 1;
constexpr float kDefaultRollInset = 0.0f;

bool RsLinkHands_PushOccluderState(std::vector<Gfx>& dl, bool textured) {
    if (!RsLinkHands_Enabled() || !Occlude()) {
        return false;
    }
    dl.push_back(gsDPPipeSync());
    if (textured) {
        // THE CUT'S ALPHA (owner look 7, a green sliver between the right roll's outer edge and the hand): the
        // interpreter's alpha threshold is fixed in the shader at 8/256 and ignores the blend colour, so the occluders
        // wrote depth across the art's soft alpha fringe, which the visible draw (bilinear, blended) shows as almost
        // transparent - hiding hand pixels just outside the visible roll and paper. So the combiner subtracts the
        // wanted cut first: alpha = (TEXEL0 - PRIM) * SHADE (vertex alpha 1), and the fixed test then discards
        // everything below CutAlpha. 8 is the old cut (see kDefaultCutAlpha for the sweep). The art's own alpha is all-or-nothing, so what made the sliver was FILTERING: the visible draw is
        // bilinear, which fades the last texel's outer half toward the backdrop, while the point-sampled occluder
        // covered that whole texel. So above the old cut the occluder samples bilinear too, and its cut at half alpha
        // lands on the visible edge's half-alpha contour. (Point sampling was look 2's BLUE fix, when the cut was the
        // fixed 3%: bilinear then kept the faint fringe and cut fingertips short.)
        const int32_t cut = std::clamp(CVarGetInteger(CVAR_RS_HAND("CutAlpha"), kDefaultCutAlpha), 0, 255);
        dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON));
        dl.push_back(gsDPSetCombineLERP(0, 0, 0, TEXEL0, TEXEL0, PRIMITIVE, SHADE, 0, 0, 0, 0, TEXEL0, TEXEL0,
                                        PRIMITIVE, SHADE, 0));
        dl.push_back(gsDPSetPrimColor(0, 0, 255, 255, 255, (u8)std::max(0, cut - 8)));
    } else {
        dl.push_back(gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF));
        dl.push_back(gsDPSetCombineMode(G_CC_SHADE, G_CC_SHADE));
    }
    const bool bilerp = textured && CVarGetInteger(CVAR_RS_HAND("CutAlpha"), kDefaultCutAlpha) > 8;
    dl.push_back(gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT |
                                      (bilerp ? G_TF_BILERP : G_TF_POINT) | G_TT_NONE |
                                      G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_1CYCLE | G_PM_NPRIMITIVE,
                                  (textured ? G_AC_THRESHOLD : G_AC_NONE) | G_ZS_PIXEL | RS_RM_HAND_OCCLUDER(1) |
                                      RS_RM_HAND_OCCLUDER(2)));
    dl.push_back(gsSPLoadGeometryMode(G_ZBUFFER | G_SHADE | G_SHADING_SMOOTH));
    return true;
}

constexpr int32_t kSplitFront = 0;
constexpr int32_t kSplitBack = 1;
constexpr int32_t kSplitFingers = 2;

// THE PER-TRIANGLE SPLIT (owner look 2, GREEN). A copy of a hand list, made each frame in frame memory, keeping only
// the triangles on one side of the scroll's plane: BACK keeps those whose centre is behind z 0 (screen space, after
// the paper knob's step back), FRONT the rest. So the cut runs along the model's own edges - no depth-aliased
// staircase across a finger - and is picked again from the pose every frame, so no table goes stale when the
// owner turns a hand. Link's hand lists draw with G_TRI2 only (21 in the adult open hand); a TRI2 whose two halves
// fall on different sides becomes the one TRI1 that stays. `count` gains the triangles kept. Null when none are.
// MIXED (owner look 5): FRONT only when all three vertices are at or in front of `frontZ`, the rolls' bulge, and BACK
// - the depth-tested pass, cut per pixel against the paper and the round rolls - otherwise; `straddle` counts the back
// ones that are not wholly behind the plane. Look 2's rule (by the vertices' sum against z 0) without `mixed`.
template <typename SlotMf>
static const Gfx* SplitList(GraphicsContext* gfxCtx, const Gfx* src, const MtxF* own, const SlotMf& mfForSlot, int32_t want,
                            int32_t* count, bool mixed, float frontZ, int32_t* straddle, uint64_t frontMask,
                            uint64_t backMask, int32_t skip) {
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
    // The triangle's pass: kSplitFront, kSplitBack, kSplitFingers, or -1 (skipped). The masks come first - thumb
    // front, fingers back - then the cut decides the rest.
    int32_t triIndex = 0;
    auto classOf = [&](uint32_t w) -> int32_t {
        const int32_t index = triIndex++;
        if (index == skip) {
            return -1;
        }
        if (index < 64 && (frontMask >> index) & 1) {
            return kSplitFront;
        }
        if (index < 64 && (backMask >> index) & 1) {
            return kSplitFingers;
        }
        const int32_t a = (int32_t)((w >> 16) & 0xFF) / 2, b = (int32_t)((w >> 8) & 0xFF) / 2, c = (int32_t)(w & 0xFF) / 2;
        const float za = z[a & 63], zb = z[b & 63], zc = z[c & 63];
        if (!mixed) {
            return za + zb + zc < 0.0f ? kSplitBack : kSplitFront;
        }
        const bool isBack = !(za >= frontZ && zb >= frontZ && zc >= frontZ);
        if (isBack && want == kSplitBack && straddle != nullptr && std::max(za, std::max(zb, zc)) >= 0.0f) {
            (*straddle)++;
        }
        return isBack ? kSplitBack : kSplitFront;
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
            if (classOf((uint32_t)g->words.w0) != want) {
                copy = gsSPNoOp();
            } else {
                kept++;
            }
        } else if (op == (uint8_t)G_TRI2) {
            const bool keepA = classOf((uint32_t)g->words.w0) == want;
            const bool keepB = classOf((uint32_t)g->words.w1) == want;
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

// The grip offset (x right, y down, in the hand's swivelled frame) that puts the grip on roll-frame point `at`
// (x, y up) given the slide and swivel there - the inverse of what RsMenu and the hand's first translate apply.
static void OffsetForRollPoint(const float at[2], const float slide[2], float swivel, float out[2]) {
    const float dx = at[0] - slide[0];
    const float dy = at[1] - slide[1];
    const float c = std::cos(swivel);
    const float s = std::sin(swivel);
    out[0] = dx * c + dy * s;
    out[1] = -(-dx * s + dy * c);
}
static void RollPointForOffset(const float off[2], const float slide[2], float swivel, float out[2]) {
    const float c = std::cos(swivel);
    const float s = std::sin(swivel);
    const float vx = off[0];
    const float vy = -off[1];
    out[0] = slide[0] + vx * c - vy * s;
    out[1] = slide[1] + vx * s + vy * c;
}
// along/in <-> the roll's frame: along is up the roll (+y); in is toward the page, +x on the left roll, -x on the right.
static void AnchorPoint(int32_t side, const char* which, const float derived[2], float out[2]) {
    char fa[24];
    char fi[24];
    std::snprintf(fa, sizeof(fa), "Anchor%sAlong", which);
    std::snprintf(fi, sizeof(fi), "Anchor%sIn", which);
    const float sx = side == 0 ? 1.0f : -1.0f;
    const float along = SideFloat(side, fa, derived[1]);
    const float in = SideFloat(side, fi, derived[0] * sx);
    out[0] = in * sx;
    out[1] = along;
}

void RsLinkHands_Draw(PlayState* play, GraphicsContext* gfxCtx, std::vector<Gfx>& dl, int32_t side, float gripX,
                      float gripY, float levelBlend, int32_t keyTick, bool keyUp, const RsLinkHandsRollFrame& frame) {
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
    // ...and one more after that: the hand's matrix pushed kFingerBias back, for THE FINGERS.
    Mtx* mtx = (Mtx*)Graph_Alloc(gfxCtx, (slots + 2) * sizeof(Mtx));
    Mtx* mtxFingers = &mtx[slots + 1];

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
    // The offset and paper for this tick of the level gesture, from the side's key tables.
    float key[4];
    KeyAt(side, keyUp, keyTick, pose, key);
    // ANCHOR MODE: the key table keeps the paper and roll depth; the offset comes from the anchors.
    sStatus.anchored[side] = AnchorMode(side);
    if (sStatus.anchored[side]) {
        float endKey[4];
        float derivedFlat[2];
        float derivedUp[2];
        KeyAt(side, false, 0, pose, endKey);
        RollPointForOffset(endKey, frame.slideFlat, 0.0f, derivedFlat);
        KeyAt(side, false, kKeyTicks - 1, pose, endKey);
        RollPointForOffset(endKey, frame.slideUpright, frame.swivelUpright, derivedUp);
        float flat[2];
        float up[2];
        AnchorPoint(side, "Flat", derivedFlat, flat);
        AnchorPoint(side, "Up", derivedUp, up);
        const float b = std::pow(std::clamp(frame.blend, 0.0f, 1.0f), AnchorCurve(side));
        const float at[2] = { flat[0] + (up[0] - flat[0]) * b, flat[1] + (up[1] - flat[1]) * b };
        OffsetForRollPoint(at, frame.slide, frame.swivel, key);
        const float sx = side == 0 ? 1.0f : -1.0f;
        sStatus.anchor[side][0] = at[1];
        sStatus.anchor[side][1] = at[0] * sx;
    }
    const float paper = key[2];
    auto handChain = [&](float back) {
        Matrix_Translate(gripX - (float)(SCREEN_WIDTH / 2) + key[0], (float)(SCREEN_HEIGHT / 2) - gripY - key[1],
                         -paper - back, MTXMODE_APPLY);
        Matrix_RotateZ((pose.rot[2] + pose.vturn * levelBlend) * kDeg, MTXMODE_APPLY);
        Matrix_RotateY(pose.rot[1] * kDeg, MTXMODE_APPLY);
        Matrix_RotateX(pose.rot[0] * kDeg, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        Matrix_Translate(-hs.gripPoint[grip][0], -hs.gripPoint[grip][1], -hs.gripPoint[grip][2], MTXMODE_APPLY);
    };
    // THE FINGERS' matrix first: the same chain kFingerBias further back. The side matrix under it turns only about
    // the screen's z, so this is a pure depth shift - the fingers land on the same pixels. Always emitted.
    Matrix_Push();
    handChain(kFingerBias);
    Matrix_ToMtx(mtxFingers, (char*)__FILE__, __LINE__);
    Matrix_Pop();
    handChain(0.0f);
    toMtx(slotHand);
    Matrix_Get(&mfHand);
    // The forearm, turned at the wrist and stepped back so its wrist point lands on the hand's origin. First the
    // turn's bend: sin(pi * blend) is 0 at both ends of the level turn and 1 at its middle.
    const float turnBend = std::sin(3.14159265f * std::clamp(levelBlend, 0.0f, 1.0f));
    Matrix_RotateX(pose.turnBend * turnBend * kDeg, MTXMODE_APPLY);
    Matrix_RotateZ(pose.wrist[2] * kDeg, MTXMODE_APPLY);
    Matrix_RotateY(pose.wrist[1] * kDeg, MTXMODE_APPLY);
    Matrix_RotateX(pose.wrist[0] * kDeg, MTXMODE_APPLY);
    // The wrist seam's overlap: the forearm slid along its own x (elbow to wrist) into the hand. Always emitted.
    // And its girth: the forearm thickened about its own axis, so the bracer's end covers the hand's open wrist.
    const float girth = Girth();
    Matrix_Translate(Overlap(), 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_Scale(1.0f, girth, girth, MTXMODE_APPLY);
    Matrix_Translate(-(float)jpHand.x, -(float)jpHand.y, -(float)jpHand.z, MTXMODE_APPLY);
    toMtx(slotFore);
    Matrix_Get(&mfFore);
    // The girth undone for the upper arm and the rest, about the elbow (this point).
    Matrix_Scale(1.0f, 1.0f / girth, 1.0f / girth, MTXMODE_APPLY);
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
    sStatus.keyTick = keyTick;
    sStatus.keyUp = keyUp;
    for (int32_t i = 0; i < 4; i++) {
        sStatus.key[side][i] = key[i];
    }
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
    const size_t handItem = items.size();
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
    const int32_t cut = Cut();
    const bool byTris = occlude && cut != kCutPixel;
    const bool mixed = cut == kCutMixed;
    const float frontZ = std::max(0.0f, key[3]);
    uint64_t thumbMask = ThumbFront() ? kThumbTris[set][grip][side] : 0;
    sStatus.thumb[side] = kThumbTris[set][grip][side] == 0 ? "none" : thumbMask != 0 ? "front" : "split";
    uint64_t fingerMask = FingersBack() ? kFingerTris[set][grip] : 0;
    sStatus.fingers[side] = kFingerTris[set][grip] == 0 ? "none" : fingerMask != 0 ? "back" : "split";
    if (kFingerTris[set][grip] != 0) {
        const uint64_t knuckle = 1ULL << kKnuckleTri[side];
        const int32_t kc = KnuckleClass(side);
        if (kc == kKnuckleFront) {
            thumbMask |= knuckle;
        } else if (kc == kKnuckleBack) {
            fingerMask |= knuckle;
        }
    }
    const int32_t skip = SkipTri();
    int32_t fingerTris = 0;
    int32_t front = 0;
    int32_t back = 0;
    int32_t straddle = 0;
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
            const bool isHand = i == handItem;
            const Gfx* half = SplitList(gfxCtx, resolved[i], items[i].own, mfForSlot, kSplitBack, &back, mixed, frontZ,
                                        &straddle, isHand ? thumbMask : 0, isHand ? fingerMask : 0, isHand ? skip : -1);
            if (half != nullptr) {
                emit(i, half);
            }
            // THE FINGERS, under their pushed-back matrix, in this same depth-tested pass.
            const Gfx* fingers =
                isHand && fingerMask != 0
                    ? SplitList(gfxCtx, resolved[i], items[i].own, mfForSlot, kSplitFingers, &fingerTris, mixed,
                                frontZ, nullptr, thumbMask, fingerMask, skip)
                    : nullptr;
            if (fingers != nullptr) {
                dl.push_back(gsDPPipeSync());
                dl.push_back(gsSPMatrix(mtxFingers, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW));
                dl.push_back(gsSPDisplayList((Gfx*)fingers));
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
            const bool isHand = i == handItem;
            const Gfx* half = SplitList(gfxCtx, resolved[i], items[i].own, mfForSlot, kSplitFront, &front, mixed, frontZ,
                                        nullptr, isHand ? thumbMask : 0, isHand ? fingerMask : 0, isHand ? skip : -1);
            if (half != nullptr) {
                emit(i, half);
            }
        }
    }
    // The far half back, for whatever comes next under this projection: the next side's roll and its depth.
    dl.push_back(gsSPMatrix(HandsOrtho(gfxCtx, false), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION));

    sStatus.tris[side][0] = front;
    sStatus.tris[side][1] = back;
    sStatus.straddle[side] = straddle;
    sStatus.fingerTris[side] = fingerTris;
    sStatus.dlists[side] = lists;
    sStatus.verts[side] = verts;
    for (int32_t k = 0; k < 4; k++) {
        sStatus.extent[side][k] = ext[k];
    }
}

RsLinkHandsStatus RsLinkHands_Status() {
    return sStatus;
}

bool RsLinkHands_ScrollPoint() {
    return CVarGetInteger(CVAR_RS_HAND("ScrollPoint"), kDefaultScrollPoint) != 0;
}
float RsLinkHands_RollInset() {
    return std::clamp(CVarGetFloat(CVAR_RS_HAND("RollInset"), kDefaultRollInset), 0.0f, 5.0f);
}

float RsLinkHands_RollRadius(int32_t side, int32_t keyTick, bool keyUp) {
    float key[4];
    KeyAt(side, keyUp, keyTick, ReadPose(side), key);
    return std::max(0.0f, key[3]);
}

// --- the console: `menu hand ...` -------------------------------------------------------------------------------

std::string RsLinkHands_Describe() {
    const RsLinkHandsStatus& s = sStatus;
    char buf[2048];
    int32_t n = std::snprintf(buf, sizeof(buf),
                              "set=%s drawn=%s grip=%s variant=%s upper=%d scale=%.4f slots=%d loads=%d "
                              "light=%.0f,%.0f,%.0f ambient=%d occlude=%d cut=%s roll=%.1f paper=%.1f reach=%.2f overlap=%.1f girth=%.2f "
                              "trim=%d trimmed=%d,%d tris_front=%d,%d tris_back=%d,%d straddle=%d,%d tris_fingers=%d,%d skip=%d cutalpha=%d scrollfilter=%s rollinset=%.2f "
                              "key_tick=%d key_dir=%s",
                              UseBoxes() ? "boxes" : kHandSets[LINK_IS_ADULT ? 0 : 1].id, s.set, kGripNames[Grip()],
                              s.variant.c_str(), Upper() ? 1 : 0,
                              s.scale, s.slots, s.loads, s.light[0], s.light[1], s.light[2], s.ambient,
                              Occlude() ? 1 : 0, kCutNames[Cut()], RollRadius(), Paper(), Reach(), Overlap(), Girth(),
                              Trim() ? 1 : 0, s.trimmed[0], s.trimmed[1], s.tris[0][0], s.tris[1][0], s.tris[0][1],
                              s.tris[1][1], s.straddle[0], s.straddle[1], s.fingerTris[0], s.fingerTris[1], SkipTri(),
                              CVarGetInteger(CVAR_RS_HAND("CutAlpha"), kDefaultCutAlpha),
                              RsLinkHands_ScrollPoint() ? "point" : "bilinear", RsLinkHands_RollInset(), s.keyTick,
                              s.keyUp ? "up" : "down");
    for (int32_t side = 0; side < 2 && n > 0 && n < (int32_t)sizeof(buf); side++) {
        const RsLinkHandPose p = ReadPose(side);
        const char* k = side == 0 ? "l" : "r";
        n += std::snprintf(buf + n, sizeof(buf) - n,
                           " %s_rot=%.1f,%.1f,%.1f %s_offset=%.1f,%.1f %s_wrist=%.1f,%.1f,%.1f %s_elbow=%.1f "
                           "%s_vturn=%.1f %s_turnbend=%.1f %s_key=%.1f,%.1f %s_keypaper=%.1f %s_keyroll=%.1f %s_thumb=%s %s_fingers=%s %s_knuckle=%s %s_mode=%s %s_anchor=%.1f,%.1f %s_lists=%d %s_verts=%d %s_extent=%.1f,%.1f,%.1f,%.1f %s_slots=%d,%d,%d",
                           k, p.rot[0], p.rot[1], p.rot[2], k, p.offset[0], p.offset[1], k, p.wrist[0], p.wrist[1],
                           p.wrist[2], k, p.elbow, k, p.vturn, k, p.turnBend, k, s.key[side][0], s.key[side][1], k,
                           s.key[side][2], k, s.key[side][3], k, s.thumb[side] ? s.thumb[side] : "none", k,
                           s.fingers[side] ? s.fingers[side] : "none", k, kKnuckleNames[KnuckleClass(side)], k,
                           AnchorMode(side) ? (RsLinkHands_Rigid(side) ? "anchor-rigid" : "anchor") : "keys", k,
                           s.anchor[side][0], s.anchor[side][1], k,
                           s.dlists[side], k, s.verts[side], k,
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
                                           "TurnBend", "Knuckle", "Anchor", "AnchorRigid", "AnchorCurve",
                                           "AnchorFlatAlong", "AnchorFlatIn", "AnchorUpAlong", "AnchorUpIn" };
    for (int32_t side = 0; side < 2; side++) {
        for (const char* f : kFields) {
            char name[96];
            std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.%s"), kSideKey[side], f);
            CVarClear(name);
        }
        for (int32_t up = 0; up < 2; up++) {
            for (int32_t t = 0; t < kKeyTicks; t++) {
                for (char c : { 'X', 'Y', 'P', 'R' }) {
                    char name[96];
                    std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.Key%c%02d%c"), kSideKey[side], up ? 'U' : 'D', t,
                                  c);
                    CVarClear(name);
                }
            }
        }
    }
    for (const char* name : { CVAR_RS_HAND("Grip"), CVAR_RS_HAND("Upper"), CVAR_RS_HAND("ScaleAdult"),
                              CVAR_RS_HAND("ScaleChild"), CVAR_RS_HAND("LightX"), CVAR_RS_HAND("LightY"),
                              CVAR_RS_HAND("LightZ"), CVAR_RS_HAND("Ambient"), CVAR_RS_HAND("Boxes"),
                              CVAR_RS_HAND("Occlude"), CVAR_RS_HAND("Paper"), CVAR_RS_HAND("Trim"),
                              CVAR_RS_HAND("Cut"), CVAR_RS_HAND("Reach"), CVAR_RS_HAND("Overlap"), CVAR_RS_HAND("Girth"),
                              CVAR_RS_HAND("Roll"), CVAR_RS_HAND("Thumb"),
                              CVAR_RS_HAND("Fingers"), CVAR_RS_HAND("Skip"),
                              CVAR_RS_HAND("CutAlpha"), CVAR_RS_HAND("ScrollPoint"), CVAR_RS_HAND("RollInset") }) {
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
    if (verb == "cut" && args.size() == 2 && (args[1] == "tris" || args[1] == "pixel" || args[1] == "mixed")) {
        CVarSetInteger(CVAR_RS_HAND("Cut"), args[1] == "tris" ? kCutTris : args[1] == "pixel" ? kCutPixel : kCutMixed);
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
    // key <l|r|both> <tick> <dx> <dy> [down|up] / keypaper <l|r|both> <tick> <z> [down|up]
    if (verb == "key" || verb == "keypaper" || verb == "keyroll") {
        const bool isPaper = verb == "keypaper" || verb == "keyroll";
        const char one = verb == "keyroll" ? 'R' : 'P';
        const size_t nv = isPaper ? 1 : 2;
        int32_t mask = 0;
        float tick = 0.0f;
        float v[2] = {};
        bool up = false;
        bool ok = (args.size() == 3 + nv || args.size() == 4 + nv) && ParseSides(args[1], &mask) &&
                  ParseFloat(args[2], &tick) && tick >= 0.0f && tick <= (float)(kKeyTicks - 1) &&
                  tick == std::floor(tick);
        for (size_t i = 0; ok && i < nv; i++) {
            ok = ParseFloat(args[3 + i], &v[i]);
        }
        if (ok && args.size() == 4 + nv) {
            ok = args[3 + nv] == "down" || args[3 + nv] == "up";
            up = ok && args[3 + nv] == "up";
        }
        if (!ok) {
            Addl(lines, verb.c_str(), 1, "usage");
            return 1;
        }
        for (int32_t side = 0; side < 2; side++) {
            if (mask & (1 << side)) {
                if (isPaper) {
                    SetKeyCVar(side, up, (int32_t)tick, one, v[0]);
                } else {
                    SetKeyCVar(side, up, (int32_t)tick, 'X', v[0]);
                    SetKeyCVar(side, up, (int32_t)tick, 'Y', v[1]);
                }
            }
        }
        Addl(lines, verb.c_str(), 0, nullptr);
        return 0;
    }
    // keys: every entry in force, one line per side and direction, `t<tick>=dx,dy/paper`.
    if (verb == "keys" && args.size() == 1) {
        for (int32_t side = 0; side < 2; side++) {
            const RsLinkHandPose pose = ReadPose(side);
            for (int32_t up = 0; up < 2; up++) {
                std::string line = std::string("op=hand keys side=") + (side == 0 ? "l" : "r") +
                                   " dir=" + (up ? "up" : "down");
                for (int32_t t = 0; t < kKeyTicks; t++) {
                    float k[4];
                    KeyAt(side, up != 0, t, pose, k);
                    char cell[64];
                    std::snprintf(cell, sizeof(cell), " t%d=%g,%g/%g/%g", t, k[0], k[1], k[2], k[3]);
                    line += cell;
                }
                lines.push_back(line);
            }
        }
        return 0;
    }
    if (verb == "fingers" && args.size() == 2 && (args[1] == "back" || args[1] == "split")) {
        CVarSetInteger(CVAR_RS_HAND("Fingers"), args[1] == "back" ? 1 : 0);
        Addl(lines, "fingers", 0, nullptr);
        return 0;
    }
    // mode <l|r|both> anchor|keys
    if (verb == "mode" && args.size() == 3 && (args[2] == "anchor" || args[2] == "keys")) {
        int32_t mask = 0;
        if (!ParseSides(args[1], &mask)) {
            Addl(lines, "mode", 1, "usage");
            return 1;
        }
        for (int32_t side = 0; side < 2; side++) {
            if (mask & (1 << side)) {
                SetSideFloat(side, "Anchor", args[2] == "anchor" ? 1.0f : 0.0f);
            }
        }
        Addl(lines, "mode", 0, nullptr);
        return 0;
    }
    // anchor <l|r|both> flat|upright <along> <in> / curve <k> / rigid on|off / derive
    if (verb == "anchor" && args.size() >= 3) {
        int32_t mask = 0;
        bool ok = ParseSides(args[1], &mask);
        const std::string& what = args[2];
        float v[2] = {};
        if (ok && (what == "flat" || what == "upright")) {
            ok = args.size() == 5 && ParseFloat(args[3], &v[0]) && ParseFloat(args[4], &v[1]);
        } else if (ok && what == "curve") {
            ok = args.size() == 4 && ParseFloat(args[3], &v[0]) && v[0] > 0.0f;
        } else if (ok && what == "rigid") {
            ok = args.size() == 4 && (args[3] == "on" || args[3] == "off");
        } else if (ok && what == "derive") {
            ok = args.size() == 3;
        } else {
            ok = false;
        }
        if (!ok) {
            Addl(lines, "anchor", 1, "usage");
            return 1;
        }
        for (int32_t side = 0; side < 2; side++) {
            if (!(mask & (1 << side))) {
                continue;
            }
            const char* w = what == "flat" ? "Flat" : "Up";
            if (what == "flat" || what == "upright") {
                char fa[24];
                char fi[24];
                std::snprintf(fa, sizeof(fa), "Anchor%sAlong", w);
                std::snprintf(fi, sizeof(fi), "Anchor%sIn", w);
                SetSideFloat(side, fa, v[0]);
                SetSideFloat(side, fi, v[1]);
            } else if (what == "curve") {
                SetSideFloat(side, "AnchorCurve", v[0]);
            } else if (what == "rigid") {
                SetSideFloat(side, "AnchorRigid", args[3] == "on" ? 1.0f : 0.0f);
            } else {
                for (const char* f : { "AnchorFlatAlong", "AnchorFlatIn", "AnchorUpAlong", "AnchorUpIn" }) {
                    char name[96];
                    std::snprintf(name, sizeof(name), CVAR_RS_HAND("%s.%s"), kSideKey[side], f);
                    CVarClear(name);
                }
            }
        }
        Addl(lines, "anchor", 0, nullptr);
        return 0;
    }
    // knuckle <l|r|both> auto|front|back
    if (verb == "knuckle" && args.size() == 3 && (args[2] == "auto" || args[2] == "front" || args[2] == "back")) {
        int32_t mask = 0;
        if (!ParseSides(args[1], &mask)) {
            Addl(lines, "knuckle", 1, "usage");
            return 1;
        }
        const float v = args[2] == "front" ? (float)kKnuckleFront : args[2] == "back" ? (float)kKnuckleBack : 0.0f;
        for (int32_t side = 0; side < 2; side++) {
            if (mask & (1 << side)) {
                SetSideFloat(side, "Knuckle", v);
            }
        }
        Addl(lines, "knuckle", 0, nullptr);
        return 0;
    }
    if (verb == "scrollfilter" && args.size() == 2 && (args[1] == "point" || args[1] == "bilinear")) {
        CVarSetInteger(CVAR_RS_HAND("ScrollPoint"), args[1] == "point" ? 1 : 0);
        Addl(lines, "scrollfilter", 0, nullptr);
        return 0;
    }
    if (verb == "rollinset" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < 0.0f || v > 5.0f) {
            Addl(lines, "rollinset", 1, "not_an_inset");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("RollInset"), v);
        Addl(lines, "rollinset", 0, nullptr);
        return 0;
    }
    if (verb == "cutalpha" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < 0.0f || v > 255.0f) {
            Addl(lines, "cutalpha", 1, "not_an_alpha");
            return 1;
        }
        CVarSetInteger(CVAR_RS_HAND("CutAlpha"), (int32_t)v);
        Addl(lines, "cutalpha", 0, nullptr);
        return 0;
    }
    if (verb == "skip" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < -1.0f || v > 63.0f || v != std::floor(v)) {
            Addl(lines, "skip", 1, "not_a_triangle");
            return 1;
        }
        CVarSetInteger(CVAR_RS_HAND("Skip"), (int32_t)v);
        Addl(lines, "skip", 0, nullptr);
        return 0;
    }
    if (verb == "thumb" && args.size() == 2 && (args[1] == "front" || args[1] == "split")) {
        CVarSetInteger(CVAR_RS_HAND("Thumb"), args[1] == "front" ? 1 : 0);
        Addl(lines, "thumb", 0, nullptr);
        return 0;
    }
    if (verb == "roll" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < 0.0f) {
            Addl(lines, "roll", 1, "not_a_radius");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("Roll"), v);
        Addl(lines, "roll", 0, nullptr);
        return 0;
    }
    if (verb == "girth" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v) || v < 0.5f || v > 2.0f) {
            Addl(lines, "girth", 1, "not_a_girth");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("Girth"), v);
        Addl(lines, "girth", 0, nullptr);
        return 0;
    }
    if (verb == "overlap" && args.size() == 2) {
        float v = 0.0f;
        if (!ParseFloat(args[1], &v)) {
            Addl(lines, "overlap", 1, "not_a_number");
            return 1;
        }
        CVarSetFloat(CVAR_RS_HAND("Overlap"), v);
        Addl(lines, "overlap", 0, nullptr);
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
