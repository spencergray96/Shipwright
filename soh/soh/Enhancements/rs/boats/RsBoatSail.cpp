// The player's sail pictures (sturdy-bassoon#287): RsBoatSail.h says what a sail is, where its file lives, when it is
// read and what is refused. The boat draws them (RsBoat_RecolourList); the Boat Sails window and `boat sails` drive
// them.

#include "RsBoatSail.h"
#include "RsBoatFit.h"
#include "RsBoatSailDialog.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include <stb_image.h>
#include <spdlog/spdlog.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <ship/window/FileDropMgr.h>
#include <ship/window/Window.h>
#include <ship/window/gui/ConsoleWindow.h>
#include <ship/window/gui/Gui.h>

#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/rs/actors/RsActors.h"
#include "soh/Enhancements/rs/actors/RsBoat.h"
#include "soh/Notification/Notification.h"
#include "soh/ShipInit.hpp"

namespace fs = std::filesystem;

// OTRGlobals.h declares it for C callers only.
extern "C" void Gfx_TextureCacheDelete(const uint8_t* addr);

// SoH's own drop handlers, registered before ShipInit runs (OTRGlobals.cpp, randomizer.cpp); declared in no header.
bool SoH_HandleConfigDrop(char* filePath);
bool Rando_HandleSpoilerDrop(char* filePath);

namespace {

// A path from a string that is UTF-8 (the file dialog's answer) or in the system's code page (a dropped file's,
// DragQueryFileA; the app folder). Converting bytes that are not UTF-8 as UTF-8 throws on MSVC, so that is tried
// first and the code page is the fallback, never an exception out of a drop handler.
fs::path PathOf(const std::string& s) {
    try {
        return fs::path(std::u8string(s.begin(), s.end()));
    } catch (const std::exception&) {
        return fs::path(s);
    }
}

// The one that exists, when the two readings differ: a UTF-8 name and a code-page one can both be valid UTF-8.
fs::path Existing(const std::string& s) {
    std::error_code ec;
    const fs::path asUtf8 = PathOf(s);
    return fs::exists(asUtf8, ec) ? asUtf8 : fs::path(s);
}

// A path as UTF-8 text, for messages: .string() throws for a name the code page cannot hold.
std::string Text(const fs::path& p) {
    const std::u8string u = p.u8string();
    return std::string(u.begin(), u.end());
}

struct SailInfo {
    const char* name; // generate_props' image name in the boat lists, and the file's stem
    int32_t kind;     // RsBoatKindId
    int32_t look;     // its image look
    int32_t pattern;  // its pattern look, the colour under the picture
};

const SailInfo kSails[RS_BOAT_SAIL_COUNT] = {
    { "raft", RS_BOAT_KIND_RAFT, RS_BOAT_LOOK_SAIL_IMAGE, RS_BOAT_LOOK_SAIL_PATTERN },
    { "skiff", RS_BOAT_KIND_SKIFF, RS_BOAT_LOOK_SAIL_IMAGE, RS_BOAT_LOOK_SAIL_PATTERN },
    { "sloop", RS_BOAT_KIND_SLOOP, RS_BOAT_LOOK_SAIL_IMAGE, RS_BOAT_LOOK_SAIL_PATTERN },
    { "sloop-jib", RS_BOAT_KIND_SLOOP, RS_BOAT_LOOK_JIB_IMAGE, RS_BOAT_LOOK_JIB_PATTERN },
};

// What is refused (RsBoatSail.h, THE FILE).
constexpr size_t kMaxFileMb = 16;
constexpr size_t kMaxFileBytes = kMaxFileMb * 1024 * 1024;
constexpr int kMaxSide = 8192;
constexpr int kMinSide = 8;
// Game frames a replaced picture outlives its last draw: SoH replays one tick's display list for every interpolated
// frame, so a few ticks is past any list that could still name it.
constexpr int kRetireFrames = 4;

using Pixels = std::vector<uint8_t>;

struct Sail {
    std::unique_ptr<Pixels> pixels;   // RS_BOAT_SAIL_SIZE square RGBA32, or null
    std::unique_ptr<Pixels> mirrored; // the same mirrored left to right, made when a matched sail first draws it
    std::string refusal;            // why the file on disk is not drawn
    bool present = false;           // a file is on disk
    fs::file_time_type time{};
    uintmax_t size = 0;
};

struct Retired {
    std::unique_ptr<Pixels> pixels;
    int framesLeft;
};

std::array<Sail, RS_BOAT_SAIL_COUNT> sSails;
std::vector<Retired> sRetired;
bool sReadOnce = false;
int32_t sArmed = -1;
uint32_t sGeneration = 0;
std::string sFolder;
std::string sTemplateFolder;

bool SailValid(int32_t sail) {
    return sail >= 0 && sail < RS_BOAT_SAIL_COUNT;
}

fs::path FilePath(int32_t sail) {
    return PathOf(RsBoatSail_Folder()) / (std::string(kSails[sail].name) + ".png");
}

// `source` names the file a refusal is about when it is not the sail's own (an import's pick), for the toast.
void Report(int32_t sail, const char* result, const std::string& why, const std::string& source = "") {
    char line[512];
    std::snprintf(line, sizeof(line), "rs_boat event=sail sail=%s result=%s why=\"%s\"", RsBoatSail_Name(sail), result,
                  why.c_str());
    RsAgent_Marker(line);
    if (std::strcmp(result, "refused") == 0) {
        const std::string file =
            source.empty() ? std::string("sails/") + RsBoatSail_Name(sail) + ".png"
                           : source + " for the " + RsBoatSail_Name(sail) + " sail";
        SPDLOG_WARN("[rs_boat] {} refused: {}", file, why);
        auto console = std::dynamic_pointer_cast<Ship::ConsoleWindow>(
            Ship::Context::GetRawInstance()->GetWindow()->GetGui()->GetGuiWindow("Console"));
        if (console != nullptr) {
            console->SendErrorMessage(file + " refused: " + why);
        }
        Notification::Emit({ .prefix = file,
                             .message = "refused: " + why,
                             .messageColor = ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                             .remainingTime = 8.0f });
    } else {
        SPDLOG_INFO("[rs_boat] sails/{}.png {}{}{}", RsBoatSail_Name(sail), result, why.empty() ? "" : ": ", why);
    }
}

// The picture as the boat draws it: cropped to a square from its middle, then resampled to RS_BOAT_SAIL_SIZE -
// averaged over each texel's footprint when shrinking, bilinear when growing - in premultiplied alpha, so a
// transparent pixel's colour never bleeds into its neighbours. Refuses as RsBoatSail.h's THE FILE says.
std::string TooBig(uintmax_t bytes) {
    return "the file is " + std::to_string(bytes / (1024 * 1024)) + " MB; the most is " + std::to_string(kMaxFileMb) +
           " MB";
}

bool Convert(const std::vector<uint8_t>& bytes, Pixels& out, std::string& why) {
    if (bytes.size() > kMaxFileBytes) {
        why = TooBig(bytes.size());
        return false;
    }
    int w = 0, h = 0, n = 0;
    if (bytes.empty() || !stbi_info_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n)) {
        why = std::string("not an image the game reads (") + (bytes.empty() ? "empty" : stbi_failure_reason()) + ")";
        return false;
    }
    if (w > kMaxSide || h > kMaxSide) {
        why = std::to_string(w) + " x " + std::to_string(h) + " is wider or taller than " + std::to_string(kMaxSide) +
              " pixels";
        return false;
    }
    if (w < kMinSide || h < kMinSide) {
        why = std::to_string(w) + " x " + std::to_string(h) + " is under " + std::to_string(kMinSide) +
              " pixels a side";
        return false;
    }
    stbi_uc* src = stbi_load_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
    if (src == nullptr) {
        why = std::string("not an image the game reads (") + stbi_failure_reason() + ")";
        return false;
    }
    const int side = std::min(w, h);
    const int x0 = (w - side) / 2, y0 = (h - side) / 2;
    const int size = RS_BOAT_SAIL_SIZE;
    out.assign((size_t)size * size * 4, 0);
    auto put = [&](int dx, int dy, double r, double g, double b, double a) {
        uint8_t* p = &out[((size_t)dy * size + dx) * 4];
        if (a <= 0.0) {
            p[0] = p[1] = p[2] = p[3] = 0;
            return;
        }
        p[0] = (uint8_t)std::lround(std::min(255.0, r / a));
        p[1] = (uint8_t)std::lround(std::min(255.0, g / a));
        p[2] = (uint8_t)std::lround(std::min(255.0, b / a));
        p[3] = (uint8_t)std::lround(std::min(255.0, a));
    };
    auto at = [&](int x, int y, int c) { return (double)src[((size_t)y * w + x) * 4 + c]; };
    for (int dy = 0; dy < size; dy++) {
        for (int dx = 0; dx < size; dx++) {
            double r = 0, g = 0, b = 0, a = 0;
            if (side >= size) {
                const int sx0 = x0 + dx * side / size, sx1 = std::max(sx0 + 1, x0 + (dx + 1) * side / size);
                const int sy0 = y0 + dy * side / size, sy1 = std::max(sy0 + 1, y0 + (dy + 1) * side / size);
                for (int y = sy0; y < sy1; y++) {
                    for (int x = sx0; x < sx1; x++) {
                        const double pa = at(x, y, 3);
                        r += at(x, y, 0) * pa, g += at(x, y, 1) * pa, b += at(x, y, 2) * pa, a += pa;
                    }
                }
                const double count = (double)(sx1 - sx0) * (sy1 - sy0);
                put(dx, dy, r / count, g / count, b / count, a / count);
                continue;
            }
            const double u = std::clamp(x0 + (dx + 0.5) * side / size - 0.5, (double)x0, (double)(x0 + side - 1));
            const double v = std::clamp(y0 + (dy + 0.5) * side / size - 0.5, (double)y0, (double)(y0 + side - 1));
            const int ux = (int)u, vy = (int)v;
            const int ux1 = std::min(ux + 1, x0 + side - 1), vy1 = std::min(vy + 1, y0 + side - 1);
            const double fx = u - ux, fy = v - vy;
            const int xs[4] = { ux, ux1, ux, ux1 }, ys[4] = { vy, vy, vy1, vy1 };
            const double ws[4] = { (1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy };
            for (int k = 0; k < 4; k++) {
                const double pa = at(xs[k], ys[k], 3) * ws[k];
                r += at(xs[k], ys[k], 0) * pa, g += at(xs[k], ys[k], 1) * pa, b += at(xs[k], ys[k], 2) * pa, a += pa;
            }
            put(dx, dy, r, g, b, a);
        }
    }
    stbi_image_free(src);
    // The renderer reads a texture address whose first bytes are "__OTR__" as an archive path (SETTIMG's
    // gfx_check_image_signature, and Gfx_TextureCacheDelete), so a picture that happens to begin with them is nudged
    // by one in its first byte.
    if (std::memcmp(out.data(), "__OTR__", 7) == 0) {
        out[0] ^= 1;
    }
    return true;
}

// --- PNG out: an RGBA picture as a PNG with stored (uncompressed) deflate blocks, which needs only the two checksums.
uint32_t Crc32(const uint8_t* data, size_t len, uint32_t crc = 0) {
    static uint32_t table[256];
    static bool built = false;
    if (!built) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        built = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

void Be32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(v >> 24), out.push_back(v >> 16), out.push_back(v >> 8), out.push_back(v);
}

void Chunk(std::vector<uint8_t>& out, const char* tag, const std::vector<uint8_t>& data) {
    Be32(out, (uint32_t)data.size());
    std::vector<uint8_t> body(tag, tag + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    Be32(out, Crc32(body.data(), body.size()));
}

std::vector<uint8_t> EncodePng(const Pixels& px, int size) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)size * (size * 4 + 1));
    for (int y = 0; y < size; y++) {
        raw.push_back(0); // filter: none
        raw.insert(raw.end(), px.begin() + (size_t)y * size * 4, px.begin() + (size_t)(y + 1) * size * 4);
    }
    std::vector<uint8_t> z = { 0x78, 0x01 };
    uint32_t a = 1, b = 0;
    for (uint8_t c : raw) {
        a = (a + c) % 65521, b = (b + a) % 65521;
    }
    for (size_t at = 0; at < raw.size() || at == 0; at += 65535) {
        const uint16_t len = (uint16_t)std::min<size_t>(65535, raw.size() - at);
        z.push_back(at + len >= raw.size() ? 1 : 0);
        z.push_back(len & 0xFF), z.push_back(len >> 8), z.push_back(~len & 0xFF), z.push_back((~len >> 8) & 0xFF);
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + len);
        if (raw.empty()) {
            break;
        }
    }
    Be32(z, (b << 16) | a);
    std::vector<uint8_t> out = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    std::vector<uint8_t> ihdr;
    Be32(ihdr, size), Be32(ihdr, size);
    ihdr.insert(ihdr.end(), { 8, 6, 0, 0, 0 }); // 8-bit RGBA
    Chunk(out, "IHDR", ihdr);
    Chunk(out, "IDAT", z);
    Chunk(out, "IEND", {});
    return out;
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out, std::string& why) {
    std::error_code ec;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec) {
        why = "the file cannot be read (" + ec.message() + ")";
        return false;
    }
    if (size > kMaxFileBytes) {
        why = TooBig(size);
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    out.assign((size_t)size, 0);
    if (!in || !in.read(reinterpret_cast<char*>(out.data()), (std::streamsize)size)) {
        why = "the file cannot be read";
        return false;
    }
    return true;
}

bool WriteFile(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size())) {
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmp, path, ec);
    return !ec;
}

// The picture leaves the boat's hands now, and memory a few frames later.
void Retire(int32_t sail) {
    for (std::unique_ptr<Pixels>* buffer : { &sSails[sail].pixels, &sSails[sail].mirrored }) {
        if (*buffer != nullptr) {
            sRetired.push_back({ std::move(*buffer), kRetireFrames });
            sGeneration++;
        }
    }
}

// The sail's picture mirrored left to right, made the first time it is asked for.
const uint8_t* Mirrored(int32_t sail) {
    Sail& s = sSails[sail];
    if (s.pixels == nullptr) {
        return nullptr;
    }
    if (s.mirrored == nullptr) {
        const int size = RS_BOAT_SAIL_SIZE;
        s.mirrored = std::make_unique<Pixels>(s.pixels->size());
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                std::memcpy(&(*s.mirrored)[((size_t)y * size + x) * 4],
                            &(*s.pixels)[((size_t)y * size + (size - 1 - x)) * 4], 4);
            }
        }
    }
    return s.mirrored->data();
}

void Take(int32_t sail, std::unique_ptr<Pixels> pixels) {
    Retire(sail);
    sSails[sail].pixels = std::move(pixels);
    sGeneration++;
}

void Stamp(int32_t sail) {
    std::error_code ec;
    const fs::path path = FilePath(sail);
    sSails[sail].present = fs::is_regular_file(path, ec);
    sSails[sail].time = sSails[sail].present ? fs::last_write_time(path, ec) : fs::file_time_type{};
    sSails[sail].size = sSails[sail].present ? fs::file_size(path, ec) : 0;
}

// One sail's file, as it is on disk now.
void Read(int32_t sail) {
    Sail& s = sSails[sail];
    const bool had = s.pixels != nullptr;
    Stamp(sail);
    if (!s.present) {
        if (had || !s.refusal.empty()) {
            Take(sail, nullptr);
            s.refusal.clear();
            Report(sail, "removed", "");
        }
        return;
    }
    std::vector<uint8_t> bytes;
    std::string why;
    auto pixels = std::make_unique<Pixels>();
    if (!ReadFile(FilePath(sail), bytes, why) || !Convert(bytes, *pixels, why)) {
        // A changed file refused mid-session keeps the last good picture (a save from an editor caught half-written).
        if (why != s.refusal) {
            Report(sail, "refused", why + (had ? " (the last good picture stays until the game restarts)" : ""));
        }
        s.refusal = why;
        return;
    }
    Take(sail, std::move(pixels));
    s.refusal.clear();
    Report(sail, "accepted", "");
}

const char* kReadme =
    "Boat sail pictures\n"
    "\n"
    "TLDR:\n"
    "Put a picture here to draw it on a boat's sail. One file a sail:\n"
    "  raft.png       the raft's sail\n"
    "  skiff.png      the skiff's sail\n"
    "  sloop.png      the sloop's mainsail\n"
    "  sloop-jib.png  the sloop's jib (or set the jib to match the mainsail)\n"
    "\n"
    "The picture can then be applied to your chosen sail(s) by opening the SoH menu: Enhancements > Boat Sails,\n"
    "and select \"custom\" from the desired drop-down. Alternatively, find the in-game NPC at any port to help you\n"
    "change your sail settings.\n"
    "\n"
    "For more control over cropping, open the desired template from the templates/ directory in your preferred\n"
    "photo editing software, make a new layer and draw your design inside the cut-out of the sail graphic, hide or\n"
    "delete the layer with the sail graphic template (do not change the image size), and then make sure you save-as\n"
    "with the appropriate file name (as a .png) in the sails/ dir.\n"
    "\n"
    "Transparent pixels will pick up the color you have your sail set to so you can test it against any color you\n"
    "want in-game without exporting multiple.\n"
    "\n"
    "--\n"
    "\n"
    "More info:\n"
    "\n"
    "Instead of copying a file here yourself, use the Boat Sails window: \"Choose image...\" copies one here for\n"
    "you, and so does dropping one on the game window after pressing \"Drop an image now\". From the console:\n"
    "`boat look <kind> sail_image custom`, or `boat look sloop jib_image custom` for the jib.\n"
    "\n"
    "Each template is the 1024 x 1024 square the game draws, the sail's outline in white: the grey around it never\n"
    "shows. The picture reads correctly from the boat's right-hand (starboard) side, bow on the right; the other\n"
    "side shows it mirrored. A jib set to match the mainsail shows the mainsail's picture flipped.\n"
    "\n"
    "Any PNG, JPG, BMP or TGA up to 8192 pixels a side and 16 MB. A picture that is not square is cropped to the\n"
    "square in its middle, then scaled to 1024 x 1024. A file changed while the game runs is read again at the next\n"
    "door, or with `boat sails reload`.\n"
    "\n"
    "The game rewrites this readme and the files in templates/ each time it starts, so keep your own notes and\n"
    "edits in files of other names.\n";

// Writes the file unless it already holds exactly these bytes: a readme or template from an older build is replaced.
void WriteIfDifferent(const fs::path& out, const std::vector<uint8_t>& bytes) {
    std::error_code ec;
    std::vector<uint8_t> was;
    std::string why;
    if (!fs::exists(out, ec) || !ReadFile(out, was, why) || was != bytes) {
        if (!WriteFile(out, bytes)) {
            SPDLOG_WARN("[rs_boat] could not write {}", Text(out));
        }
    }
}

// The folder, its readme and the templates, written when missing or different. Quiet: a folder the game cannot
// write (a read-only install) only means no templates.
void PrepareFolder() {
    std::error_code ec;
    fs::create_directories(PathOf(RsBoatSail_TemplateFolder()), ec);
    WriteIfDifferent(PathOf(RsBoatSail_Folder()) / "readme.txt",
                     std::vector<uint8_t>(kReadme, kReadme + std::strlen(kReadme)));
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
        char inArchive[256];
        if (!RsBoatSail_TemplateInArchive(sail, inArchive, sizeof(inArchive))) {
            continue;
        }
        if (!archives->HasFile(inArchive)) {
            continue; // an archive from before #287
        }
        auto file = archives->LoadFile(inArchive);
        if (file == nullptr || file->Buffer == nullptr) {
            continue;
        }
        const std::vector<uint8_t> bytes(file->Buffer->begin() + file->BufferOffset, file->Buffer->end());
        const std::string name = std::string(kSails[sail].name) + "-template.png";
        WriteIfDifferent(PathOf(RsBoatSail_TemplateFolder()) / name, bytes);
    }
}

void OnSceneInitSails(int16_t) {
    RsBoatSail_Check();
}

void OnFrameSails() {
    // The file dialog's answer, whoever opened it (the window or the sailmaker): its thread is done.
    int32_t picked = -1;
    std::string path;
    if (RsBoatSailDialog_Poll(&picked, &path)) {
        if (path.empty()) {
            Report(picked, "cancelled", "");
        } else {
            RsBoatSail_Import(picked, path.c_str(), nullptr, 0);
        }
    }
    for (auto it = sRetired.begin(); it != sRetired.end();) {
        if (--it->framesLeft > 0) {
            ++it;
            continue;
        }
        Gfx_TextureCacheDelete(it->pixels->data());
        it = sRetired.erase(it);
    }
}

bool OnDropSails(char* path) {
    return RsBoatSail_HandleDrop(path) != 0;
}

void RegisterRsBoatSails() {
    // ShipInit "*" functions re-run on preset apply and config drop.
    static bool registered = false;
    if (registered) {
        return;
    }
    registered = true;
    COND_HOOK(OnSceneInit, true, OnSceneInitSails);
    COND_HOOK(OnGameFrameUpdate, true, OnFrameSails);
    // An armed drop is the sail's whatever the file is, so this handler goes first: SoH's config handler, registered
    // before ShipInit runs, toasts "Failed to load config file" on a picture before passing it on, and would apply a
    // JSON dropped while armed. Its handlers go back behind this one in the order they had; one never registered stays
    // so. Unarmed, this handler declines and they run as before.
    auto drops = Ship::Context::GetRawInstance()->GetFileDropMgr();
    const bool rando = drops->UnregisterDropHandler(Rando_HandleSpoilerDrop);
    const bool config = drops->UnregisterDropHandler(SoH_HandleConfigDrop);
    drops->RegisterDropHandler(OnDropSails);
    if (rando) {
        drops->RegisterDropHandler(Rando_HandleSpoilerDrop);
    }
    if (config) {
        drops->RegisterDropHandler(SoH_HandleConfigDrop);
    }
}

RegisterShipInitFunc rsBoatSailsInitFunc(RegisterRsBoatSails);

} // namespace

extern "C" const char* RsBoatSail_Name(int32_t sail) {
    return SailValid(sail) ? kSails[sail].name : "?";
}

extern "C" int32_t RsBoatSail_ByName(const char* name) {
    for (int32_t sail = 0; name != nullptr && sail < RS_BOAT_SAIL_COUNT; sail++) {
        if (std::strcmp(kSails[sail].name, name) == 0) {
            return sail;
        }
    }
    return -1;
}

extern "C" int32_t RsBoatSail_Of(int32_t kind, int32_t imageLook) {
    for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
        if (kSails[sail].kind == kind && kSails[sail].look == imageLook) {
            return sail;
        }
    }
    return -1;
}

extern "C" int32_t RsBoatSail_Kind(int32_t sail) {
    return SailValid(sail) ? kSails[sail].kind : -1;
}

extern "C" int32_t RsBoatSail_Look(int32_t sail) {
    return SailValid(sail) ? kSails[sail].look : -1;
}

extern "C" const uint8_t* RsBoatSail_Pixels(int32_t sail) {
    return SailValid(sail) && sSails[sail].pixels != nullptr ? sSails[sail].pixels->data() : nullptr;
}

extern "C" int32_t RsBoatSail_Accepted(int32_t sail) {
    return RsBoatSail_Pixels(sail) != nullptr ? 1 : 0;
}

extern "C" int32_t RsBoatSail_Present(int32_t sail) {
    return SailValid(sail) && sSails[sail].present ? 1 : 0;
}

extern "C" const char* RsBoatSail_Refusal(int32_t sail) {
    return SailValid(sail) ? sSails[sail].refusal.c_str() : "";
}

extern "C" void RsBoatSail_Check(void) {
    if (!sReadOnce) {
        RsBoatSail_Reload();
        return;
    }
    for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
        std::error_code ec;
        const fs::path path = FilePath(sail);
        const bool present = fs::is_regular_file(path, ec);
        const Sail& s = sSails[sail];
        if (present != s.present ||
            (present && (fs::last_write_time(path, ec) != s.time || fs::file_size(path, ec) != s.size))) {
            Read(sail);
        }
    }
}

extern "C" void RsBoatSail_Reload(void) {
    if (!sReadOnce) {
        PrepareFolder();
        sReadOnce = true;
    }
    for (int32_t sail = 0; sail < RS_BOAT_SAIL_COUNT; sail++) {
        Read(sail);
    }
}

extern "C" int32_t RsBoatSail_Import(int32_t sail, const char* path, char* why, size_t len) {
    std::string reason;
    auto pixels = std::make_unique<Pixels>();
    std::vector<uint8_t> bytes;
    bool ok = SailValid(sail) && path != nullptr;
    if (!ok) {
        reason = "no such sail";
    }
    ok = ok && ReadFile(Existing(path), bytes, reason) && Convert(bytes, *pixels, reason);
    if (ok) {
        if (!sReadOnce) {
            RsBoatSail_Reload();
        }
        std::error_code ec;
        fs::create_directories(PathOf(RsBoatSail_Folder()), ec);
        if (!WriteFile(FilePath(sail), EncodePng(*pixels, RS_BOAT_SAIL_SIZE))) {
            ok = false;
            reason = "the game could not write " + Text(FilePath(sail));
        }
    }
    if (!ok) {
        if (SailValid(sail)) {
            Report(sail, "refused", reason, path != nullptr ? Text(Existing(path).filename()) : "");
        }
        if (why != nullptr && len > 0) {
            std::snprintf(why, len, "%s", reason.c_str());
        }
        return 0;
    }
    Take(sail, std::move(pixels));
    sSails[sail].refusal.clear();
    Stamp(sail);
    Report(sail, "accepted", std::string("imported from ") + Text(Existing(path).filename()));
    const int32_t look = kSails[sail].look;
    RsBoatFit_SetLook(kSails[sail].kind, look, RsBoatFit_LookOptionByName(look, "custom"));
    if (why != nullptr && len > 0) {
        why[0] = '\0';
    }
    return 1;
}

extern "C" void RsBoatSail_Arm(int32_t sail) {
    sArmed = SailValid(sail) ? sail : -1;
    char line[96];
    std::snprintf(line, sizeof(line), "rs_boat event=sail_arm sail=%s", sArmed >= 0 ? kSails[sArmed].name : "none");
    RsAgent_Marker(line);
}

extern "C" int32_t RsBoatSail_Armed(void) {
    return sArmed;
}

extern "C" int32_t RsBoatSail_HandleDrop(const char* path) {
    if (sArmed < 0) {
        return 0;
    }
    const int32_t sail = sArmed;
    sArmed = -1;
    RsBoatSail_Import(sail, path, nullptr, 0);
    return 1;
}

extern "C" int32_t RsBoatSail_DialogueAction(int32_t sail, int32_t op) {
    if (!SailValid(sail)) {
        return 0;
    }
    switch (op) {
        case RS_BOAT_SAIL_OP_ARM:
            RsBoatSail_Arm(sail);
            return 1;
        case RS_BOAT_SAIL_OP_BROWSE:
            RsBoatSail_Arm(-1);
            return RsBoatSailDialog_Open(sail, "") ? 1 : 0;
        case RS_BOAT_SAIL_OP_OFF: {
            RsBoatSail_Arm(-1);
            const int32_t look = kSails[sail].look;
            return RsBoatFit_SetLook(kSails[sail].kind, look, RsBoatFit_LookOptionByName(look, "none")) ==
                   RS_BOAT_FIT_OK;
        }
        case RS_BOAT_SAIL_OP_DONE:
            RsBoatSail_Arm(-1);
            return 1;
        case RS_BOAT_SAIL_OP_USE: {
            RsBoatSail_Arm(-1);
            if (!RsBoatSail_Accepted(sail)) {
                Report(sail, "refused",
                       *RsBoatSail_Refusal(sail) != '\0'
                           ? RsBoatSail_Refusal(sail)
                           : std::string("there is no picture for it in the sails folder (") + kSails[sail].name +
                                 ".png)");
                return 0;
            }
            const int32_t look = kSails[sail].look;
            return RsBoatFit_SetLook(kSails[sail].kind, look, RsBoatFit_LookOptionByName(look, "custom")) ==
                   RS_BOAT_FIT_OK;
        }
        default:
            return 0;
    }
}

extern "C" int32_t RsBoatSail_TemplateInArchive(int32_t sail, char* buf, size_t len) {
    const char* list = SailValid(sail) ? RsBoat_KindListPath(kSails[sail].kind) : nullptr;
    if (list == nullptr || buf == nullptr || len == 0) {
        return 0;
    }
    const int n = std::snprintf(buf, len, "%s.template.%s.png", list, kSails[sail].name);
    return n > 0 && (size_t)n < len ? 1 : 0;
}

extern "C" int32_t RsBoatSail_Pattern(int32_t sail) {
    return SailValid(sail) ? kSails[sail].pattern : -1;
}

extern "C" const uint8_t* RsBoatSail_DrawnPixels(int32_t sail) {
    if (!SailValid(sail)) {
        return nullptr;
    }
    int32_t drawnAs = kSails[sail].look;
    const int32_t option = RsBoatFit_LookDrawn(kSails[sail].kind, kSails[sail].look, &drawnAs);
    if (RsBoatFit_LookOptionKind(drawnAs, option) != RS_BOAT_LOOK_OPTION_CUSTOM) {
        return nullptr;
    }
    if (drawnAs == kSails[sail].look) {
        return RsBoatSail_Pixels(sail);
    }
    const int32_t source = RsBoatSail_Of(kSails[sail].kind, drawnAs);
    return SailValid(source) ? Mirrored(source) : nullptr;
}

extern "C" const char* RsBoatSail_Folder(void) {
    if (sFolder.empty()) {
        sFolder = Ship::Context::GetPathRelativeToAppDirectory("sails");
    }
    return sFolder.c_str();
}

extern "C" const char* RsBoatSail_TemplateFolder(void) {
    if (sTemplateFolder.empty()) {
        sTemplateFolder = Text(PathOf(RsBoatSail_Folder()) / "templates");
    }
    return sTemplateFolder.c_str();
}

extern "C" uint32_t RsBoatSail_Generation(void) {
    return sGeneration;
}
