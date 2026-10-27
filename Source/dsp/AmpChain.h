// Murli — everything after the reed bar: pickup network, preamp with the LDR
// tremolo inside its feedback loop, volume, power amp and the lid speakers.
// All of it runs at the host rate with no lookahead, so the plugin reports zero latency.
#pragma once

#include "DspUtil.h"
#include "Pedals.h"

namespace murli
{

// Twin-T oscillator → LED → CdS cell → shunt on the preamp's emitter feedback node.
// The output is the preamp's closed-loop gain relative to the vibrato-off gain.
class Tremolo
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        attack  = (float) std::exp (-1.0 / (0.003 * fs));   // LED on: cell resistance drops fast
        release = (float) std::exp (-1.0 / (0.050 * fs));   // LED off: cell recovers slowly
        phase = 0.0; ldr = 0.0f;
    }

    void setRate (double hz) { inc = hz / sampleRate; }

    float lamp() const noexcept { return ldr; }

    // depth 0..1 is the VIBRATO pot position.
    float next (float depth) noexcept
    {
        phase += inc;
        if (phase >= 1.0) phase -= 1.0;

        // The twin-T runs slightly into its rails, so the sine is a little flattened.
        const double s = std::sin (kTwoPi * phase);
        const float led = (float) std::max (0.0, std::tanh (1.6 * s) / std::tanh (1.6));

        const float c = led > ldr ? attack : release;
        ldr = led + c * (ldr - led);

        // CdS cell: log-resistance follows light, ~9k lit to ~1M dark.
        const double drive = std::pow ((double) std::clamp (ldr, 0.0f, 1.0f), 0.9);
        const double rLdr = std::exp (kLogRmax + (kLogRmin - kLogRmax) * drive);

        // 50k VIBRATO pot as a divider, 18k from top to wiper, cell on the wiper.
        const double rUp = 50.0e3 * (1.0 - depth), rLo = 50.0e3 * depth;
        const double zUp = rUp * 18.0e3 / (rUp + 18.0e3 + 1.0e-9);
        const double zLo = rLo * rLdr / (rLo + rLdr + 1.0e-9);
        const double z = zUp + zLo;

        // R-10 (56k) feedback into that node; the emitter path loads it (~58.5k equivalent).
        const double zEff = z * 58.5e3 / (z + 58.5e3);
        const double gain = 1.0 + 56.0e3 / std::max (zEff, 1.0);
        return (float) (gain / kGainAtRest);
    }

private:
    static constexpr double kLogRmin = 9.10498;    // ln(9k)
    static constexpr double kLogRmax = 13.81551;   // ln(1M)
    static constexpr double kGainAtRest = 6.2;     // ≈15.9 dB closed loop with vibrato off

    double sampleRate = 48000.0, phase = 0.0, inc = 5.63 / 48000.0;
    float attack = 0, release = 0, ldr = 0;
};

struct AmpSettings
{
    float drive = 1.0f;     // linear preamp input trim
    float vibrato = 0.0f;   // 0..1
    float volume = 0.7f;    // pot position 0..1
    float speaker = 1.0f;   // 0 = direct out, 1 = lid speakers
    float noise = 0.0f;     // 0..1
};

class AmpChain
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;

        // Pickup network: the moving reed injects charge into ~240 pF of comb and cable,
        // loaded by the bias network — a high-pass near 880 Hz. This is a large part of
        // why the 200A's bass is reedy rather than boomy.
        pickupHP.setCutoff (880.0, fs);

        // Two-stage direct-coupled preamp: asymmetric headroom (≈10.9 V up vs ≈2.05 V down
        // on TR-1's collector) gives even harmonics when it is pushed.
        preClip.setRails (5.3, 1.0);
        preLP.setCutoff (11000.0, fs);
        dcBlock.setCutoff (16.0, fs);
        tremolo.prepare (fs);

        for (auto& o : out) o.prepare (fs);
        pedals.prepare (fs);
        hissLP.setCutoff (7000.0, fs);

        humPhase = 0.0;
        humInc = 60.0 / fs;

        drive.reset (fs, 0.02, 1.0f);
        vibrato.reset (fs, 0.03, 0.0f);
        volume.reset (fs, 0.02, 0.7f);
        speaker.reset (fs, 0.03, 1.0f);
        noise.reset (fs, 0.03, 0.0f);
        reset();
    }

    void reset()
    {
        pickupHP.reset(); preClip.reset(); preLP.reset(); dcBlock.reset(); hissLP.reset();
        for (auto& o : out) o.reset();
        pedals.reset();
    }

    void setTremoloRate (double hz) { tremolo.setRate (hz); }
    float lamp() const noexcept     { return tremolo.lamp(); }
    float chorusLed() const noexcept { return pedals.chorusLed(); }

    void setPedals (const PedalSettings& p) { pedals.set (p); }

    void setTargets (const AmpSettings& s)
    {
        drive.setTarget (s.drive);
        vibrato.setTarget (s.vibrato);
        volume.setTarget (s.volume);
        speaker.setTarget (s.speaker);
        noise.setTarget (s.noise);
    }

    // left[] holds the summed pickup signal on entry; left/right get the stereo output.
    void process (float* left, float* right, int n) noexcept
    {
        // With every pedal bypassed and silent the loop is a wire: run one side, copy it.
        const bool mono = pedals.transparent();

        for (int i = 0; i < n; ++i)
        {
            const float trem = tremolo.next (vibrato.next());

            // --- pickup network + preamp (tremolo modulates the loop gain) ---
            float x = pickupHP.process (left[i]) * kPickupToPreamp * drive.next();
            x = preClip.process (x * trem);
            x = dcBlock.process (preLP.process (x));

            // preamp hiss rides on the signal into the loop and the volume pot
            const float nz = noise.next();
            if (nz > 0.0f)
                x += hissLP.process (rng.next()) * 0.0009f * nz;

            // --- effects loop: SEND → pedals → RETURN ---
            // Always fed: a bypassed chorus keeps its delay chip filled and its LFO running.
            float l, r;
            pedals.process (x, l, r);

            // --- volume pot (audio taper) → power amp → lid speakers / direct ---
            const float v = volume.next();
            const float pot = v * v * (0.4f + 0.6f * v) * kPowerAmpGain;
            const float hum = nz > 0.0f ? humSample() * 0.0016f * nz : 0.0f;
            const float s = speaker.next();
            left[i] = out[0].process (l * pot, hum, s);
            right[i] = mono ? left[i] : out[1].process (r * pot, hum, s);
        }

        if (mono)
            out[1] = out[0];   // keep the right side's filters primed for when a pedal engages
    }

private:
    // Power amp, lid speakers and direct out for one side of the stereo image.
    struct OutputStage
    {
        void prepare (double fs)
        {
            // Two 4"x8" ceramic ovals in an open-backed plastic lid.
            spkHP.highPass (95.0, 0.85, fs);
            spkBody.peak (210.0, 1.0, 2.5, fs);
            spkPresence.peak (2400.0, 1.3, 3.0, fs);
            spkLP1.lowPass (6800.0, 0.75, fs);
            spkLP2.lowPass (9500.0, 0.6, fs);
            diHP.highPass (25.0, 0.7, fs);
            diLP.setCutoff (14000.0, fs);
        }
        void reset()
        {
            paRail.reset(); spkHP.reset(); spkBody.reset(); spkPresence.reset();
            spkLP1.reset(); spkLP2.reset(); diHP.reset(); diLP.reset();
        }
        float process (float x, float hum, float speakerMix) noexcept
        {
            const float a = paRail.process (x) + hum;
            const float cab = spkLP2.process (spkLP1.process (spkPresence.process (spkBody.process (spkHP.process (a)))));
            const float di  = diLP.process (diHP.process (a));
            return safetyClip ((di + speakerMix * (cab * kCabMakeup - di)) * kOutputTrim);
        }

        SoftRailADAA paRail;
        Biquad spkHP, spkBody, spkPresence, spkLP1, spkLP2, diHP;
        OnePoleLP diLP;
    };

    // Transparent below -1.4 dBFS; only a fully cranked amp ever reaches the knee.
    static float safetyClip (float x) noexcept
    {
        constexpr float knee = 0.85f;
        const float a = std::abs (x);
        if (a <= knee) return x;
        const float y = knee + (1.0f - knee) * std::tanh ((a - knee) / (1.0f - knee));
        return x < 0.0f ? -y : y;
    }

    float humSample() noexcept
    {
        humPhase += humInc;
        if (humPhase >= 1.0) humPhase -= 1.0;
        const double p = kTwoPi * humPhase;
        return (float) (std::sin (p) + 0.5 * std::sin (2 * p + 0.3) + 0.25 * std::sin (3 * p + 1.1));
    }

    // Gain staging (see tests/engine_test.cpp for the calibration renders).
    static constexpr float kPickupToPreamp = 0.55f;
    static constexpr float kPowerAmpGain   = 2.7f;
    static constexpr float kCabMakeup      = 1.15f;
    static constexpr float kOutputTrim     = 0.55f;

    double sampleRate = 48000.0, humPhase = 0.0, humInc = 0.0;
    Tremolo tremolo;
    OnePoleHP pickupHP, dcBlock;
    OnePoleLP preLP, hissLP;
    AsymClipperADAA preClip;
    OutputStage out[2];
    Pedalboard pedals;
    Smoothed drive, vibrato, volume, speaker, noise;
    Lcg rng;
};

} // namespace murli
