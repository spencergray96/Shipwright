/*
 * Combat music, opt-in per actor (sturdy-bassoon#90 P6). See CombatMusic.h for the policy and the
 * console surface, and docs/decisions/2026-10-26-combat-music-opt-in.md for why.
 */

#include "CombatMusic.h"
#include "MusicZones.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

#include <libultraship/bridge/consolevariablebridge.h>

#include "soh/Enhancements/console/ConsoleSink.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/ShipInit.hpp"
#include "soh/cvar_prefixes.h"

extern "C" {
#include <z64.h>
#include "macros.h"
extern PlayState* gPlayState;
}

// The director's master switch, read the same way ZoneDirector.cpp reads it.
#define CVAR_RS_MUSIC_ON CVAR_ENHANCEMENT("RsMusicZones")
// 1 = the opt-in policy (only listed actors start combat music); 0 = the stock rule. Defaults on: it
// IS the policy, and the CVar exists for the A/B, not as a preference.
#define CVAR_RS_MUSIC_COMBAT_OPT_IN CVAR_ENHANCEMENT("RsMusicCombatOptIn")
// audio/EnemyBGMDisable.cpp's switch. Respected rather than overridden: a player who turned enemy
// music off everywhere has not asked for listed actors to bring it back.
#define CVAR_ENEMY_BGM_DISABLE CVAR_AUDIO("EnemyBGMDisable")

namespace {

using ConsoleSink::Addf;

/*
 * THE LIST. Actors that start combat music in a director scene, one line each with the reason.
 *
 * Empty on purpose: rs/actors/ has no monster yet, and which ones should affect the music is a
 * per-encounter call to be made by feel when they exist - not something to guess for vanilla
 * enemies that never appear in RS content. A boss does not belong here (see CombatMusic.h).
 * The whole recipe - flags, trying it with `rsmusic combat add` first, checking it - is
 * CombatMusic.h's "HOW TO USE IT", recipe 1.
 *
 *   ACTOR_RS_<NAME>, // why this one gets combat music
 */
const std::vector<uint16_t> kListedActors = {};

// The test list: added from the console, discarded on relaunch, never saved - so an id listed for a
// run cannot leak into somebody's play. Small and fixed so a typo loop cannot grow it.
constexpr size_t TEST_LIST_MAX = 16;
std::vector<uint16_t> sTestActors;

// Never reset - a run compares two readings. Per actor per frame, so they are large numbers.
uint32_t sSuppressed = 0;
uint32_t sAllowed = 0;
int32_t sLastSuppressed = -1;
int32_t sLastAllowed = -1;

bool Contains(const std::vector<uint16_t>& list, uint16_t id) {
    return std::find(list.begin(), list.end(), id) != list.end();
}

bool IsListed(uint16_t id) {
    return Contains(kListedActors, id) || Contains(sTestActors, id);
}

bool PolicyOn() {
    return CVarGetInteger(CVAR_RS_MUSIC_COMBAT_OPT_IN, 1) != 0;
}

// Does the opt-in policy decide the gate right now? Only in a scene the director owns.
bool Applies() {
    return PolicyOn() && CVarGetInteger(CVAR_RS_MUSIC_ON, 0) != 0 && gPlayState != nullptr &&
           RsMusicZones_Scene(gPlayState->sceneNum) != nullptr;
}

/*
 * VB_DETECT_BGM_ENEMY runs from Attention_FindActorInCategory for every attention-enabled actor,
 * every frame, and decides which one is the music's "closest enemy". Here it is AUTHORITATIVE in a
 * director scene: the answer is computed from scratch rather than adjusted, so the result does not
 * depend on which other handlers ran first.
 *
 * A listed actor gets LeeverEnemyBGM's conditions rather than vanilla's - hostile and attention-
 * enabled, inside the 500-unit search, closer than the best so far - without vanilla's
 * ACTORCAT_ENEMY test, because listing an actor is the explicit statement that it counts.
 *
 * Two handlers can still disagree with this one and win by running later, both deliberate player
 * settings: EnemyBGMDisable (respected below too) and LeeverEnemyBGM.
 */
void OnDetectBgmEnemy(va_list args, bool* should) {
    if (!Applies()) {
        return;
    }
    Actor* actor = va_arg(args, Actor*);
    f32* bgmEnemyDistSq = va_arg(args, f32*);
    (void)va_arg(args, int32_t); // actorCategory: deliberately not consulted for a listed actor

    if (IsListed(actor->id) && CVarGetInteger(CVAR_ENEMY_BGM_DISABLE, 0) == 0) {
        const bool counts = CHECK_FLAG_ALL(actor->flags, ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_HOSTILE) &&
                            (actor->xyzDistToPlayerSq < SQ(500.0f)) && (actor->xyzDistToPlayerSq < *bgmEnemyDistSq);
        if (counts) {
            sAllowed++;
            sLastAllowed = actor->id;
        }
        *should = counts;
        return;
    }
    if (*should) {
        sSuppressed++;
        sLastSuppressed = actor->id;
    }
    *should = false;
}

void RegisterCombatMusic() {
    // Unconditional, with every CVar read inside: ShipInit does not re-run for a CVar the console
    // writes (soh-add-console-command's gotcha), and `rsmusic combat policy` is how a run flips it.
    COND_VB_SHOULD(VB_DETECT_BGM_ENEMY, true, { OnDetectBgmEnemy(args, should); });
}

RegisterShipInitFunc combatMusicInitFunc(RegisterCombatMusic);

bool ParseActorId(const std::string& s, uint16_t* out) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 0);
    if (end == s.c_str() || *end != '\0' || v >= 0xFFFF) {
        return false;
    }
    *out = (uint16_t)v;
    return true;
}

void FormatId(char* buf, size_t size, int32_t id) {
    if (id < 0) {
        std::snprintf(buf, size, "none");
    } else {
        std::snprintf(buf, size, "0x%X", (uint32_t)id);
    }
}

void AddStatus(std::vector<std::string>& lines) {
    std::string ids;
    const std::vector<uint16_t>* lists[] = { &kListedActors, &sTestActors };
    for (const std::vector<uint16_t>* list : lists) {
        for (uint16_t id : *list) {
            char one[12];
            std::snprintf(one, sizeof(one), "%s0x%X", ids.empty() ? "" : ",", (uint32_t)id);
            ids += one;
        }
    }
    char lastSuppressed[12];
    char lastAllowed[12];
    FormatId(lastSuppressed, sizeof(lastSuppressed), sLastSuppressed);
    FormatId(lastAllowed, sizeof(lastAllowed), sLastAllowed);
    Addf(lines,
         "combat policy=%s applies=%d global_off=%d listed=%d test=%d ids=%s suppressed=%u last_suppressed=%s "
         "allowed=%u last_allowed=%s",
         PolicyOn() ? "opt_in" : "vanilla", Applies() ? 1 : 0, CVarGetInteger(CVAR_ENEMY_BGM_DISABLE, 0) != 0 ? 1 : 0,
         (int32_t)kListedActors.size(), (int32_t)sTestActors.size(), ids.empty() ? "none" : ids.c_str(), sSuppressed,
         lastSuppressed, sAllowed, lastAllowed);
}

} // namespace

int32_t RsCombatMusic_Console(const std::vector<std::string>& args, std::vector<std::string>& lines) {
    const std::string sub = args.empty() ? std::string("status") : args[0];

    if (sub == "status") {
        AddStatus(lines);
        return 0;
    }

    if (sub == "add" || sub == "remove") {
        const std::string op = "combat_" + sub;
        if (args.size() < 2) {
            Addf(lines, "op=%s result=error error=missing_value", op.c_str());
            return 1;
        }
        uint16_t id = 0;
        if (!ParseActorId(args[1], &id)) {
            Addf(lines, "op=%s result=error error=bad_actor (expects an actor id 0..0xFFFE)", op.c_str());
            return 1;
        }
        if (sub == "add") {
            if (IsListed(id)) {
                Addf(lines, "op=%s actor=0x%X result=error error=already_listed", op.c_str(), (uint32_t)id);
                return 1;
            }
            if (sTestActors.size() >= TEST_LIST_MAX) {
                Addf(lines, "op=%s actor=0x%X result=error error=test_list_full max=%d", op.c_str(), (uint32_t)id,
                     (int32_t)TEST_LIST_MAX);
                return 1;
            }
            sTestActors.push_back(id);
        } else {
            auto it = std::find(sTestActors.begin(), sTestActors.end(), id);
            if (it == sTestActors.end()) {
                // A built-in entry is not removable from here either: it is code, and a console
                // that could unlist it would make the run test something other than what ships.
                Addf(lines, "op=%s actor=0x%X result=error error=not_in_test_list", op.c_str(), (uint32_t)id);
                return 1;
            }
            sTestActors.erase(it);
        }
        Addf(lines, "op=%s actor=0x%X result=ok test=%d", op.c_str(), (uint32_t)id, (int32_t)sTestActors.size());
        return 0;
    }

    if (sub == "clear") {
        sTestActors.clear();
        Addf(lines, "op=combat_clear result=ok test=0");
        return 0;
    }

    if (sub == "policy") {
        if (args.size() < 2 || (args[1] != "opt_in" && args[1] != "vanilla")) {
            Addf(lines, "op=combat_policy result=error error=bad_value (expects opt_in|vanilla)");
            return 1;
        }
        CVarSetInteger(CVAR_RS_MUSIC_COMBAT_OPT_IN, args[1] == "opt_in" ? 1 : 0);
        CVarSave();
        Addf(lines, "op=combat_policy policy=%s result=ok", args[1].c_str());
        AddStatus(lines);
        return 0;
    }

    Addf(lines, "op=combat_%s result=error error=unknown_subcommand", sub.c_str());
    return 1;
}
