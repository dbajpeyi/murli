// Murli — the whole instrument: 88 reeds summed on one pickup, then the amp chain.
// No heap allocation after prepare(); every call below is real-time safe.
#pragma once

#include "Reed.h"
#include "AmpChain.h"

namespace murli
{

struct EngineParams
{
    float volume = 0.7f;     // 0..1
    float vibrato = 0.0f;    // 0..1
    float speed = 5.63f;     // Hz
    float bark = 1.0f;       // 0.4..1.5
    float driveDb = 0.0f;    // -12..+12
    float attack = 1.0f;     // 0..2
    float speaker = 1.0f;    // 0..1
    float noise = 0.0f;      // 0..1
    float tuneA4 = 440.0f;   // Hz
};

class Engine
{
public:
    Engine()
    {
        for (int i = 0; i < kNumKeys; ++i)
            ratios[(size_t) i] = cantileverRatios (tipMassRatio (kLowestNote + i));
    }

    void prepare (double fs)
    {
        sampleRate = fs;
        builtA4 = -1.0;
        rebuildKeys (params.tuneA4);
        for (auto& v : voices) { v.kill(); v.setSampleRate (fs); }
        amp.prepare (fs);
        barkNow = params.bark;
        attackNow = params.attack;
        applyParams();
    }

    void setParams (const EngineParams& p)
    {
        params = p;
        applyParams();
    }

    void noteOn (int note, float velocity)
    {
        if (note < kLowestNote || note > kHighestNote) return;
        if (velocity <= 0.0f) { noteOff (note); return; }
        const int k = note - kLowestNote;
        voices[(size_t) k].strike (keys[(size_t) k], sampleRate, velocity, ++strikeCount);
    }

    void noteOff (int note)
    {
        if (note < kLowestNote || note > kHighestNote) return;
        voices[(size_t) (note - kLowestNote)].release (sustain);
    }

    void setSustain (bool down)
    {
        if (sustain && ! down)
            for (auto& v : voices) v.sustainReleased();
        sustain = down;
    }

    void allNotesOff()
    {
        sustain = false;
        for (auto& v : voices) { v.release (false); }
    }

    void panic()
    {
        sustain = false;
        for (auto& v : voices) v.kill();
        amp.reset();
    }

    // Renders n mono samples into out (any n).
    void render (float* out, int n) noexcept
    {
        std::fill (out, out + n, 0.0f);
        // Bark and attack act inside every voice; glide them per call to avoid steps.
        const float glide = std::min (1.0f, (float) n / (float) (0.01 * sampleRate));
        barkNow += (params.bark - barkNow) * glide;
        attackNow += (params.attack - attackNow) * glide;
        const VoiceParams vp { barkNow, attackNow };
        int count = 0;
        for (int k = 0; k < kNumKeys; ++k)
        {
            auto& v = voices[(size_t) k];
            if (! v.active) continue;
            v.render (out, n, keys[(size_t) k], law, vp);
            ++count;
        }
        activeVoices = count;
        amp.process (out, n);
    }

    float lamp() const noexcept       { return amp.lamp(); }
    int   voicesActive() const noexcept { return activeVoices; }

private:
    void applyParams()
    {
        if (std::abs (params.tuneA4 - builtA4) > 1.0e-3)
            rebuildKeys (params.tuneA4);

        AmpSettings s;
        s.drive = (float) dbToGain (params.driveDb);
        s.vibrato = params.vibrato;
        s.volume = params.volume;
        s.speaker = params.speaker;
        s.noise = params.noise;
        amp.setTargets (s);
        amp.setTremoloRate (params.speed);
    }

    void rebuildKeys (double a4)
    {
        for (int i = 0; i < kNumKeys; ++i)
            keys[(size_t) i].build (kLowestNote + i, a4, sampleRate, law, ratios[(size_t) i]);
        builtA4 = a4;
    }

    double sampleRate = 48000.0, builtA4 = -1.0;
    EngineParams params;
    PickupLaw law;
    std::array<std::array<double, kNumModes>, kNumKeys> ratios {};
    std::array<KeyModel, kNumKeys> keys {};
    std::array<ReedVoice, kNumKeys> voices {};
    AmpChain amp;
    float barkNow = 1.0f, attackNow = 1.0f;
    bool sustain = false;
    uint32_t strikeCount = 0;
    int activeVoices = 0;
};

} // namespace murli
