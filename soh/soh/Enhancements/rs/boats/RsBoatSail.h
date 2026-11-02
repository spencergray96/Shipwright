#ifndef SOH_RS_BOAT_SAIL_H
#define SOH_RS_BOAT_SAIL_H

// The player's own pictures on the boats' sails (sturdy-bassoon#287). A sail is a piece of a boat's cloth that the
// generator gave real texture coordinates (a `look`'s `image` in rs_boat_<kind>_props.json): the raft's, the skiff's,
// the sloop's and the sloop's jib. Its picture is `sails/<name>.png` in the game's folder, beside `Save/`, shared by
// every save; a save keeps only whether each sail draws it (the `sail_image` and `jib_image` looks, RsBoatFit.h).
//
// THE FILE. Anything stb_image reads (PNG, JPG, BMP, TGA, whatever its name says), cropped to cover a square from its
// middle and resampled to RS_BOAT_SAIL_SIZE x RS_BOAT_SAIL_SIZE RGBA32. Its transparent pixels show the sail's colour
// (its pattern, or plain) through. Refused, with the reason said in a toast, the console and a marker: a file that
// does not decode, one over 16 MB, wider or taller than 8192 pixels, or under 8 a side. A sail with no file, or a
// refused one, draws its pattern exactly as before.
//
// WHEN IT IS READ. Every file at the first scene after boot; again on entering any scene, for each file whose size or
// time changed (RsBoatSail_Check); and on `boat sails reload`. Each read makes a new pixel buffer, so the renderer's
// texture cache (keyed by address) never shows the old picture; the old buffer is dropped from that cache and freed a
// few game frames later, once no display list being drawn can name it. A changed file that is refused keeps the last
// good picture for the session; a deleted one goes back to the pattern.
//
// IMPORT. RsBoatSail_Import converts a file from anywhere and writes the result as `sails/<name>.png`, so the folder
// is always the store and the original can go. The Boat Sails window's "Choose image..." (the system's file dialog)
// and a file dropped on the game window while a sail is armed for it (RsBoatSail_Arm) both import; a drop with no
// sail armed is left to SoH's own handlers. A successful import sets that sail's image look to custom.
//
// TEMPLATES. Each boat's archive carries its sails' outlines (`<list>.template.<name>.png`, generate_props.py). They
// are written to `sails/templates/<name>-template.png` when missing or different: paint inside the outline.
//
// Console: `boat sails ...` (RsBoatFitConsole.h). Markers: `rs_boat event=sail sail=<name> result=<...>` and
// `rs_boat event=sail_arm sail=<name|none>`.

#include <stddef.h>
#include <stdint.h>

// The square a picture is drawn in: generate_props.py's IMAGE_SIZE, which the cloth's texture coordinates fill.
#define RS_BOAT_SAIL_SIZE 256

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RsBoatSailId {
    RS_BOAT_SAIL_RAFT,
    RS_BOAT_SAIL_SKIFF,
    RS_BOAT_SAIL_SLOOP,     // the sloop's mainsail
    RS_BOAT_SAIL_SLOOP_JIB, // the sloop's jib
    RS_BOAT_SAIL_COUNT,
} RsBoatSailId;

// "sloop-jib": the file's stem, the archive's image name and the console's token; "?" out of range. And back, -1.
const char* RsBoatSail_Name(int32_t sail);
int32_t RsBoatSail_ByName(const char* name);
// The sail a kind's image look draws on (the sail's, the jib's), or -1. And a sail's kind and image look.
int32_t RsBoatSail_Of(int32_t kind, int32_t imageLook);
int32_t RsBoatSail_Kind(int32_t sail);
int32_t RsBoatSail_Look(int32_t sail);

// The sail's picture, RS_BOAT_SAIL_SIZE square RGBA32, or NULL: no file, or none accepted yet. Valid until the next
// RsBoatSail_Check, reload or import; a display list may name it for a few frames after (see WHEN IT IS READ).
const uint8_t* RsBoatSail_Pixels(int32_t sail);
// 1 when it has a picture to draw.
int32_t RsBoatSail_Accepted(int32_t sail);
// The picture the sail draws now: its image look, a `match` followed to the mainsail's, set to custom with that
// picture accepted; NULL for none. A matched jib draws the mainsail's picture on its own texture coordinates. What the
// boat, the window and the console all show.
const uint8_t* RsBoatSail_DrawnPixels(int32_t sail);
// The sail's pattern look, the colour under its picture (the sail's, or the jib's), or -1.
int32_t RsBoatSail_Pattern(int32_t sail);
// 1 when its file is on disk.
int32_t RsBoatSail_Present(int32_t sail);
// Why its file on disk is not drawn ("" when it is, or there is none). Kept with the last good picture when a changed
// file is refused.
const char* RsBoatSail_Refusal(int32_t sail);

// Re-reads each file whose size or time changed; reads them all the first time. Every scene entry calls it.
void RsBoatSail_Check(void);
// Re-reads every file now.
void RsBoatSail_Reload(void);
// Converts the image at `path` for the sail and writes it as its file; then sets its look to custom when the boat is
// owned. 1 on success; 0 with the reason in `why` (may be NULL), which is also reported.
int32_t RsBoatSail_Import(int32_t sail, const char* path, char* why, size_t len);

// The sail a dropped file is for, or -1 (a drop is then SoH's). Disarmed by the next drop, accepted or refused.
void RsBoatSail_Arm(int32_t sail);
int32_t RsBoatSail_Armed(void);
// What a drop does (FileDropMgr's handler): 1 when it was this file's (a sail was armed), 0 to pass it on.
int32_t RsBoatSail_HandleDrop(const char* path);

// What the sailmaker's dialogue does to a sail (RS_DLG_ACTION_BOAT_SAIL, dialogue/npcs/SailmakerNpc.cpp).
typedef enum RsBoatSailOp {
    RS_BOAT_SAIL_OP_ARM,    // the next dropped file is this sail's
    RS_BOAT_SAIL_OP_BROWSE, // the system's file dialog for this sail (disarms)
    RS_BOAT_SAIL_OP_OFF,    // its image look back to none, so it draws its colour
    RS_BOAT_SAIL_OP_DONE,   // disarm
    RS_BOAT_SAIL_OP_USE,    // its image look to custom, for a picture already in sails/ (disarms); refused, and said
                            // in a toast, when it has none accepted
    RS_BOAT_SAIL_OP_COUNT,
} RsBoatSailOp;

// 1 when it did it; 0 for a sail or op out of range, or a look the boat's owner may not set.
int32_t RsBoatSail_DialogueAction(int32_t sail, int32_t op);

// The archive path of the sail's template (`<list>.template.<name>.png`) in buf: 1, or 0 for a sail out of range or a
// buffer too small.
int32_t RsBoatSail_TemplateInArchive(int32_t sail, char* buf, size_t len);

// The sails folder's full path, and its templates folder's.
const char* RsBoatSail_Folder(void);
const char* RsBoatSail_TemplateFolder(void);

// Bumped each time any sail's picture changes, for a window that previews them.
uint32_t RsBoatSail_Generation(void);

#ifdef __cplusplus
}
#endif

#endif // SOH_RS_BOAT_SAIL_H
