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
        lines.emplace_back(RsMusic_Describe());
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
        Addf(lines, "where scene=0x%X opted_in=1 pos=%.1f,%.1f,%.1f rs=%d,%d zone=%s", sceneId,
             player->actor.world.pos.x, player->actor.world.pos.y, player->actor.world.pos.z, rsX, rsY,
             z != nullptr ? z->name : "none");
        return 0;
    }

    if (sub == "zones") {
        const int32_t count = RsMusicZones_Count();
        Addf(lines, "zones count=%d", count);
        for (int32_t i = 0; i < count; i++) {
            const RsMusicZone* z = RsMusicZones_At(i);
            Addf(lines, "zone[%d]=%s priority=%d fallback=%d rects=%d tracks=%d", i, z->name, (int32_t)z->priority,
                 (z->flags & RS_ZONE_FLAG_FALLBACK) ? 1 : 0, (int32_t)z->rectCount, (int32_t)z->trackCount);
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
            // emit-scene.ts recentres each bake on its own bounding box.
            Addf(lines, "scene[%d]=0x%X rs_origin=%d,%d units_per_tile=%d", i, (uint32_t)(uint16_t)s->sceneId,
                 (int32_t)s->rsOriginX, (int32_t)s->rsOriginY, (int32_t)s->unitsPerTile);
        }
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

    if (sub == "reset") {
        // There is deliberately no way to zero the counter from inside the director - a run that
        // wants a baseline takes one here, walks, and compares. See the negative assertion in
        // AUDIO_SYSTEM.md: cross a boundary and come back inside the dwell window, and the count
        // must not move.
        Addf(lines, "op=reset baseline=%d", RsMusic_TransitionCount());
        return 0;
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
    console->AddCommand("rsmusic",
                        { MusicCommandHandler,
                          "Zone-based overworld music (sturdy-bassoon#90): status | where | zones | scenes | "
                          "on | off | dwell <sec> | fadeout <sec> | fadein <sec> | reset. `where` prints Link's "
                          "position in both OoT world units and RS absolute tiles plus the zone that wins there - "
                          "that is how you check a rect against where he actually is. Fades are an 8-bit field in "
                          "units of 1/30 s, so they clamp at 8.5 seconds and the reported unit count is what the "
                          "engine really gets.",
                          { { "status|where|zones|scenes|on|off|dwell|fadeout|fadein|reset", Ship::ArgumentType::TEXT },
                            { "seconds", Ship::ArgumentType::TEXT, true } } });
}

RegisterShipInitFunc musicConsoleInitFunc(RegisterMusicConsole);

} // namespace
