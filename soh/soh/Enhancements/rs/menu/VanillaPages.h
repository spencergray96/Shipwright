#ifndef SOH_RS_MENU_VANILLA_PAGES_H
#define SOH_RS_MENU_VANILLA_PAGES_H

#include <stdint.h>
#include <string>
#include <vector>

// The three vanilla pause pages, ported onto the scroll (sturdy-bassoon#111 stage 8): Select Item
// (`items`), Equipment (`equipment`) and Quest Status (`quest_status`). A STRAIGHT PORT, NOT A
// REDESIGN - for each page, vanilla's item positions, vanilla's cursor movement and the real contents
// of gSaveContext, placed on the plain parchment with none of kaleido's backdrop framing (no page
// quads, name plates, page arrows or info panel). Our own menus get built by modifying these.
//
// THE POSITIONS are kaleido's own tables (KaleidoScope_InitVertices, z_kaleido_scope_PAL.c), which
// are PAGE-LOCAL - vanilla draws each page onto a face of its rotating cube, x right and y UP from
// the face's centre. Each page maps them into RsMenu_PageRect() with ONE uniform scale and ONE offset,
// so every relative position stays exact: screen x = offsetX + x, screen y = offsetY - y. The scale is
// 1 on all three. Vertices are integers in the menu's 320x240 space, so any other scale would round
// relative positions by up to half a unit - the one thing a port must not do. The offset centres the
// page's drawn extent in the rect; Equipment's is 218 wide against the rect's 216, so it runs one unit
// into the hands' clearance strip on either side (still five units clear of the hands).
//
// THE MOVEMENT is kaleido's cursor code (z_kaleido_item.c, z_kaleido_equipment.c, z_kaleido_collect.c)
// run over the live inventory every update tick to build the cursor graph's edges - adjacency stays
// the graph's (RsMenu.h). Wherever vanilla would leave the page for a page arrow, the edge goes to the
// hand on that side; the hands' inward edges are vanilla's "step off the page arrow" scans.
//
// NODE IDS ARE VANILLA'S CURSOR POINTS: `items_NN`, `equip_NN`, `status_NN`, with NN pauseCtx's
// cursorPoint for that page (items: slot, 6 across; equipment: x + 4 * y, x 0 the upgrades column;
// quest status: the QUEST_ index, 24 the heart pieces). So a node id reads straight across to what
// kaleido's own cursor would say, which is what the differential test compares.
//
// No z64.h here, on purpose: MenuConsole.cpp includes this, and it cannot share a translation unit
// with z64.h (fast/interpreter.h, C4005 GIMMCMD).

// One slot of a ported page, as `menu dump` reports it. `item` is the ITEM_ id the slot shows (or
// would show - the quest page's unowned slots still name what goes there), `itemName` its enum token
// (`ITEM_HOOKSHOT`), `owned` whether it is in the save, `node` whether the cursor can stand on it
// right now, `grey` whether it draws greyed for the current age.
struct RsMenuSlotInfo {
    std::string node;
    int32_t slot;
    int16_t x, y, w, h; // the drawn box, in the menu's 320x240 space
    int32_t item;
    std::string itemName;
    bool owned;
    bool isNode;
    bool grey;
};

struct RsMenuPortInfo {
    std::string pageId;
    int32_t pageIndex; // 0-based ring index, -1 before registration
    float scale;
    int16_t offsetX, offsetY;
    int16_t x0, y0, x1, y1; // the page's drawn extent after mapping
    std::vector<RsMenuSlotInfo> slots;
};

// Registers the three pages at the end of the ring, in vanilla's order after the Quest Journal.
// Idempotent - ShipInit functions re-run on every config and preset load.
void RsMenuVanillaPages_Register();
// All three, whether or not one is the visible page: the slot lists are pure functions of the save.
std::vector<RsMenuPortInfo> RsMenuVanillaPages_Describe();

// Kaleido's own cursor, read live (never written) - the vanilla half of the differential test.
// `valid` is false with no PlayState. `special` is 0 on the page, 1 on the left page arrow, 2 on the
// right one. `sub` is pauseCtx->unk_1E4, kaleido's sub-state (0 is "taking input", 3 the item-equip
// animation, 7 the equipment page's equip lockout).
struct RsMenuKaleidoCursor {
    bool valid;
    int32_t state;
    int32_t debugState;
    int32_t page;
    int32_t point;
    int32_t x, y;
    int32_t special;
    int32_t item;
    int32_t slot;
    int32_t sub;
};
RsMenuKaleidoCursor RsMenu_KaleidoCursor();

// #125: the gameplay HUD as the interface sees it this tick, read live under either menu - what a
// screenshot of the HUD is asserted against. `mode`/`prevMode` are gSaveContext.hudVisibilityMode and
// prevHudVisibilityMode; `status` is buttonStatus[0..8] (B, C-left, C-down, C-right, A, D-up, D-down,
// D-left, D-right; 0 enabled, 255 disabled, anything else a temp-B item); `alpha` is the interface's
// per-element alpha, the thing that actually decides what draws: B, A, C-left, C-down, C-right, D-up,
// D-down, D-left, D-right, hearts, magic (and rupees and keys), minimap, START. `bLabel` is the B
// button's loaded action label (DO_ACTION_*) and `bLabelShown` whether B draws it instead of its item
// (vanilla pause loads DO_ACTION_SAVE and shows it). `valid` is false with no PlayState.
struct RsMenuHudState {
    bool valid;
    int32_t mode;
    int32_t prevMode;
    int32_t status[9];
    int32_t alpha[13];
    int32_t bLabel;
    int32_t bLabelShown;
};
RsMenuHudState RsMenu_HudState();

// #125: the flying equip icon (EquipFlight.cpp). `state` is kaleido's sEquipState (0 a magic arrow's
// fade-in, 1 its flight to the Bow, 2 its flash, 3 the flight to the button); `item` the ITEM_ id it
// draws (0xBF+ while a magic arrow's effect plays); `target` 0-2 C-left/down/right, 3-6 the D-pad
// slots; `x/y` its top-left, screen-centred (x - 160, 120 - y); `size` its edge in pixels; `flights`
// and `landings` count this session's. Landings lag flights by one while a flight is up.
struct RsMenuEquipFlightState {
    bool active;
    int32_t state;
    int32_t item;
    int32_t target;
    int32_t x, y;
    int32_t alpha;
    int32_t size;
    int32_t moveTimer;
    int32_t flights;
    int32_t landings;
    int32_t ticks;  // update ticks the current (or last) flight has taken
    int32_t holdAt; // the test-only hold, -1 off
};
RsMenuEquipFlightState RsVanilla_EquipFlightState();
// TEST-ONLY: stop a flight advancing once it has taken `tick` ticks (-1 lets it go), so a run can read
// and screenshot one pose of a half-second animation. `menu flight hold <n>|release`.
void RsVanilla_SetEquipFlightHold(int32_t tick);

// The save fields equipping writes, read live - what the half-2 comparison compares. `buttons` is
// equips.buttonItems (B, C-left, C-down, C-right, then the four D-pad slots), `slots` cButtonSlots,
// `equipment` equips.equipment (sword | shield << 4 | tunic << 8 | boots << 12), `swordless` the
// INFTABLE_SWORDLESS bit and `inf29` the whole of infTable[29], which vanilla's sword equip zeroes.
struct RsMenuEquipState {
    int32_t buttons[8];
    int32_t slots[7];
    uint16_t equipment;
    bool swordless;
    uint16_t inf29;
    int32_t swordHealth;
    int32_t bgsFlag;
    bool dpadEquips; // SoH's DpadEquips CVar, which decides whether the D-pad slots exist
    int32_t equipsDone; // equips the ported pages have performed this session (C, D-pad and A)
    // The LIVE Player - what Link in the world is wearing, which the save fields above do not say
    // (#123: the save changed and Link did not). Player_SetEquipmentData's four writes, and what its
    // Player_SetModelGroup derives from them. `player` is false with no Player; the rest are then -1.
    bool player;
    int32_t playerSword; // currentSwordItemId, an ITEM_ id
    int32_t playerShield, playerTunic, playerBoots; // PLAYER_SHIELD_/TUNIC_/BOOTS_
    int32_t modelGroup, modelAnimType, leftHandType, rightHandType, sheathType;
    int32_t linkAge; // gSaveContext.linkAge: 0 adult, 1 child
    int32_t playerSyncs; // times the scroll's close has run Player_SetEquipmentData this session
};
RsMenuEquipState RsMenu_EquipState();

// Vanilla's close hands the save's equipment to the live Player (z_kaleido_scope_PAL.c:4882,
// Player_SetEquipmentData); the scroll's close does the same, through this. Equipping from the pages
// writes only the save, as kaleido does, so without it Link keeps what he wore when the menu opened.
struct PlayState;
void RsMenuVanillaPages_SyncPlayer(struct PlayState* play);

// TEST-ONLY: the fixture the movement tests need - a SPARSE inventory, because the debug save owns
// nearly everything and a full grid never exercises kaleido's skip-empty-slots rules. Every kind but
// `age` writes gSaveContext directly and nothing else (no save, no HUD refresh); `age` reloads the
// scene. Run it on a scratch save:
//   `item <slot> <ITEM_ id | 255>`, `equip <bit 0-15> <0|1>` (the owned-equipment bits),
//   `upgrade <UPG_ type 0-7> <value>`, `quest <QUEST_ bit 0-23> <0|1>`,
//   `sword <bgsFlag 0|1> <swordHealth 0-8>` (the Biggoron/broken-knife cases; the knife's owned bit is
//   `equip 3 1`), `age <0|1> 0` (0 adult, 1 child: SoH's own SwitchAge, a scene reload, so wait for
//   the next `ready` - and a no-op when Link is that age already).
// False on an unknown kind or an out-of-range argument.
bool RsMenu_TestSetInventory(const std::string& kind, int32_t a, int32_t b);

#endif // SOH_RS_MENU_VANILLA_PAGES_H
