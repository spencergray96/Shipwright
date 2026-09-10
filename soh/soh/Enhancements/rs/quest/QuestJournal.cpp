#include "QuestJournal.h"

#include <cstddef>
#include <cstring>

#include "Quest.h"
#include "QuestDef.h"
#include "QuestStore.h"
#include "soh/Enhancements/rs/prefs/FloorText.h"

namespace {

struct TagEntry {
    const char* tag;
    QuestRunStyle style;
};

// Exact, lowercase, untrimmed. `#ITEM:x#` and `#item :x#` are UNKNOWN_TAG on purpose: a
// near-miss that silently became plain prose would be the exact failure this parser exists to
// prevent, and a near-miss that was quietly accepted would teach authors the tag is fuzzy.
//
// `npc` and `place` are ALIASES of `hint` since P4's D23 checkpoint (QuestJournalDef.h says why).
// They are still accepted tags - only their STYLE collapsed - so no prose had to be rewritten and
// un-collapsing is re-pointing these two rows at their own enumerator.
const TagEntry kTags[] = {
    { "item", QUEST_RUN_ITEM },
    { "npc", QUEST_RUN_HINT },
    { "place", QUEST_RUN_HINT },
    { "hint", QUEST_RUN_HINT },
};

bool LookUpTag(const char* text, size_t begin, size_t end, QuestRunStyle* style) {
    const size_t len = end - begin;
    for (const TagEntry& entry : kTags) {
        if (std::strlen(entry.tag) == len && std::strncmp(text + begin, entry.tag, len) == 0) {
            *style = entry.style;
            return true;
        }
    }
    return false;
}

// Byte comparisons only - never <cctype>. On MSVC `char` is signed, so a UTF-8 continuation byte
// is negative, and isspace()/iscntrl() on a negative value trips the Debug CRT's own assert. Real
// prose will carry UTF-8 the moment someone writes an apostrophe in a word processor, and an
// assert there would hang the agent loop in a path no test can reach.
bool IsBadChar(char c) {
    return c == '%' || c == '"' || c == '\n' || c == '\r' || c == '\t';
}

QuestMarkupResult Ok() {
    return { QUEST_MARKUP_OK, 0 };
}

QuestMarkupResult Fail(QuestMarkupError error, size_t pos) {
    return { error, static_cast<int32_t>(pos) };
}

// One RsFloorTokenError, in this file's vocabulary. The two enums are deliberately separate - a
// journal caller asks one type one question - and this is the only place they meet.
QuestMarkupError MarkupErrorForToken(RsFloorTokenError error) {
    switch (error) {
        case RS_FLOOR_TOKEN_UNCLOSED:
            return QUEST_MARKUP_TOKEN_UNCLOSED;
        case RS_FLOOR_TOKEN_STRAY_CLOSE:
            return QUEST_MARKUP_TOKEN_STRAY_CLOSE;
        case RS_FLOOR_TOKEN_MISSING_COLON:
            return QUEST_MARKUP_TOKEN_MISSING_COLON;
        case RS_FLOOR_TOKEN_UNKNOWN:
            return QUEST_MARKUP_TOKEN_UNKNOWN;
        case RS_FLOOR_TOKEN_BAD_INDEX:
            return QUEST_MARKUP_TOKEN_BAD_INDEX;
        case RS_FLOOR_TOKEN_NULL_TEXT:
            return QUEST_MARKUP_NULL_TEXT;
        default:
            return QUEST_MARKUP_TOKEN_UNCLOSED; // unreachable: RS_FLOOR_TOKEN_OK never gets here
    }
}

// The part of the scan that is the same in plain prose and inside a `#tag:text#` span: a refused
// character, a stray '}', and a `{floor:N}` token. Sharing it is what keeps "first error wins" a
// statement about POSITION - a token error inside a span and a bad character after it are compared
// by offset, not by which rulebook ran first.
//
// Returns false with `*result` set on an error. Otherwise `*advance` is how many bytes this byte
// consumed: the token's length for a well-formed token, and 0 for an ordinary byte the caller must
// interpret itself (a '#', or plain text).
bool ScanCommon(const char* text, size_t i, size_t* advance, QuestMarkupResult* result) {
    *advance = 0;
    if (IsBadChar(text[i])) {
        *result = Fail(QUEST_MARKUP_BAD_CHAR, i);
        return false;
    }
    if (text[i] == '}') {
        *result = Fail(QUEST_MARKUP_TOKEN_STRAY_CLOSE, i);
        return false;
    }
    if (text[i] == '{') {
        RsFloorToken token = {};
        const RsFloorTokenResult scan = RsFloorText_ScanToken(text, i, &token);
        if (scan.error != RS_FLOOR_TOKEN_OK) {
            *result = Fail(MarkupErrorForToken(scan.error), static_cast<size_t>(scan.pos));
            return false;
        }
        *advance = token.length;
    }
    return true;
}

// One emitted run's text, with its `{floor:N}` tokens expanded against the live convention (#94).
// The scan has already proven every token in this range well-formed, so Compose cannot produce a
// diagnostic here - it is called for its expansion, not for its error handling.
std::string RunText(const char* text, size_t begin, size_t count) {
    return RsFloorText_Compose(std::string(text + begin, count));
}

// The single scan. `out` may be null (validate-only). Every error path returns before anything is
// pushed, so a caller that ignores the result still cannot end up with half a parse.
QuestMarkupResult Scan(const char* text, std::vector<QuestRun>* out) {
    if (text == nullptr) {
        return Fail(QUEST_MARKUP_NULL_TEXT, 0);
    }
    const size_t len = std::strlen(text);
    size_t plainBegin = 0;
    size_t i = 0;
    QuestMarkupResult problem = Ok();
    size_t advance = 0;
    while (i < len) {
        if (!ScanCommon(text, i, &advance, &problem)) {
            return problem;
        }
        if (advance > 0) {
            i += advance; // a well-formed token; it is part of whatever run it sits in
            continue;
        }
        if (text[i] != '#') {
            i++;
            continue;
        }
        // A span opens here. Find its close, refusing a bad character or a malformed token on the
        // way so that both are always reported at the offending byte rather than being masked by
        // the span's own shape - the scan order is what makes "first error wins" a statement about
        // POSITION.
        const size_t open = i;
        size_t close = open + 1;
        while (close < len && text[close] != '#') {
            if (!ScanCommon(text, close, &advance, &problem)) {
                return problem;
            }
            // A well-formed token can never contain a '#' (one inside the name is UNKNOWN, one
            // inside the index is BAD_INDEX), so stepping over it cannot step over this span's
            // close.
            close += (advance > 0) ? advance : 1;
        }
        if (close >= len) {
            return Fail(QUEST_MARKUP_UNCLOSED, open);
        }
        size_t colon = open + 1;
        while (colon < close && text[colon] != ':') {
            colon++;
        }
        if (colon >= close) {
            // `#Egg#`, and `##`. The bare form CustomMessage::EncodeColors uses, which is exactly
            // why it must be refused here rather than passed through.
            return Fail(QUEST_MARKUP_MISSING_TAG, open);
        }
        if (colon == open + 1) {
            return Fail(QUEST_MARKUP_EMPTY_TAG, open + 1);
        }
        QuestRunStyle style = QUEST_RUN_PLAIN;
        if (!LookUpTag(text, open + 1, colon, &style)) {
            return Fail(QUEST_MARKUP_UNKNOWN_TAG, open + 1);
        }
        if (colon + 1 == close) {
            return Fail(QUEST_MARKUP_EMPTY_TEXT, colon + 1);
        }
        if (out != nullptr) {
            if (open > plainBegin) {
                out->push_back({ RunText(text, plainBegin, open - plainBegin), QUEST_RUN_PLAIN });
            }
            out->push_back({ RunText(text, colon + 1, close - colon - 1), style });
        }
        i = close + 1;
        plainBegin = i;
    }
    if (out != nullptr && len > plainBegin) {
        out->push_back({ RunText(text, plainBegin, len - plainBegin), QUEST_RUN_PLAIN });
    }
    return Ok();
}

void PushErrorLine(std::vector<QuestRun>* runs, const QuestMarkupResult& result) {
    // Belt and braces. Quest_Register refuses a definition whose markup does not parse, so this
    // is unreachable for a registered quest - but if it ever is reached, the reader sees a
    // diagnostic, never the raw prose.
    std::string message = "<markup error: ";
    message += QuestMarkup_ErrorName(result.error);
    message += " at ";
    message += std::to_string(result.pos);
    message += ">";
    runs->push_back({ message, QUEST_RUN_PLAIN });
}

std::vector<QuestRun> ParseOrDiagnose(const char* text) {
    std::vector<QuestRun> runs;
    const QuestMarkupResult result = QuestMarkup_Parse(text, &runs);
    if (result.error != QUEST_MARKUP_OK) {
        runs.clear();
        PushErrorLine(&runs, result);
    }
    return runs;
}

bool BlockVisible(const QuestJournalBlock& block) {
    for (int32_t i = 0; i < block.whenCount; i++) {
        if (!QuestPredicate_Eval(&block.when[i])) {
            return false;
        }
    }
    return true; // whenCount == 0 means always visible
}

} // namespace

QuestMarkupResult QuestMarkup_Validate(const char* text) {
    return Scan(text, nullptr);
}

QuestMarkupResult QuestMarkup_Parse(const char* text, std::vector<QuestRun>* out) {
    if (out != nullptr) {
        out->clear();
    }
    const QuestMarkupResult result = Scan(text, out);
    if (result.error != QUEST_MARKUP_OK && out != nullptr) {
        out->clear();
    }
    return result;
}

const char* QuestMarkup_ErrorName(QuestMarkupError error) {
    switch (error) {
        case QUEST_MARKUP_OK:
            return "ok";
        case QUEST_MARKUP_UNCLOSED:
            return "unclosed";
        case QUEST_MARKUP_MISSING_TAG:
            return "missing_tag";
        case QUEST_MARKUP_UNKNOWN_TAG:
            return "unknown_tag";
        case QUEST_MARKUP_EMPTY_TAG:
            return "empty_tag";
        case QUEST_MARKUP_EMPTY_TEXT:
            return "empty_text";
        case QUEST_MARKUP_BAD_CHAR:
            return "bad_char";
        case QUEST_MARKUP_NULL_TEXT:
            return "null_text";
        // The token kinds (#94). Prefixed so a reader of `quest parse` output can tell at a glance
        // which grammar refused - `unclosed` is a span, `token_unclosed` is a `{floor:N}`.
        case QUEST_MARKUP_TOKEN_UNCLOSED:
            return "token_unclosed";
        case QUEST_MARKUP_TOKEN_STRAY_CLOSE:
            return "token_stray_close";
        case QUEST_MARKUP_TOKEN_MISSING_COLON:
            return "token_missing_colon";
        case QUEST_MARKUP_TOKEN_UNKNOWN:
            return "token_unknown";
        case QUEST_MARKUP_TOKEN_BAD_INDEX:
            return "token_bad_index";
        default:
            return "<bad error>";
    }
}

const char* QuestJournal_StyleName(QuestRunStyle style) {
    switch (style) {
        case QUEST_RUN_PLAIN:
            return "plain";
        case QUEST_RUN_ITEM:
            return "item";
        // One name per STYLE, not per tag: a `#npc:…#` span reports `hint`, because that is what
        // it now is. A surface that printed the tag back would be claiming a distinction the
        // renderer does not make (D23, collapsed in P4).
        case QUEST_RUN_HINT:
            return "hint";
        default:
            return "<bad style>";
    }
}

QuestRunEmphasis QuestJournal_StyleEmphasis(QuestRunStyle style) {
    switch (style) {
        case QUEST_RUN_ITEM:
            return QUEST_EMPHASIS_KEY;
        // This is the function P4's D23 checkpoint read. `npc` and `place` were still returning
        // GUIDE here alongside `hint`, so they were collapsed into it (QuestJournalDef.h). A style
        // that shares an emphasis with another style is a style that buys nothing.
        case QUEST_RUN_HINT:
            return QUEST_EMPHASIS_GUIDE;
        default:
            return QUEST_EMPHASIS_NONE;
    }
}

const char* QuestJournal_EmphasisName(QuestRunEmphasis emphasis) {
    switch (emphasis) {
        case QUEST_EMPHASIS_NONE:
            return "none";
        case QUEST_EMPHASIS_KEY:
            return "key";
        case QUEST_EMPHASIS_GUIDE:
            return "guide";
        default:
            return "<bad emphasis>";
    }
}

std::string QuestJournal_RenderInline(const std::vector<QuestRun>& runs) {
    std::string out;
    for (const QuestRun& run : runs) {
        if (run.style == QUEST_RUN_PLAIN) {
            out += run.text;
        } else {
            out += "[";
            out += QuestJournal_StyleName(run.style);
            out += ":";
            out += run.text;
            out += "]";
        }
    }
    return out;
}

bool QuestJournal_Build(int32_t questId, QuestJournalEntry* out) {
    if (out == nullptr) {
        return false;
    }
    const QuestDef* def = Quest_GetDef(questId); // no assert on a bad or unregistered id
    if (def == nullptr) {
        return false;
    }
    out->questId = questId;
    out->name = def->name;
    out->status = QuestStore_GetStatus(questId);
    out->title = ParseOrDiagnose(def->title);
    out->lines.clear();
    out->blockCount = def->journalCount;
    out->visibleCount = 0;
    for (int32_t b = 0; b < def->journalCount; b++) {
        const QuestJournalBlock& block = def->journal[b];
        if (!BlockVisible(block)) {
            continue;
        }
        out->visibleCount++;
        // A checklist's lead-in is optional (NULL); a paragraph's text never is, and validation
        // has already guaranteed that, so only the NULL case needs skipping here.
        if (block.text != nullptr) {
            QuestJournalLine line;
            line.kind = QUEST_LINE_PARAGRAPH;
            line.runs = ParseOrDiagnose(block.text);
            line.blockIndex = b;
            line.step = -1;
            line.checked = false;
            out->lines.push_back(std::move(line));
        }
        for (int32_t i = 0; i < block.itemCount; i++) {
            const QuestJournalItem& item = block.items[i];
            QuestJournalLine line;
            line.kind = QUEST_LINE_CHECK_ITEM;
            line.runs = ParseOrDiagnose(item.text);
            line.blockIndex = b;
            line.step = item.step;
            // Read through the store, not Quest_IsStepSet: the latter asserts on a step outside
            // stepCount, and this is a read path a console calls. Validation has already bounded
            // item.step, so the two agree - this just refuses to be the one that hangs.
            line.checked = item.step >= 0 && QuestStore_IsStepSet(questId, item.step) != 0;
            out->lines.push_back(std::move(line));
        }
    }
    return true;
}

std::vector<QuestJournalEntry> QuestJournal_Snapshot() {
    std::vector<QuestJournalEntry> entries;
    for (int32_t id = 0; id < QUEST_MAX; id++) {
        QuestJournalEntry entry;
        if (QuestJournal_Build(id, &entry)) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}
