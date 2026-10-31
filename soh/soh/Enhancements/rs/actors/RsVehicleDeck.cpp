// RsVehicleDeck.cpp - a vehicle's drawn deck for Link's shadow on it (sturdy-bassoon#267): the faces it falls on and a
// height map of the deck, read from the vehicle's archive (the boats) or handed over as triangles (the balloon's
// placeholder basket). RsVehicleShadow.h has the design; RsVehicleShadow.c draws the shadow.

#include "RsVehicleShadow.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <ship/Context.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>
#include <tinyxml2.h>

// --- the height map and the faces ----------------------------------------------------------------------------
//
// Read straight from the archive's XML, as the game would draw it: each copy's triangle lists (`copies/<copy>/tri_N`)
// and the vertex files they load. The sails (with the mast) and the helm are left out. Every up-facing face (normal y
// at least kMinNy) is rasterised onto a 1-unit grid, keeping the highest; each 2-unit knot then takes the highest
// sample within kDilate of it that is at most kStepUp above its own, so the shadow's bilinear surface stays on or over
// rounded edges (the keel's spine, the lid's crown) instead of cutting under them. A wall is a bigger step than that
// and is left alone. A knot with no face over it is NONE. Heights are kept to a half unit, rounded up.

namespace {

constexpr double kMinNy = 0.35;
// A face the shadow falls on: any not facing down, so the slanted walls and the trim's upright inner face too (at 0.1
// the inner face was left out, a strip of bare wood between the rim's shadow and the wall's: the owner, 2026-10-30).
constexpr double kFaceMinNy = -0.05;
constexpr int kFine = 1;
constexpr int kDilate = 2;
constexpr double kStepUp = 4.0;

struct DeckTri {
    double a[3];
    double b[3];
    double c[3];
};

struct DeckMap {
    RsVehicleDeck deck;
    std::vector<u8> cells;
    std::vector<RsVehicleDeckFace> faces;
};

// By list path, once each: a null entry is a kind with no archive or no deck.
std::map<std::string, std::unique_ptr<DeckMap>> sDecks;

std::optional<std::string> ReadText(const std::string& path) {
    auto file = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager()->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr || file->Buffer->size() <= file->BufferOffset) {
        return std::nullopt;
    }
    return std::string(file->Buffer->data() + file->BufferOffset, file->Buffer->size() - file->BufferOffset);
}

// One triangle list's faces, its vertex files read through `vertexFiles` (each once).
void ReadTris(const std::string& path, std::map<std::string, std::vector<std::array<double, 3>>>& vertexFiles,
              std::vector<DeckTri>& out) {
    auto text = ReadText(path);
    tinyxml2::XMLDocument doc;
    if (!text || doc.Parse(text->c_str(), text->size()) != tinyxml2::XML_SUCCESS ||
        doc.FirstChildElement() == nullptr) {
        return;
    }
    std::map<int, std::array<double, 3>> buffer; // the vertex cache as the list loads it
    for (auto* e = doc.FirstChildElement()->FirstChildElement(); e != nullptr; e = e->NextSiblingElement()) {
        const std::string name = e->Name();
        if (name == "LoadVertices") {
            const std::string vtxPath = e->Attribute("Path") != nullptr ? e->Attribute("Path") : "";
            auto found = vertexFiles.find(vtxPath);
            if (found == vertexFiles.end()) {
                std::vector<std::array<double, 3>> verts;
                tinyxml2::XMLDocument vdoc;
                auto vtext = ReadText(vtxPath);
                if (vtext && vdoc.Parse(vtext->c_str(), vtext->size()) == tinyxml2::XML_SUCCESS &&
                    vdoc.FirstChildElement() != nullptr) {
                    for (auto* v = vdoc.FirstChildElement()->FirstChildElement("Vtx"); v != nullptr;
                         v = v->NextSiblingElement("Vtx")) {
                        verts.push_back({ (double)v->IntAttribute("X"), (double)v->IntAttribute("Y"),
                                          (double)v->IntAttribute("Z") });
                    }
                }
                found = vertexFiles.emplace(vtxPath, std::move(verts)).first;
            }
            const int start = e->IntAttribute("VertexBufferIndex");
            const int offset = e->IntAttribute("VertexOffset");
            const int count = e->IntAttribute("Count");
            for (int k = 0; k < count && offset + k < (int)found->second.size(); k++) {
                buffer[start + k] = found->second[offset + k];
            }
            continue;
        }
        const int tris = (name == "Triangle1") ? 1 : (name == "Triangle2") ? 2 : 0;
        for (int t = 0; t < tris; t++) {
            const char* keys[2][3] = { { "V00", "V01", "V02" }, { "V10", "V11", "V12" } };
            DeckTri tri;
            double* corners[3] = { tri.a, tri.b, tri.c };
            bool ok = true;
            for (int k = 0; k < 3 && ok; k++) {
                auto v = buffer.find(e->IntAttribute(keys[t][k]));
                ok = v != buffer.end();
                if (ok) {
                    std::copy(v->second.begin(), v->second.end(), corners[k]);
                }
            }
            if (ok) {
                out.push_back(tri);
            }
        }
    }
}

// The archive's triangles, from every copy but those in `skip` (NULL-terminated, or NULL).
std::vector<DeckTri> ArchiveTris(const std::string& listPath, const char* const* skip) {
    const std::string root = listPath.substr(0, listPath.rfind('/') + 1); // ".../rs_boat_<kind>/"
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    auto lists = archives->ListFiles(root + "copies/*/tri_*");
    std::map<std::string, std::vector<std::array<double, 3>>> vertexFiles;
    std::vector<DeckTri> tris;

    for (const auto& path : *lists) {
        const size_t at = root.size() + std::string("copies/").size();
        const std::string copy = path.substr(at, path.find('/', at) - at);
        bool skipped = false;
        for (const char* const* name = skip; name != nullptr && *name != nullptr; name++) {
            skipped = skipped || copy == *name;
        }
        if (!skipped) {
            ReadTris(path, vertexFiles, tris);
        }
    }
    return tris;
}

std::unique_ptr<DeckMap> BuildDeck(const std::string& listPath, const std::vector<DeckTri>& tris) {
    const auto start = std::chrono::steady_clock::now();

    // The up-facing ones, and their extent.
    struct Up {
        DeckTri t;
        double nx, ny, nz;
    };
    std::vector<Up> up;
    std::vector<RsVehicleDeckFace> faces;
    double lo[2] = { 1e9, 1e9 };
    double hi[2] = { -1e9, -1e9 };
    for (const auto& t : tris) {
        const double ux = t.b[0] - t.a[0], uy = t.b[1] - t.a[1], uz = t.b[2] - t.a[2];
        const double vx = t.c[0] - t.a[0], vy = t.c[1] - t.a[1], vz = t.c[2] - t.a[2];
        const double nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (length == 0.0 || ny / length < kFaceMinNy) {
            continue;
        }
        RsVehicleDeckFace face;
        const double* corners[3] = { t.a, t.b, t.c };
        for (int k = 0; k < 3; k++) {
            for (int axis = 0; axis < 3; axis++) {
                face.v[k][axis] = (f32)corners[k][axis];
            }
        }
        face.n[0] = (f32)(nx / length);
        face.n[1] = (f32)(ny / length);
        face.n[2] = (f32)(nz / length);
        face.lo[0] = (f32)std::min({ t.a[0], t.b[0], t.c[0] });
        face.lo[1] = (f32)std::min({ t.a[2], t.b[2], t.c[2] });
        face.hi[0] = (f32)std::max({ t.a[0], t.b[0], t.c[0] });
        face.hi[1] = (f32)std::max({ t.a[2], t.b[2], t.c[2] });
        faces.push_back(face);
        if (ny / length < kMinNy) {
            continue;
        }
        up.push_back({ t, nx, ny, nz });
        for (const double* p : { t.a, t.b, t.c }) {
            lo[0] = std::min(lo[0], p[0]);
            lo[1] = std::min(lo[1], p[2]);
            hi[0] = std::max(hi[0], p[0]);
            hi[1] = std::max(hi[1], p[2]);
        }
    }
    if (up.empty()) {
        SPDLOG_WARN("[rs_vehicle] {}: no deck faces in {} triangles; no draped shadow on this boat", listPath,
                    tris.size());
        return nullptr;
    }

    // The highest up-facing face at every fine sample.
    const int x0 = (int)std::floor(lo[0]);
    const int z0 = (int)std::floor(lo[1]);
    const int w = ((int)std::ceil(hi[0]) - x0) / kFine + 1;
    const int h = ((int)std::ceil(hi[1]) - z0) / kFine + 1;
    std::vector<double> fine((size_t)w * h, NAN);
    for (const auto& u : up) {
        const DeckTri& t = u.t;
        const double det = (t.b[0] - t.a[0]) * (t.c[2] - t.a[2]) - (t.c[0] - t.a[0]) * (t.b[2] - t.a[2]);
        if (det == 0.0) {
            continue;
        }
        const double minX = std::min({ t.a[0], t.b[0], t.c[0] }), maxX = std::max({ t.a[0], t.b[0], t.c[0] });
        const double minZ = std::min({ t.a[2], t.b[2], t.c[2] }), maxZ = std::max({ t.a[2], t.b[2], t.c[2] });
        for (int j = std::max(0, (int)std::ceil((minZ - z0) / kFine));
             j <= std::min(h - 1, (int)std::floor((maxZ - z0) / kFine)); j++) {
            const double z = (double)(z0 + j * kFine);
            for (int i = std::max(0, (int)std::ceil((minX - x0) / kFine));
                 i <= std::min(w - 1, (int)std::floor((maxX - x0) / kFine)); i++) {
                const double x = (double)(x0 + i * kFine);
                const double s = ((x - t.a[0]) * (t.c[2] - t.a[2]) - (t.c[0] - t.a[0]) * (z - t.a[2])) / det;
                const double r = ((t.b[0] - t.a[0]) * (z - t.a[2]) - (x - t.a[0]) * (t.b[2] - t.a[2])) / det;
                if (s < -1e-6 || r < -1e-6 || s + r > 1.0 + 1e-6) {
                    continue;
                }
                const double y = t.a[1] - (u.nx * (x - t.a[0]) + u.nz * (z - t.a[2])) / u.ny;
                double& cell = fine[(size_t)j * w + i];
                if (std::isnan(cell) || y > cell) {
                    cell = y;
                }
            }
        }
    }

    // The knots: each fine sample's height, raised over rounded edges, then quantised.
    const int kw = ((w - 1) * kFine + RS_VEHICLE_DECK_STEP - 1) / RS_VEHICLE_DECK_STEP + 1;
    const int kh = ((h - 1) * kFine + RS_VEHICLE_DECK_STEP - 1) / RS_VEHICLE_DECK_STEP + 1;
    const int reach = kDilate / kFine;
    std::vector<double> knots((size_t)kw * kh, NAN);
    double minY = 1e9, maxY = -1e9;
    for (int kj = 0; kj < kh; kj++) {
        for (int ki = 0; ki < kw; ki++) {
            const int j = std::min(h - 1, kj * RS_VEHICLE_DECK_STEP / kFine);
            const int i = std::min(w - 1, ki * RS_VEHICLE_DECK_STEP / kFine);
            const double own = fine[(size_t)j * w + i];
            if (std::isnan(own)) {
                continue;
            }
            double best = own;
            for (int jj = std::max(0, j - reach); jj < std::min(h, j + reach + 1); jj++) {
                for (int ii = std::max(0, i - reach); ii < std::min(w, i + reach + 1); ii++) {
                    const double v = fine[(size_t)jj * w + ii];
                    if (!std::isnan(v) && v > best && v <= own + kStepUp) {
                        best = v;
                    }
                }
            }
            knots[(size_t)kj * kw + ki] = best;
            minY = std::min(minY, best);
            maxY = std::max(maxY, best);
        }
    }
    const int base = (int)std::floor(minY);
    if ((maxY - base) * 2.0 > RS_VEHICLE_DECK_NONE - 1) {
        SPDLOG_WARN("[rs_vehicle] {}: the deck spans {:.1f} units, over a cell's {}; no draped shadow on this boat",
                    listPath, maxY - base, (RS_VEHICLE_DECK_NONE - 1) / 2);
        return nullptr;
    }
    auto deck = std::make_unique<DeckMap>();
    deck->cells.resize(knots.size());
    for (size_t k = 0; k < knots.size(); k++) {
        deck->cells[k] = std::isnan(knots[k]) ? RS_VEHICLE_DECK_NONE : (u8)std::ceil((knots[k] - base) * 2.0 - 1e-6);
    }
    deck->deck.map = { (s16)x0, (s16)z0, (s16)kw, (s16)kh, (s16)base, deck->cells.data() };
    // Each corner's normal, summed over the faces that share it (weighted by their area: the unnormalised normals), so
    // the shadow's copies, lifted along it, still meet along every shared edge.
    {
        std::map<std::array<int, 3>, std::array<double, 3>> sums;
        auto key = [](const f32* v) {
            return std::array<int, 3>{ (int)std::lround(v[0] * 8.0f), (int)std::lround(v[1] * 8.0f),
                                       (int)std::lround(v[2] * 8.0f) };
        };
        for (const auto& face : faces) {
            const double ux = face.v[1][0] - face.v[0][0];
            const double uy = face.v[1][1] - face.v[0][1];
            const double uz = face.v[1][2] - face.v[0][2];
            const double vx = face.v[2][0] - face.v[0][0];
            const double vy = face.v[2][1] - face.v[0][1];
            const double vz = face.v[2][2] - face.v[0][2];
            const double n[3] = { uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx };
            for (int k = 0; k < 3; k++) {
                auto& sum = sums[key(face.v[k])];
                sum[0] += n[0];
                sum[1] += n[1];
                sum[2] += n[2];
            }
        }
        for (auto& face : faces) {
            for (int k = 0; k < 3; k++) {
                const auto& sum = sums[key(face.v[k])];
                const double length = std::sqrt(sum[0] * sum[0] + sum[1] * sum[1] + sum[2] * sum[2]);
                for (int axis = 0; axis < 3; axis++) {
                    // Faces that nearly cancel (opposite windings over the same corner) leave noise: the face's own.
                    face.vn[k][axis] = (length > 1e-3) ? (f32)(sum[axis] / length) : face.n[axis];
                }
            }
        }
    }
    deck->faces = std::move(faces);
    deck->deck.faces = deck->faces.data();
    deck->deck.numFaces = (s32)deck->faces.size();
    // sum = cells weighted by their index + 1, mod 2^32: a fingerprint to compare one build's map with another's
    uint32_t sum = 0;
    for (size_t k = 0; k < deck->cells.size(); k++) {
        sum += (uint32_t)(k + 1) * deck->cells[k];
    }
    SPDLOG_INFO(
        "[rs_vehicle] {}: deck map {} x {} knots from x {} z {}, from {} faces ({} up-facing), heights {}..{:.1f}, "
        "sum {}, {} faces the shadow falls on, in {} ms",
        listPath, kw, kh, x0, z0, tris.size(), up.size(), base, maxY, sum, deck->faces.size(),
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count());
    return deck;
}

} // namespace

extern "C" const RsVehicleDeck* RsVehicleDeck_FromArchive(const char* listPath, const char* const* skipCopies) {
    auto found = sDecks.find(listPath);
    if (found == sDecks.end()) {
        auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
        found =
            sDecks
                .emplace(listPath,
                         archives->HasFile(listPath) ? BuildDeck(listPath, ArchiveTris(listPath, skipCopies)) : nullptr)
                .first;
    }
    return found->second != nullptr ? &found->second->deck : nullptr;
}

extern "C" const RsVehicleDeck* RsVehicleDeck_FromTris(const char* key, const f32 (*tris)[3][3], s32 count) {
    auto found = sDecks.find(key);
    if (found == sDecks.end()) {
        std::vector<DeckTri> in((size_t)count);
        for (s32 t = 0; t < count; t++) {
            for (int axis = 0; axis < 3; axis++) {
                in[t].a[axis] = tris[t][0][axis];
                in[t].b[axis] = tris[t][1][axis];
                in[t].c[axis] = tris[t][2][axis];
            }
        }
        found = sDecks.emplace(key, BuildDeck(key, in)).first;
    }
    return found->second != nullptr ? &found->second->deck : nullptr;
}
