#ifndef SOH_RS_MENU_LINK_HANDS_H
#define SOH_RS_MENU_LINK_HANDS_H
// THE SCROLL'S HANDS ARE LINK'S OWN (sturdy-bassoon#299): his hand, forearm and upper-arm limbs, cut out of his
// model and drawn holding the roll ends - real 3D display lists, lit, adult or child by the save, and dressed in
// whatever the save has him wearing on his arms. This supersedes #111's five-box placeholder hand (still drawn
// with `menu hand set boxes`, for an A/B and as the fallback if Link's skeleton cannot load).
//
// How a limb is cut out: Link is a FLEX skeleton, so his limbs are not self-contained. A forearm's display list
// welds its elbow to the upper arm by loading the upper arm's matrix out of segment 0x0D (slot n is the n-th
// limb that owns a display list), the upper arm welds to the torso the same way, and the gauntlet plates load
// the forearm's and the hand's slots themselves. So each hand gets a slot array of its own, filled as the skeleton
// draw would fill it, but from a chain that starts at the GRIP and works outward: hand, then forearm, then upper
// arm, then every other slot (the torso the shoulder welds to among them). Everything is under the side's matrix,
// so the hand still cannot come apart from its roll (#111 stage 5).
//
// What is data, so that a later piece of arm equipment is a row and not a rewrite: the hand sets (adult, child)
// and the arm pieces (LinkHands.cpp, kHandSets and kArmPieces). A piece is either a TINT - an environment colour
// the limbs take, the tunic's sleeve - or an OVERLAY - display lists drawn over the arm, the gauntlet plates and
// the Goron bracelet - with when it is worn and what colour it is, per side and per grip.
//
// The pose is live (`menu hand ...`, saved as CVars) because the owner works it out in game: the grip (open hand,
// the owner's pick, or fist), each side's rotation about the grip, the wrist, the elbow, the nudge, the scale per age, the extra
// turn the vertical (journal) pose gets, and the light.
//
// No z64.h here: MenuConsole.cpp includes this for its dump line and `menu hand`, and z64.h's gbi collides with the
// interpreter's there. The draw half, which needs the game's types, is LinkHandsDraw.h.
#include <stdint.h>
#include <string>
#include <vector>

// One side's tuning. Angles in degrees.
struct RsLinkHandPose {
    float rot[3];    // the hand about the grip point, applied z, then y, then x
    float offset[2]; // the grip point's nudge, in game units (x right, y down)
    float wrist[3];  // the forearm relative to the hand, applied z, then y, then x
    float elbow;     // the upper arm relative to the forearm, about the forearm's z
    float vturn;     // an extra turn about the screen's z, ramped in by the level gesture (the journal pose)
    float turnBend;  // a wrist bend in the screen's plane that rises and falls through the level turn (0 at both
                     // ends, full mid-turn)
};

struct RsLinkHandsStatus {
    const char* set;       // the hand set drawn: adult, child, boxes (the #111 placeholder), or none
    const char* grip;      // fist or open
    std::string variant;   // the arm pieces drawn, '+'-joined in table order (kokiri_tunic+silver_gauntlets), or none
    bool upper;            // whether the upper arm is drawn
    float scale;           // model units to game units, for the set drawn
    int32_t dlists[2];     // display lists drawn last frame, per side
    int32_t verts[2];      // vertices those lists load, per side (what the extents are measured over)
    float extent[2][4];    // each side's drawn extent, x0 y0 x1 y1 in game units, y down; the tick's pose
    int32_t slots;         // the skeleton's matrix slots (limbs with a display list), 0 if it did not load
    int32_t limbSlot[2][3]; // the matrix slot of each side's upper arm, forearm and hand
    int32_t loads;         // times the skeleton was fetched for a new age
    RsLinkHandPose pose[2];
    float light[3];        // the light's direction, in the menu's view space (x right, y up, z toward you)
    int32_t ambient;
    bool occlude;          // the scroll hides what of each hand is behind it (`menu hand occlude`)
    float paper;           // the scroll's depth relative to the grip, game units (+ toward you hides more)
    float reach;           // the upper arm's length, times its own (`menu hand reach`)
    bool trim;             // the sleeve trims applied (`menu hand trim`)
    int32_t trimmed[2];    // vertices the trims moved on each side's lists this frame
    int32_t keyTick;       // the level gesture's tick the key tables were read at last frame (`menu hand keys`)
    bool keyUp;            // ...and its direction: the up table, or the down one (also at rest)
    float key[2][4];       // each side's offset x, y, paper and roll depth in force last frame, from its key tables
    const char* thumb[2];  // each side's thumb: front (drawn whole in front), split (by the cut) or none (no table)
    int32_t tris[2][2];    // each side's hand triangles drawn [front, behind the scroll] (`menu hand cut`)
    int32_t straddle[2];   // of the behind ones, those partly in front of the scroll's plane (cut per pixel)
};

// False when `menu hand set boxes` has put the #111 placeholder back.
bool RsLinkHands_Enabled();

// The placeholder's turn: when the boxes are drawn, the status says so and the extents are the caller's.
void RsLinkHands_DrewBoxes(int32_t side, const float extent[4]);

RsLinkHandsStatus RsLinkHands_Status();

// `menu hand ...`. Prints the state line on no arguments; every line ends with it.
int32_t RsLinkHands_Console(const std::vector<std::string>& args, std::vector<std::string>& lines);

// The fields `menu hand` and `menu dump`'s `section=hands` share.
std::string RsLinkHands_Describe();

#endif // SOH_RS_MENU_LINK_HANDS_H
