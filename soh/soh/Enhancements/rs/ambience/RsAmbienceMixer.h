#ifndef SOH_RS_AMBIENCE_MIXER_H
#define SOH_RS_AMBIENCE_MIXER_H

#include <stdint.h>

// The ambience mixer (sturdy-bassoon#275): plays external audio files - RS sound effects from the OSRS wiki,
// or any WAV/FLAC - by adding them to SoH's finished output, beside OoT's sound engine rather than through it.
//
// WHY BESIDE IT. A sound through the engine needs an sfx id with a hand-written script in sequence 0, whose
// dispatch tables have no free room (8 env ids, 13 enemy ids, against 74 distinct RS ambient sounds), and it
// competes for the env bank's 3 channels by an importance scheme in which a fire loses to water. A voice here
// needs neither: OTRAudio_Thread (OTRGlobals.cpp) builds each 32 kHz stereo batch with
// AudioMgr_CreateNextAudioBuffer, and RsAmbienceMixer_Mix adds these voices to it before AudioPlayer_Play. The
// cost: OoT's reverb and behind-camera filter do not apply, and the master and SFX sliders are applied here
// the way audio_playback.c and audio_effects.c apply them.
//
// FILES. rs/sfx/<id>.wav or .flac, read from any mounted archive. tools/sfx/fetch-rs-sfx.ts writes them into
// <app dir>/mods/rs-sfx.o2r from tools/sfx/rs-sfx.json. Decoded once, on the game thread, to mono float at
// the file's own rate; resampled to 32 kHz as it plays.
//
// THREADS. The game thread describes what should be playing once a tick (RsAmbienceMixer_Submit); the audio
// thread reconciles its voices against that, ramping every gain so nothing clicks. If no submission arrives for
// a quarter of a second - the pause menu, a scene load - every voice fades out.

// Loads rs/sfx/<id>.wav (or .flac) if it is not loaded yet. Game thread. False when no mounted archive holds
// it or it does not decode; the reason is logged once.
bool RsAmbienceMixer_Load(const char* id);

// One voice the game thread wants this tick.
struct RsAmbienceVoice {
    int32_t key;       // stable per emitter: a submission with the same key is the same voice
    const char* id;    // the file's id; must already be loaded
    bool loop;         // true: loops for as long as it is submitted. false: plays once per trigger
    uint32_t trigger;  // one-shots only: a new value starts the file again from the top
    float left, right; // gains, 0..1, before the master and SFX sliders
};

// The full set of voices wanted this tick; anything not in it fades out. Game thread.
void RsAmbienceMixer_Submit(const RsAmbienceVoice* voices, int32_t count);

// What the audio thread last mixed, for the console: voices with a gain above zero, and how many files are
// loaded.
struct RsAmbienceMixerStats {
    int32_t voices;       // voices the audio thread holds, fading ones included
    int32_t audible;      // of those, the ones it added to the last batch with a gain above zero
    int32_t loaded;       // files decoded
    uint64_t batches;     // batches mixed since start; it stops advancing if the hook is not being called
    // The keys of the voices audible in the last batch, at most 16 (the voice pool).
    int32_t audibleKeys[16];
    int32_t audibleKeyCount;
};
RsAmbienceMixerStats RsAmbienceMixer_GetStats();

// Adds the voices to `frames` stereo frames of interleaved s16 at 32 kHz. Audio thread, from OTRAudio_Thread.
void RsAmbienceMixer_Mix(int16_t* stereo, uint32_t frames);

#endif // SOH_RS_AMBIENCE_MIXER_H
