#ifndef SOH_RS_NPC_IDS_H
#define SOH_RS_NPC_IDS_H

#include <stdint.h>
#include "soh/Enhancements/rs/RsAssert.h"
#include "soh/Enhancements/rs/quest/QuestIds.h" // QuestTier and the band-naming convention

// ============================================================================================
//  NPC IDS - BAKED INTO SCENE SOURCES AS ActorEntry.params. NEVER REORDER. NEVER REUSE.
// ============================================================================================
//
// An NpcId is the identity of a CHARACTER (sturdy-bassoon#58 D21), orthogonal to two things it is
// easy to conflate with:
//
//     actor type       the code and behaviour     recycled - one type serves many NPCs
//     model/skeleton   the visual                 recycled - RS-style reuse is expected
//     NpcId            the character              UNIQUE, and STABLE ACROSS SCENES
//
// One character placed in several scenes is ONE NPC: same id, same dialogue rule table, same
// state. That works with no per-scene handling at all because quest progress and world flags are
// GLOBAL stores (D22) - unlike vanilla's per-scene `swch`/`chest`/`collect`/`clear`, which is
// exactly why an NPC placed twice in vanilla silently keeps two sets of state. Nothing in this
// system may branch on `sceneNum`.
//
// WHY "NEVER REORDER" WHEN THIS IS NOT SERIALIZED. An NpcId is not written to a save file. It IS
// written into every scene's compiled `ActorEntry.params` (RsActorParams.h) - so renumbering does
// not corrupt a save, it silently REPOINTS every existing placement at a different character.
// Same failure class, different file. Retire an id in place with a _RETIRED suffix rather than
// deleting or reusing it, and give every enumerator an explicit `= N`.
//
// Bands mirror QuestIds.h: production characters count up from 0, debug/test ones from
// NPC_ID_DEBUG_FIRST. RsNpc_Register refuses a definition whose declared tier does not match its
// band, and a debug NPC may not set a production world flag (`quest debugwipe` could not clear it).
// NPC_MAX may be RAISED, never lowered - but see the textId assert in NpcDialogueDef.h, which is
// what actually binds it.

#define NPC_MAX 256
#define NPC_ID_DEBUG_FIRST 192

typedef enum NpcId {
    // --- production band: [0, NPC_ID_DEBUG_FIRST) ------------------------------------------
    NPC_COOK = 0, // sturdy-bassoon#58 P4 - the Cook's Assistant quest-giver, and the first
                  // production-band character. Defined in dialogue/npcs/CookNpc.cpp, placed in the
                  // test level (0x614). Six rules, one of which speaks all seven of the quest's
                  // incomplete collection states.
    NPC_SAILMAKER = 1, // sturdy-bassoon#287 - puts the player's own picture on a boat's sail (arms a dropped
                       // file, or opens the file dialog). Defined in dialogue/npcs/SailmakerNpc.cpp, spawned on
                       // Lake Hylia's shore beside the lake's boat. A proof of concept: no quest, no model of
                       // its own.

    // --- debug band: [NPC_ID_DEBUG_FIRST, NPC_MAX) ----------------------------------------
    NPC_DEBUG_GIVER = 192, // the P3 quest-giver (dialogue/npcs/DebugNpcs.cpp). Placed TWICE in
                           // 0x614 and once in 0x629 - the multi-scene and double-fire proofs.
    NPC_DEBUG_THREE = 193, // one rule carrying THREE options: the live proof that the response
                           // shape is not binary (D11, sturdy-bassoon#59).
    NPC_DEBUG_TWIN = 194,  // a DIFFERENT character on the same actor type and the same model as
                           // 192 - the other axis of D21, proven by it being unaffected by 192.
    NPC_DEBUG_FOUR = 195,  // one rule carrying a QUESTION plus FOUR options, in the five-row box
                           // (sturdy-bassoon#59). 193 proves the shape is not binary; this one
                           // proves it is not capped at what vanilla's control codes offer, and
                           // is the shape a quest menu actually wants: a prompt and a Nevermind.
    NPC_DEBUG_FLOOR = 196, // the floor-convention fixture (sturdy-bassoon#94): a body, an option
                           // label and a reply that all carry `{floor:N}`, so one `npc dump` under
                           // each convention proves the substitution reaches all three.
    NPC_DEBUG_TREE = 197,  // the dialogue-TREE fixture (sturdy-bassoon#96): an entry rule whose
                           // reply leads into a two-screen tree that loops back on itself, and one
                           // option that only exists after another has been taken. Every claim #96
                           // makes, in one conversation.
    NPC_DEBUG_PAGE = 198,  // the AUTHORED-PAGING fixture (#96 P3): a screen whose last option is
                           // "More..." leading to a second page whose first is "Back". Paging is
                           // content, not an engine feature - this is the proof it needs no
                           // engine support, and the demonstration of what it costs an author.
    NPC_DEBUG_QUEST_MENU = 199, // the tree follow-ups (#96): entry STATEMENTS that continue, a
                                // node GROUP that says different things before and during a quest,
                                // and the missing-steps clause on a node covered each of the three
                                // ways registration accepts one. "Ask about the job, hear what is
                                // left, back to the menu".
    NPC_DEBUG_SHOP = 200, // the Majora's Mask verbs (sturdy-bassoon#281): a price behind
                          // RupeesAtLeast and one nobody can pay, wares sold with BUY_ITEM, an
                          // errand whose start and completion each show a journal notice, and an
                          // item handed over with GIVE_ITEM that re-opens the conversation.
    NPC_DEBUG_SCROLL = 201, // a SCROLLING option list (sturdy-bassoon#290): ten options on its
                            // entry rule, one gated, and a node with sixteen - the cap.
    NPC_DEBUG_MEMORY = 202, // CHOICE MEMORY (sturdy-bassoon#281): a menu node, a sixteen-option child
                            // and a third level, each with ways back up, so a return restores the
                            // pick and a second visit to a child is fresh.
    NPC_DEBUG_SPEAKER_A = 203, // A SPEAKER PER SCREEN (sturdy-bassoon#281): the owner of a conversation
                               // with two speakers, placed beside 204 - whose lines move the camera
    NPC_DEBUG_SPEAKER_B = 204, // the second speaker of 203's conversation, placed TWICE: beside 203 and
                               // far away, so the nearer placement is the one that gets the floor
    NPC_DEBUG_SPEAKER_ABSENT = 205, // registered and never placed: the speaker who does not turn up
    NPC_DEBUG_TRADER = 206, // THE INVENTORY (sturdy-bassoon#286): counted items given, bought, taken and sold, a
                            // gift Link cannot carry, and a key item handed over with its ceremony
} NpcId;

#define NPC_ID_IS_VALID(id) ((id) >= 0 && (id) < NPC_MAX)
#define NPC_ID_IS_DEBUG(id) ((id) >= NPC_ID_DEBUG_FIRST)
#define NPC_ID_TIER(id) (NPC_ID_IS_DEBUG(id) ? QUEST_TIER_DEBUG : QUEST_TIER_PROD)

RS_STATIC_ASSERT(NPC_ID_DEBUG_FIRST > 0, "the production band must be non-empty");
RS_STATIC_ASSERT(NPC_ID_DEBUG_FIRST < NPC_MAX, "the debug band must be non-empty");

RS_STATIC_ASSERT(NPC_COOK >= 0 && NPC_COOK < NPC_ID_DEBUG_FIRST, "NPC_COOK must sit in the production band");
RS_STATIC_ASSERT(NPC_DEBUG_GIVER >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_GIVER < NPC_MAX,
                 "NPC_DEBUG_GIVER must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_THREE >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_THREE < NPC_MAX,
                 "NPC_DEBUG_THREE must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_TWIN >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_TWIN < NPC_MAX,
                 "NPC_DEBUG_TWIN must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_FOUR >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_FOUR < NPC_MAX,
                 "NPC_DEBUG_FOUR must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_FLOOR >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_FLOOR < NPC_MAX,
                 "NPC_DEBUG_FLOOR must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_TREE >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_TREE < NPC_MAX,
                 "NPC_DEBUG_TREE must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_PAGE >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_PAGE < NPC_MAX,
                 "NPC_DEBUG_PAGE must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_QUEST_MENU >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_QUEST_MENU < NPC_MAX,
                 "NPC_DEBUG_QUEST_MENU must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_SHOP >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_SHOP < NPC_MAX,
                 "NPC_DEBUG_SHOP must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_SCROLL >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_SCROLL < NPC_MAX,
                 "NPC_DEBUG_SCROLL must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_MEMORY >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_MEMORY < NPC_MAX,
                 "NPC_DEBUG_MEMORY must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_SPEAKER_A >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_SPEAKER_A < NPC_MAX,
                 "NPC_DEBUG_SPEAKER_A must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_SPEAKER_B >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_SPEAKER_B < NPC_MAX,
                 "NPC_DEBUG_SPEAKER_B must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_SPEAKER_ABSENT >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_SPEAKER_ABSENT < NPC_MAX,
                 "NPC_DEBUG_SPEAKER_ABSENT must sit in the debug band");
RS_STATIC_ASSERT(NPC_DEBUG_TRADER >= NPC_ID_DEBUG_FIRST && NPC_DEBUG_TRADER < NPC_MAX,
                 "NPC_DEBUG_TRADER must sit in the debug band");

#endif // SOH_RS_NPC_IDS_H
