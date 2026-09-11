#ifndef SOH_RS_FLOOR_TEXT_H
#define SOH_RS_FLOOR_TEXT_H

#include <stddef.h>
#include <stdint.h>
#include <string>

#include "RsPrefs.h"

// THE `{floor:N}` TOKEN (sturdy-bassoon#94): the one grammar every piece of `rs/` prose uses to
// name a storey, expanded at READ time against the save file's floor convention.
//
// THE INVARIANT, because it is the whole feature: A STOREY INDEX IS NEVER A STOREY LABEL, AND NO
// PROSE ANYWHERE IN `rs/` MAY CONTAIN A LITERAL STOREY LABEL. "first floor" typed into a dialogue
// body is wrong for half the players and wrong SILENTLY, because both readings are grammatical and
// neither side knows the other was meant.
//
//   storey index   UK label         US label
//   0              ground floor     first floor
//   1              first floor      second floor
//   2              second floor     third floor
//
// The index is deliberately the same number as the grid tool's authoring LEVEL, so an author
// writing about level 2 of a building types `2` and does not convert.
//
// WHY A TOKEN AND NOT A JOURNAL MARKUP TAG. `#floor:2#` looks like the natural fifth tag, and it
// cannot be: '#' is a colour control byte in CustomMessageManager, and dialogue registration
// refuses it outright (NpcDialogue.cpp). A '#'-delimited token could never appear in DIALOGUE, and
// dialogue is where most storey references are written. Braces are free in both grammars - the
// journal refuses '%', '"' and the control characters, dialogue refuses those plus '#' and '^' -
// so one syntax works in both.
//
// `{floor:N}` gives the lowercase label, `{Floor:N}` the capitalised one for the start of a
// sentence. N is EXACTLY ONE DIGIT, 0..9: nothing in this mod is a ten-storey building, and an
// unbounded ordinal table is a spelling exercise with no user.
//
// THERE IS NO ESCAPE FOR A LITERAL '{' OR '}', which is the same bargain QuestJournal.h strikes
// over '#': it makes a stray brace unambiguously an error instead of a guess, and a mistyped token
// can never quietly render as literal prose.
//
// C++ ONLY, no extern "C" shim. Every consumer is C++ (the two registration gates, the journal
// parser, the textbox renderer, the consoles); the one thing a C actor hands over directly - an
// option reply - goes through RsText_SetDirectCopy, which expands for it. An item's pickup line is
// composed in C++ by Quest_ComposePickupText.

enum RsFloorTokenError {
    RS_FLOOR_TOKEN_OK = 0,
    RS_FLOOR_TOKEN_UNCLOSED = 1,      // a '{' with no later '}'
    RS_FLOOR_TOKEN_STRAY_CLOSE = 2,   // a '}' that closes nothing. The stray-brace case.
    RS_FLOOR_TOKEN_MISSING_COLON = 3, // `{floor}` - no ':' in the token
    RS_FLOOR_TOKEN_UNKNOWN = 4,       // `{storey:1}`, and also `{FLOOR:1}` / `{ floor:1}` (exact match)
    RS_FLOOR_TOKEN_BAD_INDEX = 5,     // `{floor:}`, `{floor:x}`, `{floor:12}` - not exactly one digit
    RS_FLOOR_TOKEN_NULL_TEXT = 6,     // a NULL where prose was required
    // Not a fault in the PROSE - a fault in the CALL: a scan started somewhere other than a '{', or
    // a convention outside the enum. It has its own kind so a diagnostic can never lie about which
    // of the two went wrong; `unclosed at 0` for a bad convention argument would send a reader
    // hunting for a brace that was never there.
    RS_FLOOR_TOKEN_BAD_CALL = 7,
    RS_FLOOR_TOKEN_ERROR_COUNT,
};

// `pos` is the byte offset the scanner was standing on when it gave up, ABSOLUTE in the string
// handed in - which is what lets a caller scanning a substring report an offset a reader can count
// to in the definition. 0 when error == RS_FLOOR_TOKEN_OK.
struct RsFloorTokenResult {
    RsFloorTokenError error;
    int32_t pos;
};

// One well-formed token, as scanned.
struct RsFloorToken {
    int32_t storey;  // 0..9
    bool capitalised; // `{Floor:N}` rather than `{floor:N}`
    size_t length;    // bytes from the '{' through the '}', so a scanner can step over it
};

// Scans the token starting at `text[pos]`, which the caller has already established is a '{'.
// Never allocates, never logs, never asserts - it reports. This is the primitive the two
// registration gates share, and both of them must stay silent.
RsFloorTokenResult RsFloorText_ScanToken(const char* text, size_t pos, RsFloorToken* out);

// Every token in a whole NUL-terminated string. What a validator that is not also a markup parser
// calls; the journal's scanner drives RsFloorText_ScanToken itself, so that its own left-to-right
// pass stays the single one.
RsFloorTokenResult RsFloorText_Validate(const char* text);

// The label one convention gives one storey index. Empty for an index outside 0..9 or a convention
// outside the enum - callers reach this only through a validated token.
std::string RsFloorText_Label(int32_t convention, int32_t storey, bool capitalised);

// Validate-then-expand under an explicit convention. On ANY error `out` is left EMPTY - there is no
// literal-prose fallback anywhere in this file, which is what makes a malformed token impossible to
// miss.
RsFloorTokenResult RsFloorText_Expand(const char* text, int32_t convention, std::string* out);

// Expansion for a RENDER path: the same scan, but a malformed token yields `<floor token error:
// kind at N>` rather than an empty string. Unreachable for registered prose - both gates refuse a
// definition whose tokens do not scan - and kept because a diagnostic is the only acceptable
// fallback: rendering the raw token is the exact silent failure this grammar exists to prevent.
std::string RsFloorText_ExpandUnder(const std::string& text, int32_t convention);

// The same, under the LIVE setting. This is what every read-time composition calls, and it is why
// a save-slot switch changes what an NPC says with nothing telling it to.
std::string RsFloorText_Compose(const std::string& text);

const char* RsFloorText_ErrorName(RsFloorTokenError error); // "unclosed", "bad_index", ...

#endif // SOH_RS_FLOOR_TEXT_H
