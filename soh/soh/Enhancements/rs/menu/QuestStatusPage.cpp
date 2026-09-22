/*
 * QuestStatusPage.cpp - vanilla's Quest Status page, ported onto the scroll (sturdy-bassoon#111 stage
 * 8). Positions from KaleidoScope_InitVertices, movement and contents from KaleidoScope_DrawQuestStatus
 * (z_kaleido_collect.c). VanillaPages.h has what the three ports share.
 *
 * Draws through the menu's helpers only - no OPEN_DISPS, no Gfx macro here.
 *
 * Song playback is ported since #127 (the song section below; sturdy-bassoon
 * docs/reference/PAUSE_SONG_PLAYBACK.md has the design). NOT PORTED: the medallions'
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

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "variables.h"
#include "macros.h"
#include "textures/parameter_static/parameter_static.h"
extern PlayState* gPlayState;
extern const char* digitTextures[];
}

// --- geometry: KaleidoScope_InitVertices, z_kaleido_scope_PAL.c:2984-2998 and :3379-3446 ----------
// D_8082B138 / D_8082B198 / D_8082B1F8: x, top and size of kaleido's 47 quest quads. 0-23 are the
// QUEST_ items, 24 the heart pieces, 25-40 the song-playback staff (#127, the song section) and 41-46 the Gold
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

static void DrawStaff(); // #127, the song section below

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
    DrawStaff();
}

// --- song playback (#127): KaleidoScope_DrawQuestStatus and KaleidoScope_Update's song states ------
//
// Rest the cursor on an owned song and its notes show (8, the preview); A plays it (9, then 2, the game
// playing the demo); then the player plays it back (4 arms, 5 listens, 6 holds the result, then back to
// 4 after a miss or to 0 after a hit). The numbers are kaleido's own sub-states (pauseCtx->unk_1E4); the
// state lives here, never in pauseCtx. All of the audio is kaleido's own AudioOcarina_* calls, and the
// two staves are the ocarina's globals. One tick does what kaleido's update does (z_kaleido_scope_PAL.c
// :4294-4359), then what its draw does (z_kaleido_collect.c:195-304, :535-737), in that order.
//
// Differences, by decision (Spencer, #127):
//   - B while a song runs (9, 2, 4, 5, 6) leaves the song rather than opening the save prompt; the
//     preview (8) is only the cursor resting on a song, so B there closes the scroll as anywhere else.
//   - The BGM mutes only while a song runs - kaleido mutes it for the whole pause - through the audio
//     thread's own mute (the 0xF1000000 / 0xF2000000 pair func_800F64E0 queues, without its window
//     sounds). A mute stops nothing, so the zone music director's track is still there after.
// Kaleido's input rules come with it: nothing but the song moves in 9, 2, 4 and 6; in 5 the stick and
// START still act (the stick leaves the song), L/R do not.

enum SongState : int32_t {
    SONG_IDLE = 0,
    SONG_DEMO = 2,
    SONG_ARM = 4,
    SONG_PLAY = 5,
    SONG_RESULT = 6,
    SONG_PREVIEW = 8,
    SONG_LEAD_IN = 9,
};

// The staff's note heights by button (A, C-down, C-right, C-left, C-up) - VREG(21..25) as kaleido sets
// them every time a song starts (:213-217) - and the note textures (D_8082A130, :38-41).
static const int16_t kNoteTop[5] = { -62, -56, -49, -46, -41 };
static const void* const kNoteTex[5] = { gOcarinaBtnIconATex, gOcarinaBtnIconCDownTex, gOcarinaBtnIconCRightTex,
                                         gOcarinaBtnIconCLeftTex, gOcarinaBtnIconCUpTex };
constexpr int32_t kNoteQuadTop = 25;    // QUEST_QUAD_SONG_NOTE_A1: the demo's row, 25-32
constexpr int32_t kNoteQuadPlayed = 33; // the player's echo, 33-40

struct Song {
    int32_t state = SONG_IDLE;
    int32_t point = -1;   // the song's cursor point
    int32_t songIdx = 0;  // pauseCtx->ocarinaSongIdx: the OCARINA song, through gOcarinaSongItemMap
    u8 notes[10];         // D_8082A124: each note shown so far, 0xFF past the last - 10 as kaleido's is,
                          // so the terminator written after an 8th note (`notes[pos] = 0xFF`) has room
    s16 alpha[10];        // D_8082A150: each note's fade-in, sized with `notes`
    s16 count = 0;        // D_8082A11C
    s16 timer = 0;        // D_8082A120 (the lead-in) and D_8082B25C (the result)
    int32_t after = SONG_IDLE; // D_8082B258: where the result goes
    bool muted = false;
};
static Song sSong;
static int32_t sSongPreviews = 0;
static int32_t sSongDemos = 0;
static int32_t sSongHits = 0;
static int32_t sSongMisses = 0;

static void ClearNotes() {
    for (int32_t i = 0; i < 10; i++) {
        sSong.notes[i] = 0xFF;
        sSong.alpha[i] = 0;
    }
    sSong.count = 0;
}

static void SetMuted(bool mute) {
    if (mute == sSong.muted) {
        return;
    }
    sSong.muted = mute;
    // 0xF2 lifts every sequence player's mute. If vanilla pause has come up (the scroll stands down for
    // it), that mute is kaleido's now, and kaleido lifts it on its own close.
    if (!mute && gPlayState != nullptr && gPlayState->pauseCtx.state != 0) {
        return;
    }
    Audio_QueueCmdS32(mute ? 0xF1000000 : 0xF2000000, 0);
}

// Back to no song: the instrument off and the BGM back, whatever state it was in.
static void LeaveSong() {
    if (sSong.state != SONG_IDLE) {
        AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
    }
    sSong.state = SONG_IDLE;
    SetMuted(false);
}

static bool IsSongPoint(int32_t point) {
    return point >= QUEST_SONG_MINUET && point < QUEST_KOKIRI_EMERALD && CHECK_QUEST_ITEM(point);
}

static void PlaySfx(u16 sfxId) {
    Audio_PlaySoundGeneral(sfxId, &gSfxDefaultPos, 4, &gSfxDefaultFreqAndVolScale, &gSfxDefaultFreqAndVolScale,
                           &gSfxDefaultReverb);
}

// The player's turn: armed, listening, or holding the result (kaleido's 4, 5 and 6).
static bool IsPlayAlong(int32_t state) {
    return state == SONG_ARM || state == SONG_PLAY || state == SONG_RESULT;
}

// A song is running: from the A that starts it to the result that ends it (kaleido's 9, 2, 4, 5, 6).
static bool IsSongRunning(int32_t state) {
    return state == SONG_LEAD_IN || state == SONG_DEMO || IsPlayAlong(state);
}

// Arms the ocarina on the song: the note buffers cleared, the instrument on and AudioOcarina_Start in
// playback mode, the playback staff reset - the preview's start (:202-212) and the player's turn
// (:722-733) both do it.
static void ArmOcarina(u8 staffState) {
    ClearNotes();
    AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
    AudioOcarina_Start((1 << sSong.songIdx) + 0x8000);
    OcarinaStaff* staff = AudioOcarina_GetPlaybackStaff();
    staff->pos = 0;
    staff->state = staffState;
}

static void QuestStatusPageTick(int32_t pageIndex, uint16_t press, void* userData) {
    (void)pageIndex;
    (void)userData;
    Song& s = sSong;
    const int32_t point = PointOfNode(RsMenu_CursorId());

    // B while a song runs leaves it (Spencer, #127), where kaleido would open the save prompt from 5 and
    // take no B at all in 9, 2, 4 and 6. The menu is holding B then, so it does not also close.
    if (IsSongRunning(s.state) && CHECK_BTN_ALL(press, BTN_B)) {
        LeaveSong();
        PlaySfx(NA_SE_SY_DECIDE);
        return;
    }

    // --- kaleido's update (z_kaleido_scope_PAL.c:4294-4359) ---
    if (s.state == SONG_DEMO) {
        if (AudioOcarina_GetPlaybackStaff()->state == 0) {
            s.state = SONG_ARM;
            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
        }
    } else if (s.state == SONG_PLAY) {
        const OcarinaStaff* staff = AudioOcarina_GetPlayingStaff();
        if (staff->state == s.songIdx) {
            PlaySfx(NA_SE_SY_TRE_BOX_APPEAR);
            s.after = SONG_IDLE;
            s.timer = 30;
            s.state = SONG_RESULT;
            sSongHits++;
        } else if (staff->state == 0xFF) {
            PlaySfx(NA_SE_SY_OCARINA_ERROR);
            s.after = SONG_ARM;
            s.timer = 20;
            s.state = SONG_RESULT;
            sSongMisses++;
        }
    } else if (s.state == SONG_RESULT) {
        if (--s.timer == 0) {
            if (s.after == SONG_IDLE) {
                LeaveSong();
            } else {
                s.state = s.after;
            }
        }
    }

    // --- kaleido's draw (z_kaleido_collect.c) ---
    // A cursor move ends a preview (:154-158); any stick step ends a play-along, even one the cursor had
    // nowhere to go for (:222-226, stickRel after kaleido's filter).
    if ((s.state == SONG_PREVIEW && point != s.point) ||
        (s.state == SONG_PLAY && (point != s.point || RsMenu_StickStepped()))) {
        LeaveSong();
    }
    if (s.state == SONG_IDLE) {
        // Resting on an owned song starts its preview (:195-221): the playback staff armed and the
        // instrument switched on and straight back off, as kaleido does, so the next A finds it fresh.
        if (IsSongPoint(point)) {
            s.point = point;
            s.songIdx = gOcarinaSongItemMap[point - QUEST_SONG_MINUET];
            s.timer = 10;
            ArmOcarina(0xFF);
            s.state = SONG_PREVIEW;
            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_OFF);
            sSongPreviews++;
        }
    } else if (s.state == SONG_PREVIEW) {
        if (CHECK_BTN_ALL(press, BTN_A)) {
            s.state = SONG_LEAD_IN;
            s.timer = 10;
            SetMuted(true);
        }
    } else if (s.state == SONG_LEAD_IN) {
        if (--s.timer == 0) { // :279-304
            ClearNotes();
            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
            AudioOcarina_SetInstrument(OCARINA_INSTRUMENT_DEFAULT);
            s.songIdx = gOcarinaSongItemMap[s.point - QUEST_SONG_MINUET];
            AudioOcarina_SetPlaybackSong(s.songIdx + 1, 1);
            s.state = SONG_DEMO;
            AudioOcarina_GetPlaybackStaff()->pos = 0;
            sSongDemos++;
        }
    }

    // The notes appear as the staves' `pos` advances, each fading in by VREG(50) a tick.
    if (s.state == SONG_DEMO) { // :539-557
        const OcarinaStaff* staff = AudioOcarina_GetPlaybackStaff();
        if (staff->pos != 0 && s.count + 1 == staff->pos) {
            s.count++;
            s.notes[staff->pos - 1] = staff->buttonIndex;
        }
    } else if (IsPlayAlong(s.state)) { // :653-665
        const OcarinaStaff* staff = AudioOcarina_GetPlayingStaff();
        if (staff->pos != 0 && s.count == staff->pos - 1 && staff->buttonIndex >= OCARINA_BTN_A &&
            staff->buttonIndex <= OCARINA_BTN_C_UP) {
            s.notes[staff->pos - 1] = staff->buttonIndex;
            s.notes[staff->pos] = 0xFF;
            s.count++;
        }
    }
    if (s.state == SONG_DEMO || IsPlayAlong(s.state)) {
        for (int32_t i = 0; i < 8; i++) {
            if (s.notes[i] != 0xFF && s.alpha[i] != 255) {
                s.alpha[i] = (s16)(s.alpha[i] + VREG(50) >= 255 ? 255 : s.alpha[i] + VREG(50));
            }
        }
    }
    if (s.state == SONG_ARM) { // :722-735, the player's turn armed (kaleido resets the PLAYBACK staff)
        ArmOcarina(0xFE);
        s.state = SONG_PLAY;
    }
}

static uint32_t QuestStatusPageHold(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    switch (sSong.state) {
        case SONG_LEAD_IN:
        case SONG_DEMO:
        case SONG_ARM:
        case SONG_RESULT:
            return RS_MENU_HOLD_ALL; // kaleido takes no input in 9, 2, 4 or 6; B was the tick's
        case SONG_PLAY:
            // The stick and START still act; A and B are the song's (A plays a note on the ocarina, which
            // reads the pad itself).
            return RS_MENU_HOLD_A | RS_MENU_HOLD_B | RS_MENU_HOLD_ROLL;
        default:
            return RS_MENU_HOLD_NONE;
    }
}

static void QuestStatusPageReset(int32_t pageIndex, void* userData) {
    (void)pageIndex;
    (void)userData;
    LeaveSong();
}

// A note's tint: the HUD's button colour with SoH's cosmetics, grey when the note shuffle has not given
// it yet (z_kaleido_collect.c:474-518).
static void NoteColour(int32_t button, u8* r, u8* g, u8* b) {
    Color_RGB8 a = { 80, 150, 255 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.AButton.Changed"), 0)) {
        a = CVarGetColor24(CVAR_COSMETIC("HUD.AButton.Value"), a);
    } else if (CVarGetInteger(CVAR_COSMETIC("DefaultColorScheme"), COLORSCHEME_N64) == COLORSCHEME_GAMECUBE) {
        a = { 80, 255, 150 };
    }
    Color_RGB8 c = { 255, 255, 50 };
    if (CVarGetInteger(CVAR_COSMETIC("HUD.CButtons.Changed"), 0)) {
        c = CVarGetColor24(CVAR_COSMETIC("HUD.CButtons.Value"), c);
    }
    struct Button {
        const char* changed;
        const char* value;
        GIVanillaBehavior have;
    };
    static const Button kButtons[5] = {
        { nullptr, nullptr, VB_HAVE_OCARINA_NOTE_D4 },
        { CVAR_COSMETIC("HUD.CDownButton.Changed"), CVAR_COSMETIC("HUD.CDownButton.Value"), VB_HAVE_OCARINA_NOTE_F4 },
        { CVAR_COSMETIC("HUD.CRightButton.Changed"), CVAR_COSMETIC("HUD.CRightButton.Value"), VB_HAVE_OCARINA_NOTE_A4 },
        { CVAR_COSMETIC("HUD.CLeftButton.Changed"), CVAR_COSMETIC("HUD.CLeftButton.Value"), VB_HAVE_OCARINA_NOTE_B4 },
        { CVAR_COSMETIC("HUD.CUpButton.Changed"), CVAR_COSMETIC("HUD.CUpButton.Value"), VB_HAVE_OCARINA_NOTE_D5 },
    };
    Color_RGB8 colour = a;
    if (button > 0 && button < 5) {
        colour = c;
        if (CVarGetInteger(kButtons[button].changed, 0)) {
            colour = CVarGetColor24(kButtons[button].value, colour);
        }
    }
    if (button >= 0 && button < 5 && !GameInteractor_Should(kButtons[button].have, true)) {
        colour = { 191, 191, 191 };
    }
    *r = colour.r;
    *g = colour.g;
    *b = colour.b;
}

static void DrawNote(int32_t quad, int32_t button, u8 r, u8 g, u8 b, u8 alpha) {
    if (button < 0 || button > 4) {
        return;
    }
    const QuadBox box = Quad(quad);
    RsMenu_DrawIcon(kNoteTex[button], RS_MENU_TEX_IA8, 16, 16, kMap.X(box.left), kMap.Y(kNoteTop[button]), box.w, 12,
                    r, g, b, alpha, false);
}

// The staff (z_kaleido_collect.c:535-737): the preview's notes at alpha 200; the demo's as they play;
// then, while the player plays, the song in grey on the top row and their notes echoed below. The echo
// is tinted by the SONG's button at that position, not the one played - kaleido's own quirk (:693-708),
// kept - except for A.
static void DrawStaff() {
    const Song& s = sSong;
    u8 r, g, b;
    if (s.state == SONG_DEMO) {
        for (int32_t i = 0; i < 8 && s.notes[i] != 0xFF; i++) {
            NoteColour(s.notes[i], &r, &g, &b);
            DrawNote(kNoteQuadTop + i, s.notes[i], r, g, b, (u8)s.alpha[i]);
        }
        return;
    }
    if (s.state != SONG_PREVIEW && !IsPlayAlong(s.state)) {
        return;
    }
    const OcarinaSongButtons& song = gOcarinaSongButtons[s.songIdx];
    for (int32_t i = 0; i < song.numButtons; i++) {
        const int32_t button = song.buttonsIndex[i];
        if (s.state == SONG_PREVIEW) {
            NoteColour(button, &r, &g, &b);
            DrawNote(kNoteQuadTop + i, button, r, g, b, 200);
        } else {
            DrawNote(kNoteQuadTop + i, button, 150, 150, 150, 150);
        }
    }
    if (s.state == SONG_PREVIEW) {
        return;
    }
    for (int32_t i = 0; i < 8; i++) {
        if (s.notes[i] == 0xFF) {
            continue;
        }
        NoteColour(s.notes[i] == OCARINA_BTN_A ? OCARINA_BTN_A : song.buttonsIndex[i], &r, &g, &b);
        DrawNote(kNoteQuadPlayed + i, s.notes[i], r, g, b, (u8)s.alpha[i]);
    }
}

RsMenuSongState RsMenu_SongState() {
    RsMenuSongState st = {};
    st.state = sSong.state;
    st.point = sSong.point;
    st.songIdx = sSong.songIdx;
    st.count = sSong.count;
    for (int32_t i = 0; i < 8; i++) {
        st.notes[i] = sSong.notes[i];
    }
    st.muted = sSong.muted;
    const OcarinaStaff* playback = AudioOcarina_GetPlaybackStaff();
    const OcarinaStaff* playing = AudioOcarina_GetPlayingStaff();
    st.playbackPos = playback->pos;
    st.playbackState = playback->state;
    st.playbackButton = playback->buttonIndex;
    st.playingPos = playing->pos;
    st.playingState = playing->state;
    st.playingButton = playing->buttonIndex;
    st.bgmMutedByAudio = gAudioContext.seqPlayers[SEQ_PLAYER_BGM_MAIN].muted;
    st.previews = sSongPreviews;
    st.demos = sSongDemos;
    st.hits = sSongHits;
    st.misses = sSongMisses;
    return st;
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
    page.tick = QuestStatusPageTick;
    page.hold = QuestStatusPageHold;
    page.reset = QuestStatusPageReset;
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
