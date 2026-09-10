#include "FloorText.h"

#include <cstring>

namespace {

// The ordinal table. Ten entries because the US column of a 0..9 index reaches "tenth" - the UK
// column stops at "ninth" and spends its first row on "ground". Byte comparisons and a fixed table
// rather than any numeric-to-ordinal cleverness: the cap is the point.
const char* const kOrdinals[] = { "first", "second", "third",   "fourth", "fifth",
                                  "sixth", "seventh", "eighth", "ninth",  "tenth" };

constexpr int32_t kMaxStorey = 9;

const char kLowerName[] = "floor";
const char kUpperName[] = "Floor";
constexpr size_t kNameLen = sizeof(kLowerName) - 1;

bool IsDigit(char c) {
    // Byte comparison, never <cctype>. On MSVC `char` is signed, so a UTF-8 continuation byte is
    // negative, and isdigit() on a negative value trips the Debug CRT's own assert - the same trap
    // QuestJournal.cpp's IsBadChar records, in prose that will carry UTF-8 the moment an author
    // types a curly apostrophe.
    return c >= '0' && c <= '9';
}

RsFloorTokenResult Ok() {
    return { RS_FLOOR_TOKEN_OK, 0 };
}

RsFloorTokenResult Fail(RsFloorTokenError error, size_t pos) {
    return { error, static_cast<int32_t>(pos) };
}

// The diagnostic a render path shows instead of the raw token. Never the prose itself.
std::string ErrorLine(const RsFloorTokenResult& result) {
    std::string message = "<floor token error: ";
    message += RsFloorText_ErrorName(result.error);
    message += " at ";
    message += std::to_string(result.pos);
    message += ">";
    return message;
}

// The scan behind RsFloorText_Validate. File-local: the journal's parser drives ScanToken from its
// own single pass instead, so nothing outside this file has ever wanted a ranged scan.
RsFloorTokenResult ValidateRange(const char* text, size_t begin, size_t end) {
    size_t i = begin;
    while (i < end) {
        if (text[i] == '}') {
            return Fail(RS_FLOOR_TOKEN_STRAY_CLOSE, i);
        }
        if (text[i] != '{') {
            i++;
            continue;
        }
        RsFloorToken token = {};
        const RsFloorTokenResult result = RsFloorText_ScanToken(text, i, &token);
        if (result.error != RS_FLOOR_TOKEN_OK) {
            return result;
        }
        // A token that closes past `end` is still a token: the caller's range is a slice of the
        // same string, and a scanner that stopped mid-token would report a spurious error rather
        // than the real one. ScanToken is bounded by the NUL, which is the string's real end.
        i += token.length;
    }
    return Ok();
}

} // namespace

RsFloorTokenResult RsFloorText_ScanToken(const char* text, size_t pos, RsFloorToken* out) {
    if (text == nullptr) {
        return Fail(RS_FLOOR_TOKEN_NULL_TEXT, 0);
    }
    const size_t len = std::strlen(text);
    if (pos >= len || text[pos] != '{') {
        // A caller only reaches here standing on a '{', so this is the call being wrong rather than
        // the prose - and it says so, instead of reporting an `unclosed` brace that is not there.
        return Fail(RS_FLOOR_TOKEN_BAD_CALL, pos);
    }
    size_t close = pos + 1;
    while (close < len && text[close] != '}') {
        close++;
    }
    if (close >= len) {
        return Fail(RS_FLOOR_TOKEN_UNCLOSED, pos);
    }
    size_t colon = pos + 1;
    while (colon < close && text[colon] != ':') {
        colon++;
    }
    if (colon >= close) {
        return Fail(RS_FLOOR_TOKEN_MISSING_COLON, pos + 1);
    }
    const size_t nameLen = colon - (pos + 1);
    bool capitalised = false;
    if (nameLen == kNameLen && std::strncmp(text + pos + 1, kLowerName, kNameLen) == 0) {
        capitalised = false;
    } else if (nameLen == kNameLen && std::strncmp(text + pos + 1, kUpperName, kNameLen) == 0) {
        capitalised = true;
    } else {
        // Exact and untrimmed, the rule the journal's tag table already sets. A near-miss that
        // quietly became literal prose would be the exact failure this grammar exists to prevent.
        return Fail(RS_FLOOR_TOKEN_UNKNOWN, pos + 1);
    }
    // EXACTLY one digit. `{floor:}` and `{floor:12}` are both BAD_INDEX, reported at the byte after
    // the colon so the offset points at where the index should have started.
    if (close - (colon + 1) != 1 || !IsDigit(text[colon + 1])) {
        return Fail(RS_FLOOR_TOKEN_BAD_INDEX, colon + 1);
    }
    if (out != nullptr) {
        out->storey = text[colon + 1] - '0';
        out->capitalised = capitalised;
        out->length = close - pos + 1;
    }
    return Ok();
}

RsFloorTokenResult RsFloorText_Validate(const char* text) {
    if (text == nullptr) {
        return Fail(RS_FLOOR_TOKEN_NULL_TEXT, 0);
    }
    return ValidateRange(text, 0, std::strlen(text));
}

std::string RsFloorText_Label(int32_t convention, int32_t storey, bool capitalised) {
    if (storey < 0 || storey > kMaxStorey) {
        return "";
    }
    std::string label;
    if (convention == RS_FLOOR_CONVENTION_UK) {
        // The whole of the difference, in one branch: UK counts the storey at ground level as the
        // GROUND floor and starts its ordinals one storey up.
        label = (storey == 0) ? "ground" : kOrdinals[storey - 1];
    } else if (convention == RS_FLOOR_CONVENTION_US) {
        label = kOrdinals[storey];
    } else {
        return "";
    }
    if (capitalised && !label.empty()) {
        label[0] = static_cast<char>(label[0] - 'a' + 'A'); // every entry is ASCII lowercase
    }
    label += " floor";
    return label;
}

RsFloorTokenResult RsFloorText_Expand(const char* text, int32_t convention, std::string* out) {
    if (out != nullptr) {
        out->clear();
    }
    if (text == nullptr) {
        return Fail(RS_FLOOR_TOKEN_NULL_TEXT, 0);
    }
    if (convention < 0 || convention >= RS_FLOOR_CONVENTION_COUNT) {
        // The CALL is wrong, not the prose - and BAD_CALL says exactly that. Reported rather than
        // asserted, because every path into this function is a render or a registration gate and
        // neither may hang the agent loop.
        return Fail(RS_FLOOR_TOKEN_BAD_CALL, 0);
    }
    const size_t len = std::strlen(text);
    std::string built;
    built.reserve(len);
    size_t i = 0;
    while (i < len) {
        if (text[i] == '}') {
            if (out != nullptr) {
                out->clear();
            }
            return Fail(RS_FLOOR_TOKEN_STRAY_CLOSE, i);
        }
        if (text[i] != '{') {
            built += text[i];
            i++;
            continue;
        }
        RsFloorToken token = {};
        const RsFloorTokenResult result = RsFloorText_ScanToken(text, i, &token);
        if (result.error != RS_FLOOR_TOKEN_OK) {
            if (out != nullptr) {
                out->clear();
            }
            return result;
        }
        built += RsFloorText_Label(convention, token.storey, token.capitalised);
        i += token.length;
    }
    if (out != nullptr) {
        *out = std::move(built);
    }
    return Ok();
}

std::string RsFloorText_ExpandUnder(const std::string& text, int32_t convention) {
    std::string out;
    const RsFloorTokenResult result = RsFloorText_Expand(text.c_str(), convention, &out);
    if (result.error != RS_FLOOR_TOKEN_OK) {
        return ErrorLine(result);
    }
    return out;
}

std::string RsFloorText_Compose(const std::string& text) {
    return RsFloorText_ExpandUnder(text, RsPrefs_GetFloorConvention());
}

const char* RsFloorText_ErrorName(RsFloorTokenError error) {
    switch (error) {
        case RS_FLOOR_TOKEN_OK:
            return "ok";
        case RS_FLOOR_TOKEN_UNCLOSED:
            return "unclosed";
        case RS_FLOOR_TOKEN_STRAY_CLOSE:
            return "stray_close";
        case RS_FLOOR_TOKEN_MISSING_COLON:
            return "missing_colon";
        case RS_FLOOR_TOKEN_UNKNOWN:
            return "unknown_token";
        case RS_FLOOR_TOKEN_BAD_INDEX:
            return "bad_index";
        case RS_FLOOR_TOKEN_NULL_TEXT:
            return "null_text";
        case RS_FLOOR_TOKEN_BAD_CALL:
            return "bad_call";
        default:
            return "<bad error>";
    }
}
