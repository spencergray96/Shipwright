// Hazardous waters: the kinds' rows, the scenes' rectangles, the boat's tick and the `boat hazard` console
// (sturdy-bassoon#285, RsBoatHazard.h).

#include "soh/frame_interpolation.h" // before OPEN_DISPS: the Tempor Tantrum note's link rule for draws in C++

#include "RsBoatHazard.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <libultraship/bridge/consolevariablebridge.h>

#include "RsBoatFit.h"
#include "RsBoatHealth.h"
#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "functions.h"
#include "macros.h"
#include "variables.h"

extern PlayState* gPlayState;
}

namespace {

using ConsoleSink::Addf;

// --- the kinds ------------------------------------------------------------------------------------
//
// RS's numbers where the wiki has them (the research note's section 2), ours where it does not (the owner, #285,
// "fine for now"): RS's "every few seconds" is 3 s and "every few ticks" 1.8 s (three RS ticks). The slowdown is RS's
// "significantly slowed" as half the hull's top speed, fetid's "severely" as 35%. Fetid's 5 is RS's hit on the player,
// put on the boat (the owner: hazards hit the boat, never Link). Icy's rising hit has no curve in RS: 2, 2 more a hit,
// to 20. Each is a knob of `boat hazard tune`.
struct KindRow {
    const char* name;    // the console's word and the markers'
    const char* cvarKey; // the knobs' CVar stem
    float knobs[RS_BOAT_HAZARD_KNOB_COUNT];
    const char* alert; // RS's words, naming the missing part (the research note's section 3)
    uint8_t rgba[4];   // its sheet's colour: the kind's tint mixed into the scene water's blue (40, 96, 150), since
                       // the sheets draw opaque, as the scene water's own does (measured, #285 run)
};

const KindRow kKinds[RS_BOAT_HAZARD_COUNT] = {
    { "stormy",
      "Stormy",
      { 0.5f, 10.0f, 3.0f, 0.0f, 0.0f },
      "The sails are not strong enough to withstand the lightning in these stormy seas!",
      { 49, 82, 118, 255 } },
    { "kelp",
      "Kelp",
      { 0.5f, 4.0f, 1.8f, 0.0f, 0.0f },
      "The helm is not strong enough to move through the tangled kelp in these waters!",
      { 40, 118, 80, 255 } },
    { "crystal",
      "Crystal",
      { 0.5f, 20.0f, 3.0f, 0.0f, 0.0f },
      "The keel is not strong enough to withstand the sharp crystals floating in the water!",
      { 68, 154, 184, 255 } },
    // RS's two fetid messages are about the player ("...will soon start to infect you!"); ours hurts the boat, so its
    // words say so in RS's shape.
    { "fetid",
      "Fetid",
      { 0.35f, 5.0f, 1.8f, 0.0f, 0.0f },
      "Without an inoculation station, the acrid waters eat away at the hull!",
      { 72, 98, 96, 255 } },
    // Likewise icy's, which in RS freezes the player: ours grinds the hull.
    { "icy",
      "Icy",
      { 0.5f, 2.0f, 3.0f, 2.0f, 20.0f },
      "Without a brazier's warmth, the icy seas freeze the boat to its core!",
      { 128, 163, 198, 255 } },
};

struct KnobInfo {
    const char* name;
    const char* cvarKey;
    float min;
    float max;
};

const KnobInfo kKnobs[RS_BOAT_HAZARD_KNOB_COUNT] = {
    { "share", "Share", 0.05f, 1.0f },
    { "hit", "Hit", 0.0f, 200.0f },
    { "every", "Every", 0.1f, 60.0f },
    { "step", "Step", 0.0f, 100.0f },
    { "cap", "Cap", 0.0f, 500.0f },
};

std::string KnobCvar(int32_t kind, int32_t knob) {
    return std::string(CVAR_ENHANCEMENT("RsBoatHazard")) + kKinds[kind].cvarKey + kKnobs[knob].cvarKey;
}

bool KindValid(int32_t kind) {
    return kind >= 0 && kind < RS_BOAT_HAZARD_COUNT;
}

// --- the rectangles -------------------------------------------------------------------------------
//
// Per scene in code, beside the scene's water (RsSceneWater.cpp; the owner, #285): a rectangle of one kind, in world
// units. Its sheet sits on the water found under its centre, once the scene is flooded.
struct HazardRect {
    int32_t kind;
    int16_t x0;
    int16_t z0;
    int16_t x1;
    int16_t z1;
};

// Boat Docks 282's test waters: one patch of each kind in a row, in the rows the map grew south for them.
#include "RsBoatHazardBoatDocks282.inc"

struct SceneHazards {
    int16_t scene;
    const HazardRect* rects;
    uint32_t count;
};

const SceneHazards kSceneHazards[] = {
    { SCENE_BOAT_DOCKS_282, kBoatDocks282Hazards,
      (uint32_t)(sizeof(kBoatDocks282Hazards) / sizeof(kBoatDocks282Hazards[0])) },
};

struct Area {
    HazardRect rect;
    bool console; // `boat hazard add`'s, this visit only
    float y;      // the water's surface under its centre, once found
    bool found;
    int32_t looks; // frames its water has been looked for: a square added on land stops looking, undrawn
};

constexpr int32_t kWaterLooks = 60;

std::vector<Area> sAreas;

void OnSceneInitHazards(int16_t sceneNum) {
    sAreas.clear();
    for (const SceneHazards& s : kSceneHazards) {
        if (s.scene != sceneNum) {
            continue;
        }
        for (uint32_t i = 0; i < s.count; i++) {
            sAreas.push_back(Area{ s.rects[i], false, 0.0f, false, 0 });
        }
    }
}

bool Inside(const HazardRect& r, float x, float z) {
    return x >= r.x0 && x <= r.x1 && z >= r.z0 && z <= r.z1;
}

void Mark(const char* fmt, ...) {
    char line[256];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    RsAgent_Marker(line);
}

void MarkBoat(const char* event, const RsBoat* boat, int32_t kind, const char* fields) {
    Mark("rs_boat event=%s hazard=%s kind=%s owned=%d %s x=%d z=%d", event, kKinds[kind].name,
         RsBoat_KindName(boat->kind), boat->owned, fields, (int)lroundf(boat->dyna.actor.world.pos.x),
         (int)lroundf(boat->dyna.actor.world.pos.z));
}

void RegisterRsBoatHazard() {
    static bool hooked = false;
    if (hooked) {
        return;
    }
    hooked = true;
    COND_HOOK(OnSceneInit, true, OnSceneInitHazards);
}

RegisterShipInitFunc rsBoatHazardInitFunc(RegisterRsBoatHazard);

} // namespace

extern "C" const char* RsBoatHazard_KindName(int32_t kind) {
    return KindValid(kind) ? kKinds[kind].name : "?";
}

extern "C" int32_t RsBoatHazard_KindByName(const char* name) {
    for (int32_t kind = 0; name != nullptr && kind < RS_BOAT_HAZARD_COUNT; kind++) {
        if (std::strcmp(kKinds[kind].name, name) == 0) {
            return kind;
        }
    }
    return -1;
}

extern "C" float RsBoatHazard_Knob(int32_t kind, int32_t knob) {
    if (!KindValid(kind) || knob < 0 || knob >= RS_BOAT_HAZARD_KNOB_COUNT) {
        return 0.0f;
    }
    const float v = CVarGetFloat(KnobCvar(kind, knob).c_str(), kKinds[kind].knobs[knob]);
    return CLAMP(v, kKnobs[knob].min, kKnobs[knob].max);
}

extern "C" void RsBoatHazard_MaskNames(uint32_t mask, char* out, int32_t size) {
    std::string names;
    for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
        if (mask & (1u << kind)) {
            names += (names.empty() ? "" : ",") + std::string(kKinds[kind].name);
        }
    }
    std::snprintf(out, (size_t)size, "%s", names.empty() ? "none" : names.c_str());
}

extern "C" uint32_t RsBoatHazard_KindsAt(float x, float z) {
    uint32_t mask = 0;
    for (const Area& a : sAreas) {
        if (Inside(a.rect, x, z)) {
            mask |= 1u << a.rect.kind;
        }
    }
    return mask;
}

extern "C" void RsBoatHazard_Tick(RsBoat* boat, PlayState* play) {
    Vec3f helm;
    RsBoatFitStats fit;
    RsBoat_HelmPos(boat, &helm);
    RsBoatFit_Stats(boat->kind, &fit);
    const uint32_t at = RsBoatHazard_KindsAt(helm.x, helm.z);
    const uint32_t harm = at & ~fit.waters;
    const bool aboard = RsBoat_LinkAboard(boat, play) != 0;
    const uint32_t wasHarm = boat->hazardHarm;
    float share = 1.0f;

    for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
        const uint32_t bit = 1u << kind;
        if ((at & bit) && !(boat->hazardIn & bit)) {
            char fields[48];
            std::snprintf(fields, sizeof(fields), "protected=%d aboard=%d", (harm & bit) ? 0 : 1, aboard ? 1 : 0);
            MarkBoat("hazard_enter", boat, kind, fields);
        } else if (!(at & bit) && (boat->hazardIn & bit)) {
            char fields[48];
            std::snprintf(fields, sizeof(fields), "hits=%d", (int)boat->hazardHits[kind]);
            MarkBoat("hazard_leave", boat, kind, fields);
        }
        if (!(harm & bit)) {
            boat->hazardTicks[kind] = 0;
            boat->hazardHits[kind] = 0;
            continue;
        }
        share = MIN(share, RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_SHARE));
    }
    boat->hazardIn = at;
    boat->hazardHarm = harm;
    boat->speedMod.share = share;

    // The hits: only with Link aboard (#284), so a boat left in a storm keeps its count where it was and never sinks.
    // Each kind counts on its own; the first lands a whole interval in.
    if (!aboard) {
        return;
    }
    for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
        // The tick it enters counts nothing, so the first hit is a whole interval in.
        if (!(harm & (1u << kind)) || !(wasHarm & (1u << kind))) {
            continue;
        }
        const int32_t every = MAX(1, (int32_t)lroundf(RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_EVERY) * 20.0f));
        if (++boat->hazardTicks[kind] < every) {
            continue;
        }
        boat->hazardTicks[kind] = 0;
        float hit = RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_HIT) +
                    RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_STEP) * (float)boat->hazardHits[kind];
        const float cap = RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_CAP);
        if (cap > 0.0f) {
            hit = MIN(hit, cap);
        }
        boat->hazardHits[kind]++;
        const int32_t dealt = RsBoatHealth_Hit(boat, (int32_t)lroundf(hit), RS_BOAT_HIT_HAZARD);
        char fields[64];
        std::snprintf(fields, sizeof(fields), "n=%d hit=%d dealt=%d hp=%d", (int)boat->hazardHits[kind],
                      (int)lroundf(hit), (int)dealt, (int)RsBoatHealth_Of(boat));
        MarkBoat("hazard_hit", boat, kind, fields);
        if (boat->sinkTicks > 0) {
            return; // capsized: nothing more lands on a boat going down
        }
    }
}

extern "C" uint32_t RsBoatHazard_Harmful(const RsBoat* boat) {
    return boat != nullptr ? boat->hazardHarm : 0;
}

extern "C" const char* RsBoatHazard_Alert(int32_t kind) {
    return KindValid(kind) ? kKinds[kind].alert : "";
}

// The sheets, at file scope: OPEN_DISPS in an anonymous namespace does not link (the Tempor Tantrum note). One above
// the scene water's own sheet (RsSceneWater.cpp, two under the surface), still under the surface for the water rings.
extern "C" void RsBoatHazard_DrawSheets(void) {
    if (gPlayState == nullptr || sAreas.empty()) {
        return;
    }
    for (Area& a : sAreas) {
        if (!a.found && a.looks < kWaterLooks) {
            a.looks++;
            float y = GET_PLAYER(gPlayState)->actor.world.pos.y;
            a.found = RsBoat_SurfaceAt(gPlayState, (a.rect.x0 + a.rect.x1) * 0.5f, (a.rect.z0 + a.rect.z1) * 0.5f, &y,
                                       nullptr) != 0;
            a.y = y;
        }
    }
    GraphicsContext* gfxCtx = gPlayState->state.gfxCtx;
    Vtx* vtx = (Vtx*)Graph_Alloc(gfxCtx, sizeof(Vtx) * 4 * sAreas.size());

    OPEN_DISPS(gfxCtx);
    Gfx_SetupDL_25Xlu(gfxCtx);
    Matrix_Translate(0.0f, 0.0f, 0.0f, MTXMODE_NEW);
    gSPMatrix(POLY_XLU_DISP++, MATRIX_NEWMTX(gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    gSPClearGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    gDPSetCombineMode(POLY_XLU_DISP++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    for (size_t i = 0; i < sAreas.size(); i++) {
        const Area& a = sAreas[i];
        if (!a.found) {
            continue;
        }
        const HazardRect& r = a.rect;
        const uint8_t* c = kKinds[r.kind].rgba;
        const int16_t y = (int16_t)(lroundf(a.y) - 1);
        const int16_t xs[4] = { r.x0, r.x1, r.x1, r.x0 };
        const int16_t zs[4] = { r.z0, r.z0, r.z1, r.z1 };
        Vtx* v = &vtx[i * 4];
        for (int k = 0; k < 4; k++) {
            v[k] = {};
            v[k].v.ob[0] = xs[k];
            v[k].v.ob[1] = y;
            v[k].v.ob[2] = zs[k];
            v[k].v.cn[1] = 127;
            v[k].v.cn[3] = 255;
        }
        gDPSetPrimColor(POLY_XLU_DISP++, 0, 0, c[0], c[1], c[2], c[3]);
        gSPVertex(POLY_XLU_DISP++, (uintptr_t)v, 4, 0);
        gSP2Triangles(POLY_XLU_DISP++, 0, 1, 2, 0, 0, 2, 3, 0);
    }
    gSPSetGeometryMode(POLY_XLU_DISP++, G_CULL_BACK | G_LIGHTING);
    CLOSE_DISPS(gfxCtx);
}

// --- the console ----------------------------------------------------------------------------------

namespace {

void ClearKind(int32_t kind) {
    for (int32_t knob = 0; knob < RS_BOAT_HAZARD_KNOB_COUNT; knob++) {
        CVarClear(KnobCvar(kind, knob).c_str());
    }
}

bool ParseFloat(const std::string& s, float* out) {
    char* end = nullptr;
    *out = std::strtof(s.c_str(), &end);
    return end != s.c_str() && *end == '\0';
}

std::string Names(uint32_t mask) {
    char buf[96];
    RsBoatHazard_MaskNames(mask, buf, (int32_t)sizeof(buf));
    return buf;
}

std::string KindLine(int32_t kind) {
    char line[160];
    std::snprintf(line, sizeof(line), "kind name=%s share=%g hit=%g every=%g step=%g cap=%g", kKinds[kind].name,
                  (double)RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_SHARE),
                  (double)RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_HIT),
                  (double)RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_EVERY),
                  (double)RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_STEP),
                  (double)RsBoatHazard_Knob(kind, RS_BOAT_HAZARD_KNOB_CAP));
    return line;
}

int32_t Tune(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    // tune | tune default | tune <kind> default | tune <kind> <knob> <value>
    if (args.size() == 2 || (args.size() == 3 && args[2] == "default")) {
        if (args.size() == 3) {
            for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
                ClearKind(kind);
            }
            CVarSave();
        }
        for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
            lines.push_back(KindLine(kind));
        }
        Addf(lines, "op=hazard result=ok");
        return 0;
    }
    const int32_t kind = RsBoatHazard_KindByName(args[2].c_str());
    if (kind < 0) {
        Addf(lines, "op=hazard result=error error=bad_kind kinds=stormy,kelp,crystal,fetid,icy");
        return 1;
    }
    if (args.size() == 4 && args[3] == "default") {
        ClearKind(kind);
        CVarSave();
    } else if (args.size() == 5) {
        int32_t knob = -1;
        for (int32_t k = 0; k < RS_BOAT_HAZARD_KNOB_COUNT; k++) {
            if (args[3] == kKnobs[k].name) {
                knob = k;
            }
        }
        float value = 0.0f;
        if (knob < 0 || !ParseFloat(args[4], &value) || value < kKnobs[knob].min || value > kKnobs[knob].max) {
            std::string ranges;
            for (const KnobInfo& k : kKnobs) {
                char part[48];
                std::snprintf(part, sizeof(part), " %s=%g..%g", k.name, (double)k.min, (double)k.max);
                ranges += part;
            }
            lines.push_back("op=hazard result=error error=bad_value" + ranges);
            return 1;
        }
        CVarSetFloat(KnobCvar(kind, knob).c_str(), value);
        CVarSave();
    } else {
        Addf(lines, "op=usage result=error usage=\"boat " RS_BOAT_HAZARD_GRAMMAR "\"");
        return 1;
    }
    lines.push_back("op=hazard result=ok " + KindLine(kind).substr(5));
    return 0;
}

} // namespace

int32_t RsBoatHazard_ConsoleRun(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    if (args[0] != "hazard") {
        return -1;
    }
    const std::string verb = args.size() >= 2 ? args[1] : "";
    // `tune` is settings, so it works from the title screen too; the rest need a scene.
    if (verb == "tune") {
        return Tune(args, lines);
    }
    if (gPlayState == nullptr) {
        Addf(lines, "op=hazard result=error error=no_scene");
        return 1;
    }
    const Player* player = GET_PLAYER(gPlayState);
    if (verb == "here" && args.size() == 2) {
        const float x = player->actor.world.pos.x;
        const float z = player->actor.world.pos.z;
        Addf(lines, "op=hazard result=ok x=%d z=%d kinds=%s", (int)lroundf(x), (int)lroundf(z),
             Names(RsBoatHazard_KindsAt(x, z)).c_str());
        if (const RsBoat* boat = RsBoatHealth_LinkBoat()) {
            Vec3f helm;
            RsBoatFitStats fit;
            RsBoat_HelmPos(boat, &helm);
            RsBoatFit_Stats(boat->kind, &fit);
            Addf(lines, "boat kind=%s owned=%d helm_x=%d helm_z=%d in=%s harmful=%s protects=%s share=%d",
                 RsBoat_KindName(boat->kind), boat->owned, (int)lroundf(helm.x), (int)lroundf(helm.z),
                 Names(boat->hazardIn).c_str(), Names(boat->hazardHarm).c_str(), Names(fit.waters).c_str(),
                 (int)lroundf(boat->speedMod.share * 100.0f));
        }
        return 0;
    }
    if (verb == "at" && args.size() == 4) {
        float x = 0.0f;
        float z = 0.0f;
        if (!ParseFloat(args[2], &x) || !ParseFloat(args[3], &z)) {
            Addf(lines, "op=hazard result=error error=bad_value");
            return 1;
        }
        Addf(lines, "op=hazard result=ok x=%d z=%d kinds=%s", (int)lroundf(x), (int)lroundf(z),
             Names(RsBoatHazard_KindsAt(x, z)).c_str());
        return 0;
    }
    if (verb == "list" && args.size() == 2) {
        for (size_t i = 0; i < sAreas.size(); i++) {
            const HazardRect& r = sAreas[i].rect;
            Addf(lines, "area n=%d kind=%s x0=%d z0=%d x1=%d z1=%d from=%s", (int)i, kKinds[r.kind].name, r.x0, r.z0,
                 r.x1, r.z1, sAreas[i].console ? "console" : "scene");
        }
        for (int32_t kind = 0; kind < RS_BOAT_HAZARD_COUNT; kind++) {
            lines.push_back(KindLine(kind));
        }
        for (int32_t kind = 0; kind < RS_BOAT_KIND_COUNT; kind++) {
            RsBoatFitStats fit;
            RsBoatFit_Stats(kind, &fit);
            Addf(lines, "protects boat=%s waters=%s", RsBoat_KindName(kind), Names(fit.waters).c_str());
        }
        Addf(lines, "op=hazard result=ok areas=%d", (int)sAreas.size());
        return 0;
    }
    if (verb == "add" && (args.size() == 4 || args.size() == 5) && args[3] == "here") {
        const int32_t kind = RsBoatHazard_KindByName(args[2].c_str());
        float half = 200.0f;
        if (kind < 0) {
            Addf(lines, "op=hazard result=error error=bad_kind kinds=stormy,kelp,crystal,fetid,icy");
            return 1;
        }
        if (args.size() == 5 && (!ParseFloat(args[4], &half) || half < 10.0f || half > 4000.0f)) {
            Addf(lines, "op=hazard result=error error=bad_value half=10..4000");
            return 1;
        }
        const float x = player->actor.world.pos.x;
        const float z = player->actor.world.pos.z;
        // Clamped to the rectangle's int16 corners: in a vanilla scene far from its origin a square must not wrap.
        auto corner = [](float v) { return (int16_t)CLAMP(lroundf(v), -32768L, 32767L); };
        const HazardRect r = { kind, corner(x - half), corner(z - half), corner(x + half), corner(z + half) };
        sAreas.push_back(Area{ r, true, 0.0f, false, 0 });
        Addf(lines, "op=hazard result=ok n=%d kind=%s x0=%d z0=%d x1=%d z1=%d", (int)sAreas.size() - 1,
             kKinds[kind].name, r.x0, r.z0, r.x1, r.z1);
        return 0;
    }
    if (verb == "clear" && args.size() == 2) {
        int removed = 0;
        for (size_t i = sAreas.size(); i-- > 0;) {
            if (sAreas[i].console) {
                sAreas.erase(sAreas.begin() + (long)i);
                removed++;
            }
        }
        Addf(lines, "op=hazard result=ok removed=%d areas=%d", removed, (int)sAreas.size());
        return 0;
    }
    Addf(lines, "op=usage result=error usage=\"boat " RS_BOAT_HAZARD_GRAMMAR "\"");
    return 1;
}
