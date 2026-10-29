/*
 * The ambience mixer (sturdy-bassoon#275). RsAmbienceMixer.h says what it is and why it sits beside the engine.
 */

#include "RsAmbienceMixer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <dr_flac.h> // implementations: AudioSampleFactory.cpp
#include <dr_wav.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/archive/ArchiveManager.h>
#include <spdlog/spdlog.h>

#include "soh/cvar_prefixes.h"

namespace {

// OTRGlobals.cpp's InitAudio .SampleRate: the rate every batch handed to RsAmbienceMixer_Mix runs at.
constexpr double kOutRate = 32000.0;
// Voices the audio thread can hold, fading ones included. RsAmbience.cpp asks for at most 8 at once.
constexpr int32_t kMaxVoices = 16;
// A gain moves at most this much per output frame: 0 to 1 in 20 ms, quick enough to follow Link and slow
// enough that a start, a stop or a pan never clicks.
constexpr float kRampPerFrame = 1.0f / (0.020f * 32000.0f);
// With no submission for this many frames (a quarter second), every voice fades out: the game has stopped
// ticking - the pause menu, a scene load - and the last picture of what should play is stale.
constexpr uint32_t kStaleFrames = 8000;

struct Sample {
    std::vector<float> data; // mono, -1..1
    double rate;
};
using SamplePtr = std::shared_ptr<const Sample>;

// Game thread only.
std::unordered_map<std::string, SamplePtr> sSamples;
std::unordered_map<std::string, bool> sFailed;

struct Target {
    int32_t key;
    SamplePtr sample;
    bool loop;
    uint32_t trigger;
    float left, right;
};

// The game thread's latest submission, handed to the audio thread under sMutex.
std::mutex sMutex;
std::vector<Target> sPending;
uint64_t sPendingSerial = 0;

// Audio thread only.
struct Voice {
    bool used = false;
    int32_t key = 0;
    SamplePtr sample;
    bool loop = false;
    double pos = 0.0;
    float left = 0.0f, right = 0.0f;        // current gains
    float wantLeft = 0.0f, wantRight = 0.0f; // where they are ramping to
};
Voice sVoices[kMaxVoices];
std::vector<Target> sCurrent;
std::unordered_map<int32_t, uint32_t> sLastTrigger;
uint64_t sSeenSerial = 0;
uint32_t sFramesSinceSubmit = 0;
std::vector<float> sAccum;

std::atomic<int32_t> sStatVoices{ 0 };
std::atomic<int32_t> sStatAudible{ 0 };
std::atomic<int32_t> sStatLoaded{ 0 };
std::atomic<uint64_t> sStatBatches{ 0 };
// The keys heard in the last batch, for the console. Written by the audio thread under sStatMutex.
std::mutex sStatMutex;
int32_t sStatKeys[kMaxVoices] = {};
int32_t sStatKeyCount = 0;

bool DecodeWav(const std::vector<char>& bytes, Sample* out) {
    drwav wav;
    if (!drwav_init_memory(&wav, bytes.data(), bytes.size(), nullptr)) {
        return false;
    }
    const drwav_uint64 frames = wav.totalPCMFrameCount;
    const uint32_t channels = wav.channels;
    std::vector<float> interleaved((size_t)frames * channels);
    const drwav_uint64 read = drwav_read_pcm_frames_f32(&wav, frames, interleaved.data());
    out->rate = wav.sampleRate;
    drwav_uninit(&wav);
    out->data.resize((size_t)read);
    for (size_t f = 0; f < (size_t)read; f++) {
        float sum = 0.0f;
        for (uint32_t c = 0; c < channels; c++) {
            sum += interleaved[f * channels + c];
        }
        out->data[f] = sum / (float)channels;
    }
    return read > 0 && out->rate > 0;
}

bool DecodeFlac(const std::vector<char>& bytes, Sample* out) {
    unsigned int channels = 0, rate = 0;
    drflac_uint64 frames = 0;
    float* pcm = drflac_open_memory_and_read_pcm_frames_f32(bytes.data(), bytes.size(), &channels, &rate, &frames,
                                                             nullptr);
    if (pcm == nullptr) {
        return false;
    }
    out->rate = rate;
    out->data.resize((size_t)frames);
    for (size_t f = 0; f < (size_t)frames; f++) {
        float sum = 0.0f;
        for (unsigned int c = 0; c < channels; c++) {
            sum += pcm[f * channels + c];
        }
        out->data[f] = channels > 0 ? sum / (float)channels : 0.0f;
    }
    drflac_free(pcm, nullptr);
    return frames > 0 && rate > 0;
}

// One sample of the file at a fractional position, linearly interpolated.
float SampleAt(const Sample& s, double pos, bool loop) {
    const size_t n = s.data.size();
    const size_t i = (size_t)pos;
    const float frac = (float)(pos - (double)i);
    const float a = s.data[i % n];
    const size_t j = i + 1;
    const float b = j < n ? s.data[j] : (loop ? s.data[0] : 0.0f);
    return a + (b - a) * frac;
}

float Toward(float value, float target) {
    if (value < target) {
        return std::min(value + kRampPerFrame, target);
    }
    return std::max(value - kRampPerFrame, target);
}

Voice* FindVoice(int32_t key) {
    for (Voice& v : sVoices) {
        if (v.used && v.key == key) {
            return &v;
        }
    }
    return nullptr;
}

Voice* FreeVoice() {
    for (Voice& v : sVoices) {
        if (!v.used) {
            return &v;
        }
    }
    return nullptr;
}

void Start(Voice* v, const Target& t) {
    v->used = true;
    v->key = t.key;
    v->sample = t.sample;
    v->loop = t.loop;
    v->pos = 0.0;
    v->left = 0.0f;
    v->right = 0.0f;
}

// Brings the voices in line with the latest submission: starts what is new, retargets what is held, and sends
// what is no longer wanted toward silence.
void Reconcile() {
    {
        std::lock_guard<std::mutex> lock(sMutex);
        if (sPendingSerial != sSeenSerial) {
            sCurrent = sPending;
            sSeenSerial = sPendingSerial;
            sFramesSinceSubmit = 0;
        }
    }
    const bool stale = sFramesSinceSubmit > kStaleFrames;
    for (Voice& v : sVoices) {
        v.wantLeft = 0.0f;
        v.wantRight = 0.0f;
    }
    if (stale) {
        return;
    }
    for (const Target& t : sCurrent) {
        Voice* v = FindVoice(t.key);
        if (t.loop) {
            if (v == nullptr && (v = FreeVoice()) != nullptr) {
                Start(v, t);
            }
        } else {
            uint32_t& last = sLastTrigger[t.key];
            if (t.trigger != last) {
                last = t.trigger;
                if (v == nullptr) {
                    v = FreeVoice();
                }
                if (v != nullptr) {
                    Start(v, t);
                }
            }
        }
        if (v != nullptr) {
            v->wantLeft = t.left;
            v->wantRight = t.right;
        }
    }
}

} // namespace

bool RsAmbienceMixer_Load(const char* id) {
    const std::string key = id;
    if (sSamples.count(key) != 0) {
        return true;
    }
    if (sFailed.count(key) != 0) {
        return false;
    }
    auto archives = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    for (const char* ext : { ".wav", ".flac" }) {
        const std::string path = "rs/sfx/" + key + ext;
        auto file = archives->LoadFile(path);
        if (file == nullptr || file->Buffer == nullptr || file->Buffer->empty()) {
            continue;
        }
        auto sample = std::make_shared<Sample>();
        const bool wav = ext[1] == 'w';
        const bool ok = wav ? DecodeWav(*file->Buffer, sample.get()) : DecodeFlac(*file->Buffer, sample.get());
        if (!ok) {
            SPDLOG_WARN("[rs-ambience] {} did not decode", path);
            sFailed[key] = true;
            return false;
        }
        sSamples[key] = sample;
        sStatLoaded = (int32_t)sSamples.size();
        return true;
    }
    SPDLOG_WARN("[rs-ambience] no mounted archive holds rs/sfx/{}.wav or .flac - run npm run fetch:rs-sfx", key);
    sFailed[key] = true;
    return false;
}

void RsAmbienceMixer_Submit(const RsAmbienceVoice* voices, int32_t count) {
    std::vector<Target> targets;
    targets.reserve(count);
    for (int32_t i = 0; i < count; i++) {
        auto it = sSamples.find(voices[i].id);
        if (it == sSamples.end()) {
            continue;
        }
        targets.push_back({ voices[i].key, it->second, voices[i].loop, voices[i].trigger,
                            std::clamp(voices[i].left, 0.0f, 1.0f), std::clamp(voices[i].right, 0.0f, 1.0f) });
    }
    std::lock_guard<std::mutex> lock(sMutex);
    sPending.swap(targets);
    sPendingSerial++;
}

RsAmbienceMixerStats RsAmbienceMixer_GetStats() {
    RsAmbienceMixerStats s;
    s.voices = sStatVoices;
    s.audible = sStatAudible;
    s.loaded = sStatLoaded;
    s.batches = sStatBatches;
    std::lock_guard<std::mutex> lock(sStatMutex);
    s.audibleKeyCount = sStatKeyCount;
    for (int32_t i = 0; i < sStatKeyCount; i++) {
        s.audibleKeys[i] = sStatKeys[i];
    }
    return s;
}

void RsAmbienceMixer_Mix(int16_t* stereo, uint32_t frames) {
    Reconcile();
    sFramesSinceSubmit += frames;

    // The sliders the way the engine applies them: master linearly per note (audio_playback.c), SFX as the
    // player's gameVolume, which audio_effects.c squares with the rest of the channel volume.
    const float master = (float)CVarGetInteger(CVAR_SETTING("Volume.Master"), 40) / 100.0f;
    const float sfx = (float)CVarGetInteger(CVAR_SETTING("Volume.SFX"), 100) / 100.0f;
    const float scale = master * sfx * sfx * 32767.0f;

    sAccum.assign((size_t)frames * 2, 0.0f);
    int32_t held = 0, audible = 0;
    int32_t keys[kMaxVoices];
    for (Voice& v : sVoices) {
        if (!v.used) {
            continue;
        }
        const Sample& s = *v.sample;
        const double step = s.rate / kOutRate;
        const double length = (double)s.data.size();
        bool heard = false;
        for (uint32_t f = 0; f < frames; f++) {
            if (v.pos >= length) {
                if (!v.loop) {
                    break;
                }
                v.pos = std::fmod(v.pos, length);
            }
            v.left = Toward(v.left, v.wantLeft);
            v.right = Toward(v.right, v.wantRight);
            if (v.left > 0.0f || v.right > 0.0f) {
                const float x = SampleAt(s, v.pos, v.loop);
                sAccum[f * 2] += x * v.left;
                sAccum[f * 2 + 1] += x * v.right;
                heard = true;
            }
            v.pos += step;
        }
        const bool finished = !v.loop && v.pos >= length;
        const bool silent = v.left == 0.0f && v.right == 0.0f && v.wantLeft == 0.0f && v.wantRight == 0.0f;
        if (finished || silent) {
            v = Voice();
            continue;
        }
        held++;
        if (heard) {
            keys[audible++] = v.key;
        }
    }
    for (uint32_t i = 0; i < frames * 2; i++) {
        const float mixed = (float)stereo[i] + sAccum[i] * scale;
        stereo[i] = (int16_t)std::clamp(mixed, -32768.0f, 32767.0f);
    }
    sStatVoices = held;
    sStatAudible = audible;
    {
        std::lock_guard<std::mutex> lock(sStatMutex);
        sStatKeyCount = audible;
        for (int32_t i = 0; i < audible; i++) {
            sStatKeys[i] = keys[i];
        }
    }
    sStatBatches++;
}
