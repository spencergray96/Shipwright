#ifndef SOH_RS_NPC_DIALOGUE_DEF_H
#define SOH_RS_NPC_DIALOGUE_DEF_H

#include <stdint.h>
#include "NpcIds.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"

// The DIALOGUE RULE TABLE (sturdy-bassoon#58 P3 / #64, decisions D8/D11), as plain C data so a C
// actor can carry a file-scope table and initialise it with brace lists.
//
// D11 - dialogue is an ORDERED rule table per NPC, FIRST MATCH WINS, gating on the SAME predicate
// vocabulary a quest requirement and a journal block use. Owned by the NPC, not the quest: one
// character will eventually serve several quests and needs one coherent priority order across all
// of them.
//
// The crux, restated because it is the thing that is easy to lose: this only beats an inline
// `switch` if the predicates are CONSTRAINED DATA. That is what lets the console print every rule,
// show which one matched and why, and diff two NPCs. An arbitrary lambda would make this an
// if-chain with extra indirection and throw the introspection away. Grow the vocabulary
// (QuestPredicate.h) before reaching for an escape hatch.
//
// THE LAST RULE MUST BE UNCONDITIONAL, refused at registration otherwise. That is what makes D8's
// "an NPC whose gate is unmet never offers the quest and FALLS THROUGH TO GENERIC DIALOGUE"
// structural rather than a convention someone has to remember: a table that could resolve to no
// rule at all is a silent failure, and this is the cheapest place to make it impossible.

// What choosing an option does. Appending a kind is never a save-format change - none of this is
// serialized. Every action goes through the CHECKED quest API (Quest_CheckStart then Quest_Start,
// and so on), so a refusal is an outcome code, never an assert: an assert on a path the agent
// test loop walks would hang it.
typedef enum RsDialogueActionKind {
    RS_DLG_ACTION_NONE = 0,           // just close (or show the reply and close)
    RS_DLG_ACTION_START_QUEST = 1,    // a = QuestId -> Quest_Start
    RS_DLG_ACTION_COMPLETE_QUEST = 2, // a = QuestId -> Quest_Complete (COMPLETE-before-rewards, D12,
                                      // so a second one is `already_complete` and grants nothing)
    RS_DLG_ACTION_SET_WORLD_FLAG = 3, // a = WorldFlagId -> Flags_SetWorldFlag
    RS_DLG_ACTION_KIND_COUNT,
} RsDialogueActionKind;

typedef struct RsDialogueOption {
    const char* label;         // the choice line; never NULL
    RsDialogueActionKind kind; // what picking it does
    int32_t a;                 // the action's operand
    const char* reply;         // shown after picking; NULL closes without a reply
} RsDialogueOption;

typedef struct RsDialogueRule {
    // Visible when EVERY predicate here is true. A count of 0 means unconditional - the list is
    // the conjunction, exactly as QuestDef.requirements and a journal block's `when` are, which is
    // why the vocabulary needs no `And`.
    const QuestPredicate* when;
    int32_t whenCount;

    const char* text; // the body; never NULL

    // D11's forward constraint (sturdy-bassoon#59) made concrete: THE MODEL IS N. `optionCount` is
    // an int and the option list is a list; nothing here is shaped like yes/no. What caps it is the
    // RENDERER, and that claim has now been paid out once: raising the cap from 3 to 4 changed the
    // engine and this line, and touched no definition written against this struct.
    //
    // Vanilla offers a two-way (CTRL_TWO_CHOICE) and a three-way (CTRL_THREE_CHOICE). The four-way
    // is ours: CTRL_FOUR_CHOICE plus TEXTBOX_ENDTYPE_4_CHOICE plus a fifth row of box, so a menu can
    // ask a question AND offer four answers. Past four the box runs out of screen, not out of model.
    const RsDialogueOption* options;
    int32_t optionCount; // 0 = a plain statement

    // THE MISSING-STEPS CLAUSE (D26, added in P4). -1 for none; otherwise a QuestId whose UNSET
    // steps are listed after `text`, on their own line, named through QuestDef.stepLabels.
    //
    // Why the model grew for this. A quest with three any-order steps has seven distinct
    // "still missing" states, and rule text is static data - so saying "you still need an egg and
    // a pot of flour" for each of them means SEVEN hand-written rules, which is the same 2^n
    // snapshot explosion D13 rejects for journal blocks. Predicates cannot help: they choose which
    // rule SPEAKS, not what it says. With this one field, ONE rule speaks all seven states and the
    // table stays O(1) in the step count.
    //
    // It stays introspectable, which is the D11 bargain: the clause is declared data, so
    // `npc dump` prints the live composed list beside each predicate's live value, and the whole
    // eight-state sweep is readable from a console without holding eight conversations.
    // Composition reads only the GLOBAL stores at render time, so the property that makes two
    // NPCs in talk range safe - the entry textbox's id carrying the speaker and the rule, with no
    // state anywhere - is untouched.
    //
    // THERE IS NO SAFE DEFAULT, so every rule states this field. A brace list that stops early
    // value-initialises it to 0 - and 0 is QUEST_COOKS_ASSISTANT, a real quest - so a forgotten
    // `-1` would quietly append the Cook's shopping list to somebody else's line. Two things make
    // that impossible to ship rather than merely discouraged: every rule row in the tree writes it
    // explicitly (so a copied row carries it), and registration REFUSES a rule whose `missingOf`
    // names a quest that no predicate in its own `when` list gates on. A clause listing what is
    // missing from a quest the rule does not condition on is meaningless anyway - it would say
    // "I still need everything" to a player who has not been offered the quest - so the check is a
    // real invariant that happens to catch the typo.
    //
    // Also refused on any rule WITH OPTIONS: a three- or four-way is hand-laid-out through Format(),
    // which does not paginate, so an appended list would run off the bottom of the box - visible
    // only in a screenshot, which is exactly the bug class P3 shipped and caught by eye.
    int32_t missingOf; // -1 = no clause; otherwise a QuestId
} RsDialogueRule;

typedef struct RsNpcDef {
    int32_t id;     // NpcId (NpcIds.h)
    QuestTier tier; // must equal NPC_ID_TIER(id); RsNpc_Register refuses otherwise
    const char* name;        // snake_case token for console lines and markers
    const char* displayName; // prose; what a surface calls this character
    const RsDialogueRule* rules;
    int32_t ruleCount; // [1, RS_DIALOGUE_MAX_RULES]; the LAST rule must be unconditional
} RsNpcDef;

#define RS_DIALOGUE_MAX_RULES 32
#define RS_DIALOGUE_MAX_OPTIONS 4

// The `missingOf` sentinel. Spelled out rather than written as a bare -1 in twenty rule rows.
#define RS_DLG_NO_MISSING (-1)

// --- text ids -----------------------------------------------------------------------------------
//
// The band is 0xA000..0xCFFF. SoH's own highest custom id is 0x9215
// (Enhancements/custom-message/CustomMessageTypes.h), and nothing in Message_OpenText's
// special-case ladder (z_message_PAL.c) touches this range - and `loadFromMessageTable = false`
// means Message_FindMessage never runs for one of ours anyway.
//
// THE ENTRY TEXTBOX'S ID CARRIES WHO IS SPEAKING AND WHICH RULE MATCHED, so rendering it needs no
// state at all. That matters concretely: `msgCtx->talkActor` is assigned AFTER Message_OpenText
// returns (z_message_PAL.c), so the OnOpenText hook cannot ask who is talking, and with two NPCs
// in talk range at once - which is exactly the two-placements case P3 has to prove - a "last actor
// to write a global wins" scheme would render the wrong character's line.
#define RS_TEXT_NPC_BASE 0xA000
#define RS_TEXT_RULE_SHIFT 5
#define RS_TEXT_NPC_ID(npcId, rule) ((uint16_t)(RS_TEXT_NPC_BASE + ((npcId) << RS_TEXT_RULE_SHIFT) + (rule)))
#define RS_TEXT_NPC_GET_ID(textId) ((int32_t)(((textId)-RS_TEXT_NPC_BASE) >> RS_TEXT_RULE_SHIFT))
#define RS_TEXT_NPC_GET_RULE(textId) ((int32_t)(((textId)-RS_TEXT_NPC_BASE) & (RS_DIALOGUE_MAX_RULES - 1)))
#define RS_TEXT_NPC_END (RS_TEXT_NPC_BASE + (NPC_MAX << RS_TEXT_RULE_SHIFT) - 1)

// One id for text an actor hands over directly: an option's reply, and an item pickup. Unlike the
// entry box this DOES read a one-slot pointer - but it is a parameter, not shared state: the
// pointer is set on the line above the Message_StartTextbox / Message_ContinueTextbox call, in the
// same statement sequence, with nothing running in between.
#define RS_TEXT_DIRECT 0xC000

RS_STATIC_ASSERT(RS_DIALOGUE_MAX_RULES == (1 << RS_TEXT_RULE_SHIFT),
                 "the rule field width and RS_DIALOGUE_MAX_RULES are the same number");
RS_STATIC_ASSERT(RS_TEXT_NPC_END < RS_TEXT_DIRECT,
                 "raising NPC_MAX must not push an NPC text id onto RS_TEXT_DIRECT");
// Four is the box, not the model. A choice puts one option per row and the body needs a row of its
// own, so five options need six rows; six rows is 96px, and the LOWER box position starts at y=142
// on a 240px screen. Raising this again means moving the box, not adding another control code.
RS_STATIC_ASSERT(RS_DIALOGUE_MAX_OPTIONS <= 4, "the four-way is the widest choice the textbox renders");

#endif // SOH_RS_NPC_DIALOGUE_DEF_H
