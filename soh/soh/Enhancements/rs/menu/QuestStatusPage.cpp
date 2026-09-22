/*
 * QuestStatusPage.cpp - vanilla's Quest Status page, ported onto the scroll (sturdy-bassoon#111 stage
 * 8). Positions from KaleidoScope_InitVertices, movement and contents from KaleidoScope_DrawQuestStatus
 * (z_kaleido_collect.c). VanillaPages.h has what the three ports share.
 *
 * Draws through the menu's helpers only - no OPEN_DISPS, no Gfx macro here.
 *
 * NOT PORTED: song playback (selecting a song plays it and draws the ocarina staff - out of scope for
 * stage 8 and written up in sturdy-bassoon docs/reference/PAUSE_SONG_PLAYBACK.md), the medallions'
 * glow pulse and the heart pieces' colour cycle (both drawn in their resting colour), the cursor's own
 * look and the name plate. Unlike the other two pages, the cursor here stands on EVERY slot, owned or
 * not, exactly as kaleido's does - its movement is a fixed table, not a scan.
 *
 * Author: Spencer (with Claude)
 * Created: 2026-09-21
 */

#include "VanillaPagesInternal.h"
#include "RsMenu.h"

#include <cstdio>
#include <string>

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
extern PlayState* gPlayState;
extern const char* digitTextures[];
}

// --- geometry: KaleidoScope_InitVertices, z_kaleido_scope_PAL.c:2984-2998 and :3379-3446 ----------
// D_8082B138 / D_8082B198 / D_8082B1F8: x, top and size of kaleido's 47 quest quads. 0-23 are the
// QUEST_ items, 24 the heart pieces, 25-40 the song-playback staff (not ported) and 41-46 the Gold
// Skulltula count - 41-43 its shadow, 44-46 the digits over it.
static const s16 kQuadX[47] = {
    74,  74,  46,  18,  18,  46,   -108, -90,  -72, -54, -36, -18, -108, -90, -72, -54,
    -36, -18, 20,  46,  72,  -110, -86,  -110, -54, -98, -86, -74, -62,  -50, -38, -26,
    -14, -98, -86, -74, -62, -50,  -38,  -26,  -14, -88, -81, -72, -90,  -83, -74,
};
static const s16 kQuadTop[47] = {
    38, 6,   -12, 6,   38,  56,  -20, -20, -20, -20, -20, -20, 2,   2,   2,   2,   2,   2,  -46, -46, -46, 58, 58, 34,
    58, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, -52, 34, 34,  34,  36,  36, 36,
};
static const s16 kQuadSize[47] = {
    24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
    48, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
};

// One quad's box in page-local units, by kaleido's three rules: the medallions and the count digits
// exactly as tabled (the digits 8 wide, 16 tall, 6 down), every other quad inset 2 on the left and top
// and 4 smaller - with the songs 12 wide rather than 20.
struct QuadBox {
    int16_t left, top, w, h;
};
static QuadBox Quad(int32_t i) {
    QuadBox box;
    if (i < 6) {
        box = { kQuadX[i], kQuadTop[i], kQuadSize[i], kQuadSize[i] };
    } else if (i >= 41) {
        box = { kQuadX[i], (int16_t)(kQuadTop[i] - 6), 8, 16 };
    } else {
        const int16_t width = (i >= 6 && i <= 17) ? 16 : kQuadSize[i];
        box = { (int16_t)(kQuadX[i] + 2), (int16_t)(kQuadTop[i] - 2), (int16_t)(width - 4),
                (int16_t)(kQuadSize[i] - 4) };
    }
    return box;
}
// Drawn extent of quads 0-24 and 41-46: x -108..98, y 56..-68 (206 x 124). Centre (-5, -6) onto the
// rect's centre (160, 120); it fits at scale 1 with 5 units spare either side.
constexpr RsVanillaMap kMap = { 165, 114 };
constexpr int32_t kPoints = 25;

// --- movement: D_8082A1AC, z_kaleido_collect.c:57-65 ------------------------------------------------
// {up, down, left, right} per cursor point. -1 is no move, -2 the right page arrow and -3 the left.
// Kaleido's walk over it (:108-152) only ever takes the first entry, because
// KaleidoScope_UpdateQuestStatusPoint always returns 1 - so the table IS the graph.
static const s8 kLinks[kPoints][4] = {
    { 0x05, 0x01, 0x05, -2 },   { 0x00, 0x02, 0x02, -2 },   { -1, 0x13, 0x03, 0x01 },   { 0x04, 0x02, 0x11, 0x02 },
    { 0x05, 0x03, 0x18, 0x05 }, { -1, -1, 0x04, 0x00 },     { 0x0C, -1, -3, 0x07 },     { 0x0D, -1, 0x06, 0x08 },
    { 0x0E, -1, 0x07, 0x09 },   { 0x0F, -1, 0x08, 0x0A },   { 0x10, -1, 0x09, 0x0B },   { 0x11, -1, 0x0A, 0x12 },
    { 0x17, 0x06, -3, 0x0D },   { 0x17, 0x07, 0x0C, 0x0E }, { 0x17, 0x08, 0x0D, 0x0F }, { 0x18, 0x09, 0x0E, 0x10 },
    { 0x18, 0x0A, 0x0F, 0x11 }, { 0x18, 0x0B, 0x10, 0x03 }, { 0x02, -1, 0x0B, 0x13 },   { 0x02, -1, 0x12, 0x14 },
    { 0x02, -1, 0x13, -2 },     { -1, 0x17, -3, 0x16 },     { -1, 0x17, 0x15, 0x18 },   { 0x15, 0x0C, -3, 0x18 },
    { -1, 0x10, 0x16, 0x04 },
};
// Off the left arrow the cursor lands on the Stone of Agony, off the right on the Forest Medallion
// (:234-276).
constexpr int32_t kEnterFromLeft = QUEST_STONE_OF_AGONY; // 0x15
constexpr int32_t kEnterFromRight = QUEST_MEDALLION_FOREST;

static int32_t Target(s8 link) {
    return link == -2 ? RS_MENU_LINK_HAND_RIGHT : link == -3 ? RS_MENU_LINK_HAND_LEFT : link;
}

// Song note colours, D_8082A164/17C/194 (:48-56), in QUEST_SONG_ order.
static const u8 kSongR[12] = { 150, 255, 100, 255, 255, 255, 255, 255, 255, 255, 255, 255 };
static const u8 kSongG[12] = { 255, 80, 150, 160, 100, 240, 255, 255, 255, 255, 255, 255 };
static const u8 kSongB[12] = { 100, 40, 255, 0, 255, 100, 255, 255, 255, 255, 255, 255 };

// What a point shows, kaleido's cursorItem mapping (:160-186): medallions 0x66+, songs 0x5A+, stones and
// the rest 0x6C+; the heart-piece point names the container's item.
static int32_t PointItem(int32_t point) {
    if (point == QUEST_HEART_PIECE) {
        return ITEM_HEART_CONTAINER;
    }
    if (point < QUEST_SONG_MINUET) {
        return point + ITEM_MEDALLION_FOREST;
    }
    if (point < QUEST_KOKIRI_EMERALD) {
        return point - QUEST_SONG_MINUET + ITEM_SONG_MINUET;
    }
    return point - QUEST_KOKIRI_EMERALD + ITEM_KOKIRI_EMERALD;
}

static bool PointOwned(int32_t point) {
    if (point == QUEST_HEART_PIECE) {
        return (gSaveContext.inventory.questItems & 0xF0000000) != 0;
    }
    return CHECK_QUEST_ITEM(point) != 0;
}

static std::string NodeId(int32_t point) {
    char id[16];
    std::snprintf(id, sizeof(id), "status_%02d", point);
    return id;
}

static int32_t PointOfNode(const std::string& id) {
    if (id.size() != 9 || id.compare(0, 7, "status_") != 0) {
        return -1;
    }
    const int32_t point = (id[7] - '0') * 10 + (id[8] - '0');
    return point >= 0 && point < kPoints ? point : -1;
}

static int32_t sLastPoint = QUEST_MEDALLION_FOREST;
static int32_t sPageIndex = -1;

static void QuestStatusPageNodes(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    const int32_t cursorPoint = PointOfNode(RsMenu_CursorId());
    if (cursorPoint >= 0) {
        sLastPoint = cursorPoint;
    }
    int32_t nodeOf[kPoints];
    for (int32_t point = 0; point < kPoints; point++) {
        const QuadBox box = Quad(point);
        nodeOf[point] = RsMenu_AddCursorNode(NodeId(point).c_str(), kMap.X(box.left), kMap.Y(box.top), box.w, box.h);
    }
    auto toNode = [&](int32_t target) {
        return target >= 0 ? nodeOf[target] : target;
    };
    for (int32_t point = 0; point < kPoints; point++) {
        RsMenu_LinkCursorNode(nodeOf[point], toNode(Target(kLinks[point][2])), toNode(Target(kLinks[point][3])),
                              toNode(Target(kLinks[point][0])), toNode(Target(kLinks[point][1])));
    }
    RsMenu_LinkHands(nodeOf[kEnterFromLeft], nodeOf[kEnterFromRight]);
    RsMenu_SetCursorGrid(nodeOf[sLastPoint]);
}

// --- drawing: KaleidoScope_DrawQuestStatus, z_kaleido_collect.c:311-783 ----------------------------

static void DrawQuad(int32_t quad, const void* tex, RsMenuTexFormat format, int16_t texW, int16_t texH, u8 r, u8 g,
                     u8 b) {
    const QuadBox box = Quad(quad);
    RsMenu_DrawIcon(tex, format, texW, texH, kMap.X(box.left), kMap.Y(box.top), box.w, box.h, r, g, b, 255, false);
}

static void QuestStatusPageDraw(struct PlayState* play, int32_t pageIndex, void* userData) {
    (void)play;
    (void)pageIndex;
    (void)userData;
    for (int32_t i = 0; i < 6; i++) {
        if (CHECK_QUEST_ITEM(i)) {
            DrawQuad(i, gItemIcons[ITEM_MEDALLION_FOREST + i], RS_MENU_TEX_RGBA32, 24, 24, 255, 255, 255);
        }
    }
    for (int32_t i = 0; i < QUEST_KOKIRI_EMERALD - QUEST_SONG_MINUET; i++) {
        if (CHECK_QUEST_ITEM(i + QUEST_SONG_MINUET)) {
            DrawQuad(i + QUEST_SONG_MINUET, gItemIcons[ITEM_SONG_MINUET], RS_MENU_TEX_IA8, 16, 24, kSongR[i], kSongG[i],
                     kSongB[i]);
        }
    }
    for (int32_t i = 0; i < 3; i++) {
        if (CHECK_QUEST_ITEM(i + QUEST_KOKIRI_EMERALD)) {
            DrawQuad(i + QUEST_KOKIRI_EMERALD, gItemIcons[ITEM_KOKIRI_EMERALD + i], RS_MENU_TEX_RGBA32, 24, 24, 255,
                     255, 255);
        }
    }
    for (int32_t i = 0; i < 3; i++) {
        if (CHECK_QUEST_ITEM(i + QUEST_STONE_OF_AGONY)) {
            DrawQuad(i + QUEST_STONE_OF_AGONY, gItemIcons[ITEM_STONE_OF_AGONY + i], RS_MENU_TEX_RGBA32, 24, 24, 255,
                     255, 255);
        }
    }
    // The heart pieces: gItemIcons[0x79 + pieces], a 48 x 48 IA8 picture of that many quarters, in the
    // first colour of kaleido's red cycle (D_8082A070[0]).
    const u32 pieces = (gSaveContext.inventory.questItems & 0xF0000000) >> 0x1C;
    if (pieces != 0) {
        DrawQuad(QUEST_HEART_PIECE, gItemIcons[0x79 + pieces], RS_MENU_TEX_IA8, 48, 48, 255, 0, 0);
    }
    // The Gold Skulltula count (:740-783): a black shadow, then the digits, hundreds and tens only
    // once a leading digit is non-zero, red at 100.
    if (CHECK_QUEST_ITEM(QUEST_SKULL_TOKEN)) {
        int32_t digits[3] = { 0, 0, gSaveContext.inventory.gsTokens };
        while (digits[2] >= 100) {
            digits[0]++;
            digits[2] -= 100;
        }
        while (digits[2] >= 10) {
            digits[1]++;
            digits[2] -= 10;
        }
        for (int32_t pass = 0; pass < 2; pass++) {
            u8 r = 0, g = 0, b = 0;
            if (pass == 1) {
                if (gSaveContext.inventory.gsTokens == 100) {
                    r = 200;
                    g = 50;
                    b = 50;
                } else {
                    r = g = b = 255;
                }
            }
            bool shown = false;
            for (int32_t d = 0; d < 3; d++) {
                if (d >= 2 || digits[d] != 0 || shown) {
                    DrawQuad(41 + pass * 3 + d, digitTextures[digits[d]], RS_MENU_TEX_I8, 8, 16, r, g, b);
                    shown = true;
                }
            }
        }
    }
}

// #125: the HUD buttons vanilla shows on this page.
static void QuestStatusPageHud(int32_t pageIndex, uint8_t status[9], void* userData) {
    (void)pageIndex;
    (void)userData;
    RsVanilla_HudButtons(PAUSE_QUEST, status);
}

int32_t RsMenuQuestStatusPage_Register() {
    RsMenuPage page;
    page.id = "quest_status";
    page.title = "Quest Status";
    page.draw = QuestStatusPageDraw;
    page.nodes = QuestStatusPageNodes;
    page.ownsItemHighlight = false;
    page.stickModel = RS_MENU_STICK_KALEIDO_ORIGIN;
    page.hud = QuestStatusPageHud;
    sPageIndex = RsMenu_RegisterPageStruct(page);
    return sPageIndex;
}

void RsMenuQuestStatusPage_Describe(RsMenuPortInfo* info) {
    info->pageId = "quest_status";
    info->pageIndex = sPageIndex;
    info->scale = 1.0f;
    info->offsetX = kMap.offsetX;
    info->offsetY = kMap.offsetY;
    // The extent quoted in the geometry block, from its extreme quads: the left column (21), the top
    // row (5, 21, 22, 24), the right medallion column (0, 1) and the bottom stones (18-20).
    info->x0 = kMap.X(Quad(QUEST_STONE_OF_AGONY).left);
    info->y0 = kMap.Y(Quad(QUEST_MEDALLION_LIGHT).top);
    info->x1 = (int16_t)(kMap.X(Quad(QUEST_MEDALLION_FOREST).left) + Quad(QUEST_MEDALLION_FOREST).w);
    info->y1 = (int16_t)(kMap.Y(Quad(QUEST_KOKIRI_EMERALD).top) + Quad(QUEST_KOKIRI_EMERALD).h);
    info->slots.clear();
    for (int32_t point = 0; point < kPoints; point++) {
        const QuadBox box = Quad(point);
        const int32_t item = PointItem(point);
        RsMenuSlotInfo s;
        s.node = NodeId(point);
        s.slot = point;
        s.x = kMap.X(box.left);
        s.y = kMap.Y(box.top);
        s.w = box.w;
        s.h = box.h;
        s.item = item;
        s.itemName = RsVanilla_ItemToken(item);
        s.owned = PointOwned(point);
        s.isNode = true;
        s.grey = false;
        info->slots.push_back(s);
    }
}
