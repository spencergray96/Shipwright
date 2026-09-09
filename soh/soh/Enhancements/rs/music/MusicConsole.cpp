/*
 * The console surface for the zone director (sturdy-bassoon#90 P0). See MusicConsole.h for the
 * subcommand list and why one renderer feeds two sinks.
 */

#include "MusicConsole.h"
#include "MusicZones.h"
#include "ZoneDirector.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/debug/Console.h>

#include "soh/Enhancements/worldstate/WorldFlags.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "global.h"
#include "macros.h"
#include "variables.h"
extern PlayState* gPlayState;
}

#define CVAR_RS_MUSIC_ON CVAR_ENHANCEMENT("RsMusicZones")
#define CVAR_RS_MUSIC_DWELL CVAR_ENHANCEMENT("RsMusicDwellSec")
#define CVAR_RS_MUSIC_FADE_OUT CVAR_ENHANCEMENT("RsMusicFadeOutSec")
#define CVAR_RS_MUSIC_FADE_IN CVAR_ENHANCEMENT("RsMusicFadeInSec")

namespace {

void Addf(std::vector<std::string>& lines, const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    lines.emplace_back(buf);
}

bool ParseSeconds(const std::string& s, float* out) {
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (end == s.c_str() || *end != '\0' || !(v >= 0.0f) || v > 20.0f) {
        return false;
    }
    *out = v;
    return true;
}

// Report the value the ENGINE will see, not the one that was typed: a fade is an 8-bit field in
// units of 1/30 s, so 12 seconds and 8.5 seconds are the same fade and a run log that echoed "12"
// would be lying about what it measured.
int32_t FadeUnits(float seconds) {
    const int32_t units = (int32_t)(seconds * 30.0f + 0.5f);
    return units > 255 ? 255 : units;
}

int32_t SetSeconds(const std::vector<std::string>& args, std::vector<std::string>& lines, const char* cvar,
                   const char* label, bool isFade) {
    if (args.size() < 2) {
        Addf(lines, "op=%s result=error error=missing_value", label);
        return 1;
    }
    float seconds = 0.0f;
    if (!ParseSeconds(args[1], &seconds)) {
        Addf(lines, "op=%s result=error error=bad_value (expects 0..20 seconds)", label);
        return 1;
    }
    CVarSetFloat(cvar, seconds);
    CVarSave();
    if (isFade) {
        Addf(lines, "op=%s seconds=%.2f units=%d result=ok", label, seconds, FadeUnits(seconds));
    } else {
        Addf(lines, "op=%s seconds=%.2f ticks=%d result=ok", label, seconds, (int32_t)(seconds * 20.0f + 0.5f));
    }
    return 0;
}

} // namespace

int32_t MusicConsole_Run(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? std::string("status") : args[0];

    if (sub == "status") {
        // Four grouped lines rather than one, because the ImGui console does not wrap and P0's
        // single line had to be read by dragging the window out to full width. RsMusic_Describe()
        // still exists and is still one line - that is the right shape for a marker or the echo
        // after `on`, and the wrong shape for a human.
        char line[256];
        for (int32_t i = 0; RsMusic_DescribeLine(i, line, sizeof(line)); i++) {
            lines.emplace_back(line);
        }
        return 0;
    }

    if (sub == "where") {
        int16_t sceneId = -1;
        int32_t rsX = 0;
        int32_t rsY = 0;
        int32_t zone = -1;
        const int32_t inZone = RsMusic_Probe(&sceneId, &rsX, &rsY, &zone);
        if (!inZone) {
            // Not a failure: "this scene is not opted in" is the answer, and it is the first thing
            // to check when a run reports no music at all.
            Addf(lines, "where scene=0x%X opted_in=0 reason=scene_not_in_table", sceneId);
            return 0;
        }
        const Player* player = GET_PLAYER(gPlayState);
        const RsMusicZone* z = RsMusicZones_At(zone);
        const RsMusicScene* s = RsMusicZones_Scene(sceneId);
        // surface_anchor=0 means rs= is arithmetic on a placeholder origin rather than a place -
        // an interior or underground scene, where rect matching is skipped and only a zone bound to
        // this scene (or the fallback) can win. Printed next to rs= so nobody reads those two
        // numbers as a location.
        const int32_t anchored = (s != NULL && (s->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR)) ? 0 : 1;
        char tiles[32];
        RsMusic_FormatTiles(tiles, sizeof(tiles), rsX, rsY);
        Addf(lines, "where scene=0x%X opted_in=1 surface_anchor=%d pos=%.1f,%.1f,%.1f rs=%s zone=%s", sceneId,
             anchored, player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, tiles,
             z != nullptr ? z->name : "none");
        return 0;
    }

    if (sub == "zones") {
        const int32_t count = RsMusicZones_Count();
        Addf(lines, "zones count=%d", count);
        for (int32_t i = 0; i < count; i++) {
            const RsMusicZone* z = RsMusicZones_At(i);
            // scene= is the optional binding: `any` for a surface zone placed by its rects, a scene
            // id for one that only exists inside that scene (an underground area). A bound zone
            // with rects=0 matches that whole scene.
            char scene[16];
            if (z->sceneId == RS_ZONE_SCENE_ANY) {
                std::snprintf(scene, sizeof(scene), "any");
            } else {
                std::snprintf(scene, sizeof(scene), "0x%X", (uint32_t)(uint16_t)z->sceneId);
            }
            Addf(lines, "zone[%d]=%s priority=%d fallback=%d scene=%s rects=%d tracks=%d first_visit_flag=%d", i,
                 z->name, (int32_t)z->priority, (z->flags & RS_ZONE_FLAG_FALLBACK) ? 1 : 0, scene,
                 (int32_t)z->rectCount, (int32_t)z->trackCount, (int32_t)z->firstVisitFlag);
            if (z->firstVisitTrack != NULL) {
                Addf(lines, "first_visit[%d]=0x%X flag=%d set=%d", i, (uint32_t)z->firstVisitTrack->seqId,
                     (int32_t)z->firstVisitFlag, Flags_GetWorldFlag(z->firstVisitFlag) ? 1 : 0);
            }
            for (uint8_t r = 0; r < z->rectCount; r++) {
                const RsZoneRect& rect = z->rects[r];
                Addf(lines, "rect[%d.%d]=%d,%d..%d,%d y=%d..%d", i, (int32_t)r, (int32_t)rect.x0, (int32_t)rect.y0,
                     (int32_t)rect.x1, (int32_t)rect.y1, (int32_t)rect.yMinUnits, (int32_t)rect.yMaxUnits);
            }
            for (uint8_t t = 0; t < z->trackCount; t++) {
                Addf(lines, "track[%d.%d]=0x%X cond=0x%X len=%d", i, (int32_t)t, (uint32_t)z->tracks[t].seqId,
                     (uint32_t)z->tracks[t].conditions, (int32_t)z->tracks[t].lengthSec);
            }
        }
        return 0;
    }

    if (sub == "scenes") {
        const int32_t count = RsMusicZones_SceneCount();
        Addf(lines, "scenes count=%d", count);
        for (int32_t i = 0; i < count; i++) {
            const RsMusicScene* s = RsMusicZones_SceneAt(i);
            // rs_origin is the RS tile that OoT world (0,0) sits on; it is per-scene because
            // emit-scene.ts recentres each bake on its own bounding box. surface_anchor=0 means
            // the scene has no position in the world frame at all - an interior or underground
            // scene - so rect matching is skipped there and rs_origin is a placeholder, not a
            // location. Reported rather than hidden because "my rect does not fire in this scene"
            // is otherwise a mystery.
            Addf(lines, "scene[%d]=0x%X surface_anchor=%d rs_origin=%d,%d units_per_tile=%d", i,
                 (uint32_t)(uint16_t)s->sceneId, (s->flags & RS_SCENE_FLAG_NO_SURFACE_ANCHOR) ? 0 : 1,
                 (int32_t)s->rsOriginX, (int32_t)s->rsOriginY, (int32_t)s->unitsPerTile);
        }
        return 0;
    }

    if (sub == "firstvisit") {
        // The one-shot openers and whether each has been spent. This is what lets a run assert the
        // first-visit rule without ears: the flag must still be 0 after clipping the boundary (no
        // switch, so no activation) and 1 after the dwell fires.
        int32_t withOpener = 0;
        const int32_t count = RsMusicZones_Count();
        for (int32_t i = 0; i < count; i++) {
            const RsMusicZone* z = RsMusicZones_At(i);
            if (z->firstVisitTrack == NULL || z->firstVisitFlag == RS_ZONE_NO_FIRST_VISIT) {
                continue;
            }
            withOpener++;
            Addf(lines, "firstvisit zone=%s flag=%d track=0x%X set=%d", z->name, (int32_t)z->firstVisitFlag,
                 (uint32_t)z->firstVisitTrack->seqId, Flags_GetWorldFlag(z->firstVisitFlag) ? 1 : 0);
        }
        // Emitted last so the count is a witness that the loop ran, not a header a zero-zone table
        // could produce by doing nothing. `agenttest worldflag <n> 0` clears one to re-test.
        Addf(lines, "firstvisit count=%d", withOpener);
        return 0;
    }

    if (sub == "on" || sub == "off") {
        const int32_t value = (sub == "on") ? 1 : 0;
        CVarSetInteger(CVAR_RS_MUSIC_ON, value);
        CVarSave();
        // Flipping ON mid-scene does not silence the vanilla loader retroactively - the stand-down
        // happens in AfterSceneCommands, which has already run. The director will still take
        // player 0 on its next tick, but the clean A/B is: flip, then re-enter the scene.
        Addf(lines, "op=%s on=%d result=ok note=reload_scene_for_clean_ab", sub.c_str(), value);
        lines.emplace_back(RsMusic_Describe());
        return 0;
    }

    if (sub == "dwell") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_DWELL, "dwell", false);
    }
    if (sub == "fadeout") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_FADE_OUT, "fadeout", true);
    }
    if (sub == "fadein") {
        return SetSeconds(args, lines, CVAR_RS_MUSIC_FADE_IN, "fadein", true);
    }

    if (sub == "baseline") {
        // Bookmarks the count; it does NOT zero it, and there is deliberately no way to. A run that
        // wants to assert the negative takes a baseline here, walks, and compares - see the
        // negative assertion in AUDIO_SYSTEM.md: cross a boundary and come back inside the dwell
        // window, and the count must not move.
        //
        // This was `reset` through P0. The behaviour was right and the name was a lie: a human
        // typed it, watched the number stay, and concluded the feature was broken (#90, human
        // tuning pass 2026-09-09). `status` now reports transitions=, baseline= and the difference,
        // so the workflow reads correctly from either end.
        Addf(lines, "op=baseline baseline=%d transitions=%d", RsMusic_MarkBaseline(), RsMusic_TransitionCount());
        return 0;
    }

    if (sub == "reset") {
        // Retired name, kept as a refusal rather than an alias: it never reset anything, and
        // silently accepting it would keep the lie alive in muscle memory and in old run notes.
        // docs/test-runs/2026-09-08-zone-music-p0 uses it and stays as the historical record.
        Addf(lines, "op=reset result=error error=renamed_to_baseline (it never reset the counter; "
                    "`baseline` bookmarks it and `status` reports the difference)");
        return 1;
    }

    Addf(lines, "op=%s result=error error=unknown_subcommand", sub.c_str());
    return 1;
}

// --- the human sink: the `rsmusic` console command ----------------------------------------------

namespace {

int32_t MusicCommandHandler(std::shared_ptr<Ship::Console> console, const std::vector<std::string>& args,
                            std::string* output) {
    std::vector<std::string> sub(args.begin() + 1, args.end());
    std::vector<std::string> lines;
    const int32_t rc = MusicConsole_Run(sub, lines);
    if (output != nullptr) {
        for (size_t i = 0; i < lines.size(); i++) {
            if (i > 0) {
                *output += "\n";
            }
            // ConsoleWindow hands the output to vsnprintf as the FORMAT string, so a stray '%'
            // would be read as a conversion. Zone names are ours and contain none, but this
            // guards anything that grows into a line later.
            for (char c : lines[i]) {
                *output += c;
                if (c == '%') {
                    *output += '%';
                }
            }
        }
    }
    return rc;
}

// ShipInit "*" functions re-run on preset apply and config drop; AddCommand only warns on a
// duplicate, but the guard keeps the log clean.
void RegisterMusicConsole() {
    auto console = Ship::Context::GetRawInstance()->GetConsole();
    if (console->HasCommand("rsmusic")) {
        return;
    }
    console->AddCommand(
        "rsmusic",
        { MusicCommandHandler,
          "Zone-based overworld music (sturdy-bassoon#90): status | where | zones | scenes | firstvisit | "
          "on | off | dwell <sec> | fadeout <sec> | fadein <sec> | baseline. `where` prints Link's "
          "position in both OoT world units and RS absolute tiles plus the zone that wins there - "
          "that is how you check a rect against where he actually is. Fades are an 8-bit field in "
          "units of 1/30 s, so they clamp at 8.5 seconds and the reported unit count is what the "
          "engine really gets. `baseline` bookmarks the transition count so `status` can report the "
          "difference; it does NOT zero the counter, on purpose.",
          { { "status|where|zones|scenes|firstvisit|on|off|dwell|fadeout|fadein|baseline", Ship::ArgumentType::TEXT },
            { "seconds", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc musicConsoleInitFunc(RegisterMusicConsole);

} // namespace
