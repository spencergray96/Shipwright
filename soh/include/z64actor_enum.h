#pragma once

#ifndef Z64ACTOR_ENUM_H
#define Z64ACTOR_ENUM_H

#define DEFINE_ACTOR_INTERNAL(_0, enum, _2) enum,
#define DEFINE_ACTOR_UNSET(enum) enum,
#define DEFINE_ACTOR(_0, enum, _2) DEFINE_ACTOR_INTERNAL(_0, enum, _2)

enum ActorID {
#include "tables/actor_table.h"
    /* 0x01D7 */ ACTOR_ID_MAX // originally "ACTOR_DLF_MAX"
};

// Actors added by Ship. They start past ACTOR_ID_MAX so it keeps working as the vanilla bound and as
// the "no actor" sentinel (see location_list.cpp). ActorDB hands out ids from ACTOR_ID_EXTRA_MAX up
// to anything registering dynamically at runtime.
//
// SOH [Fork] The index comments in this enum were 0x45 low (ACTOR_ID_MAX read 0x0192; the table's
// last row is 0x01D6), found when `spawn 405` - the old ACTOR_RS_QUEST_ITEM comment - put down an
// En_Shopnuts (sturdy-bassoon#90 P5 run). Corrected against tables/actor_table.h. Prefer spawning
// the mod's actors by ActorDB name (`spawn Rs_QuestItem <params>`), which no comment can get wrong.
enum ActorIDExtra {
    /* 0x01D8 */ ACTOR_EN_PARTNER = ACTOR_ID_MAX + 1,
    // #region SOH [Fork] The mod's own actors (sturdy-bassoon#58 P3). They need a COMPILE-TIME id
    // because a compiled-in scene names them in a `static ActorEntry[]` row, which rules out
    // ActorDB's dynamic ids. This enum is SoH's own actor band, not the 428-overlay tree in
    // soh/src/overlays/actors - keeping ours out of that tree is what keeps rebases onto develop
    // sane (D6), and adding a name here does not put them in it.
    /* 0x01D9 */ ACTOR_RS_NPC,        // Enhancements/rs/actors/RsNpc.c - a quest-giver; params = NpcId
    /* 0x01DA */ ACTOR_RS_QUEST_ITEM, // Enhancements/rs/actors/RsQuestItem.c - params = (quest, step)
    // #endregion
    ACTOR_ID_EXTRA_MAX
};

#undef DEFINE_ACTOR
#undef DEFINE_ACTOR_INTERNAL
#undef DEFINE_ACTOR_UNSET

#endif