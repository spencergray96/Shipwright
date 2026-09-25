#ifndef SOH_RS_MENU_NAME_PANEL_H
#define SOH_RS_MENU_NAME_PANEL_H

// #132: vanilla's info panel, ported under the scroll (NamePanel.cpp) - KaleidoScope_UpdateNamePanel's timer
// and KaleidoScope_DrawInfoPanel's stone, name, button prompts, "To ..." labels and L/R page-arrow icons.
// RsMenu.cpp decides where it sits and when it shows; the page says what its node names (RsMenu.h,
// RsMenuNameInfo). Nothing but RsMenu.cpp and the console calls these.
//
// No z64.h here, on purpose: MenuConsole.cpp includes this, and it cannot share a translation unit with z64.h.

#include <stdint.h>
#include <string>

#include "RsMenu.h"

// What RsMenu.cpp hands the panel, tick and frame alike. Positions are in the scroll's own game space, BEFORE
// the drop: the panel draws under the scroll's base matrix, so it rides the arrival slide as vanilla's slides.
struct RsNamePanelFrame {
    const RsMenuPage* page;       // the page on show
    int32_t pageIndex;
    const RsMenuCursorNode* node; // the cursor's node; null for none
    const RsMenuPage* handTo;     // on a hand: the page that hand turns to
    bool settled;                 // kaleido's state 6: open and arrived, not arriving or leaving
    bool opening;                 // arriving - when kaleido's unk_1E4 is 1, which keeps the L/R icons small
    int16_t stoneX, stoneY;       // the 144 x 24 stone's top-left
    int16_t lrY;                  // the L/R icons' vertical centre (each palm's)
    int16_t lrInner[2];           // the left icon's right edge and the right icon's left edge, at full size
    float screenDy;               // RsMenu_ScreenDy, so the dump's rectangles are where they are drawn
};

// Once per update tick while the menu is settled: asks the page what its node names and runs vanilla's timer.
void RsNamePanel_Tick(const RsNamePanelFrame& f);
// Draws the panel into the menu's list; the caller has already decided it shows at all (not through a roll, a
// level change or level 1). The stone, name and prompts only on a page with a `name` callback; the L/R icons on
// every page, since every page's hands are page arrows.
void RsNamePanel_Draw(const RsNamePanelFrame& f);
// The frame drew no panel.
void RsNamePanel_Hidden();
// The menu opened or closed: kaleido's own reset at open (namedItem none, timer 0), and the test-only custom
// name below comes off.
void RsNamePanel_Reset();

// What the last drawn frame did, for `menu dump section=name_panel`. Rectangles are on screen, in game units,
// x0,y0,x1,y1.
struct RsNamePanelState {
    bool shown;          // the frame drew any of it
    bool stone;
    const char* text;    // what is on the stone: name, prompt, to (a hand's label) or none
    std::string tex;     // the name's texture (a resource path), or the hand label's; "-" for anything else
    std::string glyphs;  // what was drawn as text rather than a texture ("to select quest"); "" for nothing
    int32_t item;        // namedItem as tracked, -1 none
    std::string itemName;  // its ITEM_ token, "-" for none
    bool grey;
    int32_t timer;       // nameDisplayTimer
    bool alternates;
    int32_t subState;
    int32_t prompt;      // RsMenuNamePrompt
    bool custom;         // the name came from VB_DRAW_CUSTOM_ITEM_NAME
    int32_t lookups;     // name lookups this session (one per change of named item)
    int32_t customs;     // of those, how many the hook answered
    float stoneRect[4];
    bool lr;
    int32_t lrBig;       // 0 neither, 1 the left icon, 2 the right: the cursor rests on that hand
    float lRect[4];
    float rRect[4];
};
RsNamePanelState RsNamePanel_State();

// TEST-ONLY (`menu namepanel custom <item>|off`): registers a VB_DRAW_CUSTOM_ITEM_NAME handler that names
// `item` with the Ocarina of Time's name texture, written into pauseCtx->nameSegment exactly as rando's Roc's
// Feather does it - so a run can challenge the hook's path without a rando seed. -1 removes it; so does the
// menu closing. Returns the item now named, -1 for none.
int32_t RsNamePanel_SetTestCustom(int32_t item);

#endif // SOH_RS_MENU_NAME_PANEL_H
