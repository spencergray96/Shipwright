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
    // a = RS_DLG_ITEM(questId, step): the NPC hands Link that quest step's item (sturdy-bassoon#281).
    // Picking it only CHECKS the step (Quest_CheckSetStep); the step is SET when Link accepts the item,
    // after the conversation closes, exactly as a get-item quest item sets its own. So the order is
    // action check -> reply -> any journal notices -> the box closes -> the offer -> Player's get-item
    // ceremony and pickup box (Quest_ComposePickupText) -> the conversation re-opens on `next`, with no
    // A press, or ends if `next` is RS_DLG_NO_NEXT. A refused check gives nothing and the conversation
    // carries on as for any other refusal. RsNpc only: an interaction refuses it at registration.
    RS_DLG_ACTION_GIVE_ITEM = 4,
    // a = price, 1..QUEST_RUPEE_AMOUNT_MAX (sturdy-bassoon#281). Refused as QUEST_ERR_CANNOT_AFFORD when Link holds less
    // (Quest_RupeesAfterPending), and then nothing is taken. The price lives HERE, in the definition, so
    // `npc dump` prints it - Majora's Mask keeps a shop's prices in the message header, where no tool
    // sees them. Gate the option on RupeesAtLeast(price) so a player who cannot pay never sees it.
    RS_DLG_ACTION_CHARGE_RUPEES = 5,
    // a = RS_DLG_BUY(questId, step, price): a SALE in one pick (sturdy-bassoon#281) - GIVE_ITEM's hand-over,
    // paid for. Picking it checks both halves (the step can be set, Link can afford the price) and
    // takes nothing; the rupees go when Link ACCEPTS the item, in the same breath as the step is set.
    // So a sale is all or nothing: a hand-over withdrawn or lost to a scene change charges nothing,
    // and there is no screen between paying and receiving for the player to leave by. Refused as
    // QUEST_ERR_CANNOT_AFFORD when short; gate it on RupeesAtLeast(price) as well, so a player who
    // cannot pay never sees it. RsNpc only, like GIVE_ITEM.
    RS_DLG_ACTION_BUY_ITEM = 6,
    RS_DLG_ACTION_KIND_COUNT,
} RsDialogueActionKind;

// GIVE_ITEM's operand: a quest and one of its steps, packed as the pickup text id packs them, so one
// int names the item an RsQuestItem placement of the same (quest, step) would be. Eleven bits.
#define RS_DLG_ITEM(questId, step) ((int32_t)(((questId) << 5) | (step)))
#define RS_DLG_ITEM_GET_QUEST(a) ((int32_t)((a) >> 5))
#define RS_DLG_ITEM_GET_STEP(a) ((int32_t)((a)&31))

// BUY_ITEM's operand: the price above the item, so the item half reads back through the RS_DLG_ITEM
// getters. 15 bits of price over 11 of item is 26 bits: positive in an int32 for every legal price.
#define RS_DLG_ITEM_BITS 11
#define RS_DLG_BUY(questId, step, price) ((int32_t)(((price) << RS_DLG_ITEM_BITS) | RS_DLG_ITEM(questId, step)))
#define RS_DLG_BUY_GET_ITEM(a) ((int32_t)((a) & ((1 << RS_DLG_ITEM_BITS) - 1)))
#define RS_DLG_BUY_GET_PRICE(a) ((int32_t)((a) >> RS_DLG_ITEM_BITS))

typedef struct RsDialogueOption {
    const char* label;         // the choice line; never NULL
    RsDialogueActionKind kind; // what picking it does
    int32_t a;                 // the action's operand
    const char* reply;         // shown after picking; NULL closes without a reply

    // NAVIGATION (sturdy-bassoon#96 P1). RS_DLG_NO_NEXT closes the conversation; otherwise a NODE
    // index in [0, RsNpcDef.nodeCount), in THIS npc's own node array. Rules are entered by
    // MATCHING and nodes by FOLLOWING AN EDGE, so `next` deliberately cannot name a rule: the two
    // address spaces are disjoint exactly as their text-id bands are, and a mid-conversation
    // screen can therefore never win the entry match.
    //
    // Order of operations when an option is picked: ACTION FIRES -> `reply` BOX -> `next` NODE.
    // The action fires wherever navigation goes afterwards, and a long `reply` paginates itself,
    // so multi-page intermediate dialogue needs no new node kind.
    //
    // THERE IS NO SAFE DEFAULT, so every option row states this field - the same argument
    // RsDialogueRule.missingOf makes below. A brace list that stops early value-initialises it to
    // 0, and 0 is NODE 0, a real screen: a forgotten sentinel would silently teleport the player
    // into the middle of somebody's conversation rather than closing the box. Registration cannot
    // save you here either, because on an NPC that HAS nodes, 0 is in range.
    int32_t next;

    // GATING (sturdy-bassoon#96 P2). The option is offered only when EVERY predicate here is true;
    // a count of 0 means always offered. Same conjunction-is-the-list rule, and the same seven-word
    // vocabulary, as RsDialogueRule.when and QuestDef.requirements - no new grammar.
    //
    // Unlike `next` these two DO have a safe default (NULL/0 = ungated), which is why they sit
    // after it: an option row that says nothing about gating is an ungated option, and that is what
    // every row written before #96 meant.
    //
    // Gating can only ever HIDE. The declared count is still capped at RS_DIALOGUE_MAX_OPTIONS, and
    // the box never shows more than RS_DIALOGUE_WINDOW_OPTIONS of them (a longer list scrolls,
    // sturdy-bassoon#290), so every pixel-width check stands unchanged. What it CAN do is take a
    // screen down to one, which is a textbox OoT does not have - see the two-ungated rule in
    // NpcDialogue.cpp.
    const QuestPredicate* when;
    int32_t whenCount;

    // JOURNAL NOTICE, OPT-IN (sturdy-bassoon#281). RS_DLG_NOTICE shows the "New quest" / "Quest
    // updated" / "Quest complete" box this option's action would earn; 0 - the value-initialised
    // default, so every row that says nothing - shows none. A developer's choice per option, not a
    // player setting: notices on every step get tiresome, so a quest turns them on where they earn
    // their place. Only START_QUEST, COMPLETE_QUEST, GIVE_ITEM and BUY_ITEM can earn one;
    // registration refuses the flag on any other action, where it could never fire. The action's
    // own rules still apply when it is set: a sale for a quest not started shows nothing.
    int32_t notice;

    // WHO SPEAKS THE REPLY (sturdy-bassoon#281, a speaker per screen). RS_DLG_SPEAKER(npcId) hands the
    // camera, and Link's attention, to that character while the reply is open; 0 - the
    // value-initialised default, so every row that says nothing - is the SCREEN's speaker, so an
    // option names nobody unless the answer comes from someone other than whoever asked. Which
    // option the player picks therefore decides who answers. Refused on an option with no reply
    // (nothing would ever speak it) and on an interaction's option (RsSpeaker.h says why).
    int32_t replySpeaker;
} RsDialogueOption;

// RsDialogueOption.notice's "on". Spelled out so a row reads `..., nullptr, 0, RS_DLG_NOTICE }`.
#define RS_DLG_NOTICE 1

// A SPEAKER (sturdy-bassoon#281): RsDialogueRule.speaker and RsDialogueOption.replySpeaker. Stored as
// the NpcId PLUS ONE, so that 0 - what a brace list that stops early leaves behind - means "the
// default" rather than NPC_COOK, who is NpcId 0. The default is the conversation's OWNER on a screen
// (the character Link talked to) and the screen's own speaker on a reply. Naming the owner outright
// is allowed, and is how an alternating conversation reads best.
#define RS_DLG_SPEAKER(npcId) ((int32_t)((npcId) + 1))
#define RS_DLG_SPEAKER_DEFAULT 0
#define RS_DLG_SPEAKER_GET_ID(speaker) ((int32_t)((speaker)-1)) // -1 for the default

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

    // CONTINUE (sturdy-bassoon#96 follow-up). On a STATEMENT - a screen with no options - the node
    // the conversation continues to when the player dismisses it, instead of closing. It is what
    // lets "ask about the job -> hear what is still missing -> back to the menu" exist at all: the
    // missing-steps clause is only allowed on a statement, and a statement used to be a dead end.
    // It also lets an ENTRY rule be a statement that leads into a tree.
    //
    // Refused on a screen WITH options: a choice navigates through its options' own `next`, and a
    // second exit on the screen would be dead data that looks live.
    //
    // THERE IS NO SAFE DEFAULT, for the reason RsDialogueOption.next gives: a value-initialised 0
    // is node 0. Every rule row and node row in the tree states it.
    int32_t next; // RS_DLG_NO_NEXT = close when dismissed; otherwise a node index (see "node groups")

    // WHO SPEAKS THIS SCREEN (sturdy-bassoon#281). RS_DLG_SPEAKER(npcId), or 0 for the conversation's
    // owner - the character Link talked to, which is what every screen written before this meant. The
    // owner still runs the conversation: its rules, its tree, its gifts. The speaker only gets the
    // camera and Link's attention while this screen is open, through Player's talk and focus actors
    // (actors/RsSpeaker.h). It is NOT part of the screen's text id, so a screen's identity - which
    // choice memory and a scroll's re-layout key on - does not depend on who says it.
    //
    // SAFE TO APPEND: a row that stops at `next` value-initialises this to 0, the owner. The registry
    // checks the id is in range at registration and that it is registered once every character has
    // registered (RsNpc_CheckSpeakers); whether that character is PLACED in the scene is a runtime
    // fact, and a speaker who is not there leaves the owner speaking, with a marker that says so.
    int32_t speaker;
} RsDialogueRule;

// A NODE is the same shape as a rule - a body plus up to four options - reached only by an EDGE
// (an option's `next`, or a statement's), never by entry matching (sturdy-bassoon#96). The alias
// exists so a definition site reads as what it is; there is deliberately ONE struct and therefore
// one validator, one renderer and one composer.
//
// NODE GROUPS. A node's own `when` means what a rule's does, applied to a run of nodes instead of
// the whole table: an edge names a node; if that node's gate fails, the conversation falls through
// to the next node in the array, and so on, FIRST MATCH WINS, until an ungated node ends the group.
// Registration requires that ungated end, for D8's reason - a group that could resolve to nothing
// is a silent failure. An ungated node is a group of one, which is every node written before groups
// existed. This is how one "ask about the job" edge says different things before and during a
// quest, without spending an option per state on routing.
//
// A node's `missingOf` is checked against the PATHS into it, not only its own gate: it registers
// when its own gate names the quest, or when every path from an entry rule into it passes a gate on
// that quest (NpcDialogue.cpp). That is what lets an ungated "here is what is left" node sit behind
// a gated option.
typedef RsDialogueRule RsDialogueNode;

typedef struct RsNpcDef {
    int32_t id;     // NpcId (NpcIds.h)
    QuestTier tier; // must equal NPC_ID_TIER(id); RsNpc_Register refuses otherwise
    const char* name;        // snake_case token for console lines and markers
    const char* displayName; // prose; what a surface calls this character
    const RsDialogueRule* rules;
    int32_t ruleCount; // [1, RS_DIALOGUE_MAX_RULES]; the LAST rule must be unconditional

    // The NODE array (sturdy-bassoon#96 P1). A SECOND array rather than more rules, because the two
    // addressing models are genuinely different: a rule is chosen by evaluating gates, a node by
    // following an edge. Put both in one array and a mid-tree node can win the entry match, which
    // drops the player into the middle of a conversation on first talk - so the separation makes
    // that impossible by construction rather than by a numbering convention someone must remember.
    // It also keeps all 32 rule slots for entry gating, which is what a multi-quest NPC needs.
    //
    // SAFE TO APPEND, unlike RsDialogueOption.next: an RsNpcDef brace list that stops early
    // value-initialises these to NULL/0, which means "this character has no tree" - exactly what
    // every definition written before #96 meant.
    const RsDialogueNode* nodes;
    int32_t nodeCount; // [0, RS_DIALOGUE_MAX_NODES]; every node must be REACHABLE
} RsNpcDef;

#define RS_DIALOGUE_MAX_RULES 32

// TWO numbers since sturdy-bassoon#290, which used to be one. The MODEL caps a screen at 16 options;
// the BOX shows four of them at once - a body row plus four choice rows is all that fits above the
// letterbox - and a screen offering more SCROLLS: Down on the bottom row moves the list up by one,
// Up on the top row moves it down (RsActors.cpp). Only the window is a renderer fact; the cap is an
// array size, and raising it is a recompile plus the interaction reply band (InteractionIds.h).
#define RS_DIALOGUE_MAX_OPTIONS 16
#define RS_DIALOGUE_WINDOW_OPTIONS 4

// Symmetric with the rule cap, and raisable into the id band's tail later. Unlike the rule cap
// this one is cheap to raise: NO NODE ID IS BAKED INTO A SCENE FILE (only an NpcId is - see
// RsActorParams.h), so raising it is a recompile rather than a migration.
#define RS_DIALOGUE_MAX_NODES 32

// The `next` sentinel: this option ends the conversation. Spelled out rather than written as a
// bare -1, for the reason RS_DLG_NO_MISSING is.
#define RS_DLG_NO_NEXT (-1)

// The `missingOf` sentinel. Spelled out rather than written as a bare -1 in twenty rule rows.
#define RS_DLG_NO_MISSING (-1)

// --- text ids -----------------------------------------------------------------------------------
//
// The band is 0xA000..0xF4FF, in nine pieces: entry rules (0xA000..0xBFFF), the one direct-text id
// (0xC000), interaction screens (0xC100..0xC13F, #183 - defined in interactions/InteractionIds.h,
// which asserts it clear of its neighbours), staircase menus (0xC400..0xC403, #147; 0xC404..0xC7FF freed by #214 - defined in
// stairs/StairDef.h, which asserts it clear of its neighbours), journal notices (0xC600..0xC6FF, #281),
// quest-item pickups (0xC800..0xCFFF, #99), nodes (0xD000..0xEFFF, #96)
// and the reply-then-navigate ids (0xF000..0xF01F, #96). Above them, interaction replies
// (0xF100..0xF4FF, moved there by #290 - InteractionIds.h).
// SoH's own highest custom id is 0x9215
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

// One id for text an actor hands over directly: an option's reply, and an item whose params this
// build cannot honour. Unlike the entry box this DOES read a one-slot pointer - but it is a
// parameter, not shared state: the pointer is set on the line above the Message_StartTextbox /
// Message_ContinueTextbox call, in the same statement sequence, with nothing running in between.
#define RS_TEXT_DIRECT 0xC000

// --- the quest-item PICKUP band (sturdy-bassoon#99) ----------------------------------------------
//
//     0xC800 + (questId << 5) + step    64 quests x 32 steps  ->  0xC800..0xCFFF
//
// A pickup line does not use RS_TEXT_DIRECT, because of the get-item cutscene: the textbox is opened
// by PLAYER (z_player.c, func_8084DFF4) about a second after the item
// is accepted, from the GetItemEntry's own `textId` - so "set on the line above the call, with
// nothing running in between" is no longer true of it. The id carries the (quest, step) instead,
// exactly as the entry box's carries the NPC and the rule, and the OnOpenText hook composes the
// line from the definition when the box opens. Both pickup styles use it, so there is one path.
//
// The same id is how the get-item DRAW function finds its texture (RsQuestItem.c): Player hands a
// CustomDrawFunc its own copy of the GetItemEntry and nothing else, and `textId` is the one field
// of it that already means "which of our items this is".
#define RS_TEXT_ITEM_BASE 0xC800
#define RS_TEXT_ITEM_SHIFT 5
#define RS_TEXT_ITEM_ID(questId, step) ((uint16_t)(RS_TEXT_ITEM_BASE + ((questId) << RS_TEXT_ITEM_SHIFT) + (step)))
#define RS_TEXT_ITEM_GET_QUEST(textId) ((int32_t)(((textId)-RS_TEXT_ITEM_BASE) >> RS_TEXT_ITEM_SHIFT))
#define RS_TEXT_ITEM_GET_STEP(textId) ((int32_t)(((textId)-RS_TEXT_ITEM_BASE) & (QUEST_STEP_MAX - 1)))
#define RS_TEXT_ITEM_END (RS_TEXT_ITEM_BASE + (QUEST_MAX << RS_TEXT_ITEM_SHIFT) - 1)
#define RS_TEXT_IS_ITEM(textId) ((textId) >= RS_TEXT_ITEM_BASE && (textId) <= RS_TEXT_ITEM_END)

// --- the JOURNAL NOTICE band (sturdy-bassoon#281) ------------------------------------------------
//
//     0xC600 + (questId << 2) + kind    64 quests x 4 kinds (3 used)  ->  0xC600..0xC6FF
//
// A notice box carries its quest and kind in its id, as a pickup box does, and the OnOpenText hook
// composes it from the definition (Quest_ComposeJournalNotice). So neither speaker touches the
// direct-text slot for one - which an interaction never may (RsInteraction.c) - and dismissing it
// decodes as no screen of anyone's, which is what sends the speaker back to its way out to ask for
// the next one.
#define RS_TEXT_NOTICE_BASE 0xC600
#define RS_TEXT_NOTICE_SHIFT 2
#define RS_TEXT_NOTICE_ID(questId, kind) ((uint16_t)(RS_TEXT_NOTICE_BASE + ((questId) << RS_TEXT_NOTICE_SHIFT) + (kind)))
#define RS_TEXT_NOTICE_GET_QUEST(textId) ((int32_t)(((textId)-RS_TEXT_NOTICE_BASE) >> RS_TEXT_NOTICE_SHIFT))
#define RS_TEXT_NOTICE_GET_KIND(textId) ((int32_t)(((textId)-RS_TEXT_NOTICE_BASE) & ((1 << RS_TEXT_NOTICE_SHIFT) - 1)))
#define RS_TEXT_NOTICE_END (RS_TEXT_NOTICE_BASE + (QUEST_MAX << RS_TEXT_NOTICE_SHIFT) - 1)
#define RS_TEXT_IS_NOTICE(textId) ((textId) >= RS_TEXT_NOTICE_BASE && (textId) <= RS_TEXT_NOTICE_END)

// --- the NODE band (sturdy-bassoon#96 P1) -------------------------------------------------------
//
//     0xD000 + (npcId << 5) + node      256 NPCs x 32 nodes  ->  0xD000..0xEFFF
//
// Free for the same reasons the rule band is: SoH's own highest custom id is 0x9215
// (Enhancements/custom-message/CustomMessageTypes.h), nothing in Message_OpenText's special-case
// ladder is anywhere near here, `textId` is a u16 everywhere with no masking, and
// `loadFromMessageTable = false` means Message_FindMessage never runs for one of ours.
//
// IT EXISTS SO NAVIGATION CARRIES NO ACTOR STATE. RsNpc.h says outright that nothing in the actor
// struct survives a scene transition and nothing in it may need to; a "which node am I on" field
// would be the first real exception. It is not needed, because the OPEN TEXTBOX'S ID ALREADY SAYS
// which node you are on, exactly as the entry box's id says which rule matched - so the actor
// decodes it from the live `msgCtx.textId` instead of remembering it. Walking away and re-talking
// therefore restarts at entry resolution, for free, which is what both OoT and RS do anyway.
#define RS_TEXT_NODE_BASE 0xD000
#define RS_TEXT_NODE_SHIFT 5
#define RS_TEXT_NODE_ID(npcId, node) ((uint16_t)(RS_TEXT_NODE_BASE + ((npcId) << RS_TEXT_NODE_SHIFT) + (node)))
#define RS_TEXT_NODE_GET_ID(textId) ((int32_t)(((textId)-RS_TEXT_NODE_BASE) >> RS_TEXT_NODE_SHIFT))
#define RS_TEXT_NODE_GET_NODE(textId) ((int32_t)(((textId)-RS_TEXT_NODE_BASE) & (RS_DIALOGUE_MAX_NODES - 1)))
#define RS_TEXT_NODE_END (RS_TEXT_NODE_BASE + (NPC_MAX << RS_TEXT_NODE_SHIFT) - 1)

// --- a REPLY that continues to a node ------------------------------------------------------------
//
// The one place the "the open box's id says where you are" argument needed a second band. An
// option can carry BOTH a reply and a `next`, and the reply box is a DIRECT-text box - so while it
// is open, `msgCtx.textId` would be RS_TEXT_DIRECT and the pending destination would have nowhere
// to live but the actor struct. Thirty-two ids in the band's tail buy it back: this box renders
// exactly like RS_TEXT_DIRECT and its id names the node to continue to when it is dismissed.
//
// No npc id is packed in, and it does not need one: only the actor that opened the box is in its
// Talk state (Actor_ProcessTalkRequest hands the request to exactly one actor), and it already
// knows which character it is. That is the same argument the one-slot direct-text pointer makes.
#define RS_TEXT_REPLY_TO_NODE_BASE 0xF000
#define RS_TEXT_REPLY_TO_NODE(node) ((uint16_t)(RS_TEXT_REPLY_TO_NODE_BASE + (node)))
#define RS_TEXT_REPLY_TO_NODE_GET(textId) ((int32_t)((textId)-RS_TEXT_REPLY_TO_NODE_BASE))
#define RS_TEXT_REPLY_TO_NODE_END (RS_TEXT_REPLY_TO_NODE_BASE + RS_DIALOGUE_MAX_NODES - 1)

// Which KIND of screen a text id names. A "screen" is a body plus up to four options; a rule and a
// node are the same shape and differ only in how you arrive at one.
typedef enum RsScreenKind {
    RS_SCREEN_NONE = 0, // not one of ours
    RS_SCREEN_RULE = 1,
    RS_SCREEN_NODE = 2,
} RsScreenKind;

RS_STATIC_ASSERT(RS_DIALOGUE_MAX_RULES == (1 << RS_TEXT_RULE_SHIFT),
                 "the rule field width and RS_DIALOGUE_MAX_RULES are the same number");
RS_STATIC_ASSERT(RS_DIALOGUE_MAX_NODES == (1 << RS_TEXT_NODE_SHIFT),
                 "the node field width and RS_DIALOGUE_MAX_NODES are the same number");
RS_STATIC_ASSERT(RS_TEXT_NPC_END < RS_TEXT_DIRECT,
                 "raising NPC_MAX must not push an NPC text id onto RS_TEXT_DIRECT");
RS_STATIC_ASSERT(RS_TEXT_DIRECT < RS_TEXT_NODE_BASE, "the node band must start above RS_TEXT_DIRECT");
RS_STATIC_ASSERT(QUEST_STEP_MAX == 32, "RS_DLG_ITEM's step field is five bits");
RS_STATIC_ASSERT(QUEST_MAX << 5 == 1 << RS_DLG_ITEM_BITS, "RS_DLG_ITEM fills exactly RS_DLG_ITEM_BITS bits");
RS_STATIC_ASSERT(QUEST_STEP_MAX == (1 << RS_TEXT_ITEM_SHIFT),
                 "the item text id's step field width and QUEST_STEP_MAX are the same number");
RS_STATIC_ASSERT(RS_TEXT_DIRECT < RS_TEXT_ITEM_BASE, "the item pickup band must start above RS_TEXT_DIRECT");
RS_STATIC_ASSERT(RS_TEXT_NOTICE_END < RS_TEXT_ITEM_BASE,
                 "raising QUEST_MAX must not push a journal notice id onto the item pickup band");
RS_STATIC_ASSERT(RS_TEXT_ITEM_END < RS_TEXT_NODE_BASE,
                 "raising QUEST_MAX must not push an item pickup text id onto the node band");
RS_STATIC_ASSERT(RS_TEXT_NODE_END < RS_TEXT_REPLY_TO_NODE_BASE,
                 "raising NPC_MAX must not push a node text id onto the reply-to-node band");
// 0xFFFF and 0xFFFD are message-table terminators (z_message_PAL.c) and must stay clear.
RS_STATIC_ASSERT(RS_TEXT_REPLY_TO_NODE_END < 0xFFFD, "the reply-to-node band must stay clear of the table terminators");
// Four is the box, not the model. A choice puts one option per row and the body needs a row of its
// own, so five rows of options need six rows; six rows is 96px, and the LOWER box position starts at
// y=142 on a 240px screen. More options than four scroll through this window (#290) instead.
RS_STATIC_ASSERT(RS_DIALOGUE_WINDOW_OPTIONS == 4, "the four-way is the widest choice the textbox renders");
RS_STATIC_ASSERT(RS_DIALOGUE_MAX_OPTIONS >= RS_DIALOGUE_WINDOW_OPTIONS, "a screen holds at least what the box shows");

#endif // SOH_RS_NPC_DIALOGUE_DEF_H
