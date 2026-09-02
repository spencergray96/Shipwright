// The journal fixture quest (sturdy-bassoon#58 P2 / #62) and the malformed-definition table that
// proves the registration gate refuses bad markup.
//
// QUEST_DEBUG_JOURNAL (50) is deliberately Cook's-Assistant-shaped - three any-order steps - so
// the block model is exercised against the exact quest P4 has to support, two phases before P4
// has to build it. Its five blocks are the D14 proof: accumulation and disappearance both fall
// out of the predicates, and there is no `replaces` mechanism anywhere for them to fall out of.
//
// Read the `when` column as the whole feature:
//
//   block 0  started (i.e. NOT not_started)         persists forever once started
//   block 1  in_progress AND NOT all steps set      vanishes on its own when the third item lands
//   block 2  in_progress AND all steps set          appears without replacing anything
//   block 3  complete                               the permanent epilogue
//   block 4  (no `when`)                            always visible; proves whenCount == 0
//
// Nothing tells block 1 to go away, and block 2 does not push it out. Both gates are evaluated
// fresh on every read, and they happen to be complementary because they were written that way.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "soh/Enhancements/rs/quest/Quest.h"
#include "soh/Enhancements/rs/quest/QuestDef.h"
#include "soh/Enhancements/rs/quest/QuestJournalDef.h"
#include "soh/Enhancements/rs/quest/QuestPredicate.h"
#include "soh/Enhancements/rs/quest/WorldFlagIds.h"
#include "soh/ShipInit.hpp"

namespace {

// The counts in a QuestDef are int32_t; this keeps every one of them derived from its array
// rather than typed in twice. (ARRAY_COUNT would work but drags z64.h into a file that otherwise
// needs nothing from the engine.)
template <typename T, size_t N> constexpr int32_t Count(const T (&)[N]) {
    return static_cast<int32_t>(N);
}

const char* const sStepNames[] = { "egg", "flour", "milk" };

const QuestReward sRewards[] = {
    { QUEST_REWARD_WORLD_FLAG, WORLD_FLAG_DEBUG_JOURNAL },
    { QUEST_REWARD_RUPEES, 30 },
};

// `hints` (D9) - shown before the quest starts, never evaluated. Markup applies here too, and
// this one carries a `#hint:...#` SPAN inside a hint STRING on purpose: the two senses of the
// word are different scopes of the same idea, and having both on one line makes the overload
// concrete rather than theoretical (pinned in CONTEXT.md).
const char* const sHints[] = {
    "Ask the #npc:Cook# in #place:Lumbridge Castle# - he only frets #hint:before a feast#.",
};

// --- the block list ------------------------------------------------------------------------------
//
// Every `when` is a LIST, and the list is the conjunction - that is why the vocabulary needs no
// `And` word. `Not(AllStepsSet(50))` is the gate that cannot be written with the original five
// (see the note in QuestPredicate.h); it is why P2 grew the vocabulary by exactly one word.

const QuestPredicate sWhenStarted[] = {
    QP_NOT_QUEST_STATUS_IS(QUEST_DEBUG_JOURNAL, QUEST_STATUS_NOT_STARTED),
};
const QuestPredicate sWhenCollecting[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_JOURNAL, QUEST_STATUS_IN_PROGRESS),
    QP_NOT_ALL_STEPS_SET(QUEST_DEBUG_JOURNAL),
};
const QuestPredicate sWhenReady[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_JOURNAL, QUEST_STATUS_IN_PROGRESS),
    QP_ALL_STEPS_SET(QUEST_DEBUG_JOURNAL),
};
const QuestPredicate sWhenComplete[] = {
    QP_QUEST_STATUS_IS(QUEST_DEBUG_JOURNAL, QUEST_STATUS_COMPLETE),
};

// The checklist. Each row names the step that strikes it through - a real reference, unlike the
// markup tags around it, which are annotations and resolve to nothing at all.
const QuestJournalItem sIngredients[] = {
    { "#item:an Egg#", 0 },
    { "#item:a bucket of Flour#", 1 },
    { "#item:a bucket of Milk#", 2 },
};

const QuestJournalBlock sBlocks[] = {
    // 0 - the prologue. Gated only on "started", so it is still true at COMPLETE and never leaves.
    // It carries an `item`-free `hint` next to `npc` and `place` so one line shows three styles.
    { sWhenStarted, Count(sWhenStarted), QUEST_BLOCK_PARAGRAPH,
      "The #npc:Cook# at #place:Lumbridge Castle# asked me to fetch what he needs for a cake, and "
      "he wants it all #hint:before the Duke sits down to eat#.",
      nullptr, 0 },

    // 1 - the checklist, with its own lead-in. Goes false by itself once the third step lands.
    { sWhenCollecting, Count(sWhenCollecting), QUEST_BLOCK_CHECKLIST, "I still need to find:", sIngredients,
      Count(sIngredients) },

    // 2 - appears the moment block 1 disappears. Neither block knows the other exists.
    { sWhenReady, Count(sWhenReady), QUEST_BLOCK_PARAGRAPH,
      "I have #item:every ingredient#. I should take them back to the #npc:Cook#.", nullptr, 0 },

    // 3 - the epilogue, printed alongside block 0, which is still true.
    { sWhenComplete, Count(sWhenComplete), QUEST_BLOCK_PARAGRAPH,
      "The #npc:Cook# baked his cake, and now lets me into his #place:kitchen# whenever I like.", nullptr, 0 },

    // 4 - no `when` at all: the whenCount == 0 case, visible at every status.
    { nullptr, 0, QUEST_BLOCK_PARAGRAPH,
      "(debug fixture 50 - drive it with: quest start 50, quest setstep 50 0..2, quest journal 50)", nullptr, 0 },
};

const QuestDef sJournalQuest = {
    .id = QUEST_DEBUG_JOURNAL,
    .tier = QUEST_TIER_DEBUG,
    .name = "debug_journal",
    .title = "Debug: the #npc:Cook#'s ingredients",
    .ordered = 0,
    .stepCount = 3,
    .stepNames = sStepNames,
    .requirements = nullptr,
    .requirementCount = 0,
    .prereqFn = nullptr,
    .hints = sHints,
    .hintCount = Count(sHints),
    .rewards = sRewards,
    .rewardCount = Count(sRewards),
    .onComplete = nullptr,
    .journal = sBlocks,
    .journalCount = Count(sBlocks),
};

// --- the malformed table ---------------------------------------------------------------------
//
// One definition per way markup or a block can be wrong. These are NEVER registered: they exist
// so `quest badcheck` can run Quest_DefProblem over each and show the gate refusing it, WITHOUT
// tripping the assert Quest_Register keeps for the bug class.
//
// Between this and `quest parse`, both halves of the loudness claim are demonstrable from the
// console: `parse` proves the grammar refuses, `badcheck` proves the gate is actually wired to
// the grammar. The table also covers what the console cannot drive at all - `agenttest` reads
// commands from a line-oriented file, so a newline inside a string is only testable from a
// definition.

const char* const sBadStepNames[] = { "a", "b", "c" };
const char* const sBadHints[] = { "Talk to #npc:the Cook" };
const char* const sHashStepNames[] = { "a", "#b#", "c" };
const QuestJournalItem sOutOfRangeStep[] = { { "#item:x#", 7 } }; // step 7 of a 3-step quest
const QuestJournalItem sOkItem[] = { { "#item:x#", 0 } };
const QuestPredicate sBadWhen[] = { { QUEST_PRED_ALL_STEPS_SET, 999, 0, 0 } }; // quest id out of range

struct BadDef {
    const char* label;
    QuestDef def;
};

QuestDef BaseBad() {
    QuestDef def = {};
    def.id = QUEST_DEBUG_JOURNAL;
    def.tier = QUEST_TIER_DEBUG;
    def.name = "bad";
    def.title = "Bad";
    def.stepCount = Count(sBadStepNames);
    def.stepNames = sBadStepNames;
    return def;
}

// Built once on first use. `blocks` is reserved and never allowed to grow past that, because the
// QuestDefs hold raw pointers into it; `defs` may reallocate freely (a QuestDef is trivially
// copyable and its pointers point elsewhere), but it is reserved too so the pointers this file
// hands out stay put.
const std::vector<BadDef>& BadDefs() {
    static std::vector<QuestJournalBlock> blocks;
    static std::vector<BadDef> defs;
    if (!defs.empty()) {
        return defs;
    }
    blocks.reserve(32);
    defs.reserve(32);

    auto paragraph = [](const char* text) {
        QuestJournalBlock block = {};
        block.kind = QUEST_BLOCK_PARAGRAPH;
        block.text = text;
        return block;
    };
    auto add = [&](const char* label, const QuestJournalBlock& block) {
        blocks.push_back(block);
        QuestDef def = BaseBad();
        def.journal = &blocks.back();
        def.journalCount = 1;
        defs.push_back({ label, def });
    };
    auto addProse = [&](const char* label, const char* text) { add(label, paragraph(text)); };

    // One per markup error kind, inside a block's prose.
    addProse("unclosed", "I need an #item:Egg");
    addProse("stray_hash", "The cook wants 3 # things");
    addProse("missing_tag", "I need an #Egg#");             // the bare in-tree CustomMessage form
    addProse("unknown_tag", "I need an #thing:Egg#");
    addProse("wrong_case", "I need an #ITEM:Egg#");         // tags are exact and lowercase
    addProse("spaced_tag", "I need an #item :Egg#");        // and untrimmed
    addProse("empty_tag", "I need an #:Egg#");
    addProse("empty_text", "I need an #item:#");
    addProse("bad_char_pct", "The cake is 100% done");      // '%' - the vsnprintf hazard
    addProse("bad_char_quote", "He said \"bring an egg\""); // '"' - breaks the key="value" contract
    addProse("bad_char_nl", "Line one\nline two");          // undrivable from a line-based console
    addProse("para_no_text", nullptr);                      // a paragraph with no prose at all

    // The same sweep, in the other display strings and in a token.
    {
        QuestDef def = BaseBad();
        def.title = "Bad #item:";
        defs.push_back({ "title_markup", def });
    }
    {
        QuestDef def = BaseBad();
        def.hints = sBadHints;
        def.hintCount = Count(sBadHints);
        defs.push_back({ "hint_markup", def });
    }
    {
        QuestDef def = BaseBad();
        def.stepNames = sHashStepNames; // tokens refuse '#' outright - no markup lives in a token
        defs.push_back({ "token_hash", def });
    }

    // Structural block problems, which the markup parser cannot see.
    {
        QuestJournalBlock block = paragraph("fine");
        block.items = sOkItem;
        block.itemCount = Count(sOkItem);
        add("para_with_items", block);
    }
    {
        QuestJournalBlock block = {};
        block.kind = QUEST_BLOCK_CHECKLIST;
        block.text = "I still need:";
        block.items = sOutOfRangeStep;
        block.itemCount = Count(sOutOfRangeStep);
        add("item_step_range", block);
    }
    {
        QuestJournalBlock block = {};
        block.kind = QUEST_BLOCK_CHECKLIST;
        block.text = "I still need:";
        add("empty_checklist", block); // a checklist with no rows
    }
    {
        QuestJournalBlock block = {};
        block.kind = QUEST_BLOCK_CHECKLIST;
        block.text = "I still need:";
        block.items = sOkItem;
        block.itemCount = Count(sOkItem);
        block.when = sBadWhen;
        block.whenCount = Count(sBadWhen);
        add("when_operand", block);
    }
    return defs;
}

void RegisterJournalQuest() {
    Quest_Register(&sJournalQuest); // idempotent for the same pointer, so a ShipInit re-run is safe
}

RegisterShipInitFunc journalQuestInitFunc(RegisterJournalQuest);

} // namespace

// Consumed by QuestConsole's `badcheck`, declared there. Defined here so the malformed shapes sit
// next to the good definition they are contrasted with.
int32_t QuestDebug_BadDefCount() {
    return static_cast<int32_t>(BadDefs().size());
}

const char* QuestDebug_BadDefLabel(int32_t index) {
    const std::vector<BadDef>& defs = BadDefs();
    if (index < 0 || index >= static_cast<int32_t>(defs.size())) {
        return nullptr;
    }
    return defs[index].label;
}

const QuestDef* QuestDebug_BadDef(int32_t index) {
    const std::vector<BadDef>& defs = BadDefs();
    if (index < 0 || index >= static_cast<int32_t>(defs.size())) {
        return nullptr;
    }
    return &defs[index].def;
}
