// Murli — reed, hammer and damper model (one voice per key, like the real action).
//
// A 200A reed is a clamped steel cantilever with a solder tuning mass at its tip.
// It rings as a handful of inharmonic bending modes, each an exponentially decaying
// sinusoid. The hammer is a short felt pulse: the shorter the contact (harder blow),
// the more the upper modes are excited. Keys hold their reed state between strikes,
// so repeated notes re-excite a reed that is still moving, just like the instrument.
#pragma once

#include "PickupLaw.h"
#include <array>

namespace murli
{

constexpr int kNumModes = 4;
constexpr int kLowestNote = 21, kHighestNote = 108;   // 200A is 33..96; outside that we extrapolate
constexpr int kNumKeys = kHighestNote - kLowestNote + 1;

// Mode ratios f_m / f_1 of a cantilever with tip mass ratio mu, from
//   1 + cos(L)cosh(L) + mu L (cos(L)sinh(L) - sin(L)cosh(L)) = 0,  ratio = (L_m / L_1)^2
inline std::array<double, kNumModes> cantileverRatios (double mu)
{
    auto g = [mu] (double L)
    {
        return 1.0 + std::cos (L) * std::cosh (L)
             + mu * L * (std::cos (L) * std::sinh (L) - std::sin (L) * std::cosh (L));
    };
    std::array<double, kNumModes> roots {};
    int found = 0;
    double prevL = 0.05, prevG = g (prevL);
    for (double L = 0.06; L < 20.0 && found < kNumModes; L += 0.01)
    {
        const double gl = g (L);
        if ((gl > 0) != (prevG > 0))
        {
            double lo = prevL, hi = L, glo = prevG;
            for (int i = 0; i < 60; ++i)
            {
                const double mid = 0.5 * (lo + hi), gm = g (mid);
                if ((gm > 0) == (glo > 0)) { lo = mid; glo = gm; } else hi = mid;
            }
            roots[(size_t) found++] = 0.5 * (lo + hi);
        }
        prevL = L; prevG = gl;
    }
    std::array<double, kNumModes> r {};
    for (int m = 0; m < kNumModes; ++m)
        r[(size_t) m] = (roots[(size_t) m] * roots[(size_t) m]) / (roots[0] * roots[0]);
    return r;
}

// Solder mass relative to reed mass: heaviest on the bass reeds, nearly gone at the top.
inline double tipMassRatio (int note)
{
    return std::clamp (0.16 - 0.0024 * (note - 33), 0.0, 0.2);
}

inline double interpTable (const double (*tab)[2], int n, double x)
{
    if (x <= tab[0][0]) return tab[0][1];
    for (int i = 1; i < n; ++i)
        if (x <= tab[i][0])
        {
            const double t = (x - tab[i - 1][0]) / (tab[i][0] - tab[i - 1][0]);
            return tab[i - 1][1] + t * (tab[i][1] - tab[i - 1][1]);
        }
    return tab[n - 1][1];
}

// Everything about a key that does not depend on how hard it is struck.
struct KeyModel
{
    double f0 = 440.0;
    std::array<double, kNumModes> ratio {}, baseAmp {};
    std::array<double, kNumModes> cosInc {}, sinInc {}, decay {}, damp {};
    double swingFF = 1.0;     // reed swing at the pickup for a fortissimo blow (mm)
    double velExponent = 1.5;
    int    chebDegree = 8;
    double chebRest = 0.0;    // truncated-series value at rest, subtracted so silence is 0
    float  sensitivity = 1.0f; // per-register pickup coupling (bigger bass reeds couple more)
    bool   hasDamper = true;
    float  noiseB0 = 0, noiseA1 = 0, noiseA2 = 0;   // hammer thump band-pass

    // `modeRatios` comes from cantileverRatios(tipMassRatio(note)); it is cached by the
    // caller because the root search is too slow to redo on every tuning change.
    void build (int note, double a4, double fs, const PickupLaw& law,
                const std::array<double, kNumModes>& modeRatios)
    {
        const double detune = 1.0 + hashUnit ((uint32_t) note) * 0.00173;   // ±3 cents
        f0 = a4 * std::pow (2.0, (note - 69) / 12.0) * detune;

        ratio = modeRatios;

        // Upper-mode levels relative to the fundamental, from 200A recordings
        // (real reeds sit far below ideal-beam theory).
        static constexpr double amps[kNumModes] = { 1.0, 0.0065, 0.0040, 0.0020 };
        for (int m = 0; m < kNumModes; ++m)
            baseAmp[(size_t) m] = amps[m] * (1.0 + 0.08 * hashUnit ((uint32_t) note, (uint32_t) m + 1));

        // Decay: ~3 dB/s floor in the bass, ~0.005 f^1.22 dB/s up the keyboard;
        // higher modes lose energy ∝ ω² (internal friction).
        const double baseDb = std::max (3.0, 0.005 * std::pow (f0, 1.22));
        // Dampers: felt pads, slower to stop the heavy bass reeds.
        const double dampDb = 160.0 + 420.0 * std::clamp ((note - 33) / 60.0, 0.0, 1.0);
        hasDamper = note < 92;   // the top keys of a 200A ring free

        for (int m = 0; m < kNumModes; ++m)
        {
            const double f = f0 * ratio[(size_t) m];
            const double w = kTwoPi * f / fs;
            const bool audible = f < 0.45 * fs;
            cosInc[(size_t) m] = std::cos (w);
            sinInc[(size_t) m] = std::sin (w);
            const double r2 = ratio[(size_t) m] * ratio[(size_t) m];
            decay[(size_t) m] = audible ? decayMultiplier (std::min (baseDb * r2, 600.0), fs) : 0.0;
            damp[(size_t) m] = decayMultiplier (dampDb * (m == 0 ? 1.0 : 2.0), fs);
            if (! audible) baseAmp[(size_t) m] = 0.0;
        }

        // Fortissimo swing at the pickup (mm). Bass and mid reeds are long and swing far;
        // the treble reeds are short and stiff. The step at F3 is the reed-bar section change.
        static constexpr double swingTab[][2] = {
            { 21, 0.95 }, { 33, 0.99 }, { 48, 0.95 }, { 52, 0.88 }, { 53, 1.22 },
            { 60, 1.19 }, { 72, 0.82 }, { 84, 0.45 }, { 96, 0.22 }, { 108, 0.12 } };
        swingFF = interpTable (swingTab, 10, note);

        // Pickup coupling per register: the long, wide bass reeds present more area to
        // the comb, and the treble reeds sit closer to it, so neither end is as weak
        // as the swing alone suggests.
        static constexpr double sensTab[][2] = {
            { 21, 12.0 }, { 33, 11.0 }, { 39, 9.0 }, { 45, 6.0 }, { 51, 5.0 }, { 53, 1.5 }, { 57, 0.8 },
            { 63, 0.0 }, { 69, 0.0 }, { 75, 1.2 }, { 81, 2.5 }, { 87, 4.5 }, { 93, 6.0 }, { 108, 7.0 } };
        sensitivity = (float) dbToGain (interpTable (sensTab, 14, note));

        // Key leverage: the mid keyboard has the widest dynamic range.
        const double d = (note - 62) / 15.0;
        velExponent = 1.3 + 0.4 * std::exp (-0.5 * d * d);

        // Highest pickup harmonic kept below Nyquist (see PickupLaw).
        chebDegree = std::clamp ((int) (0.46 * fs / f0), 1, PickupLaw::kMaxDegree);
        chebRest = law.eval (0.0, chebDegree);

        // Hammer thump: felt-on-steel, centred a few harmonics up, Q ~0.7.
        const double fc = std::clamp (5.0 * f0, 200.0, 2000.0);
        const double w = kTwoPi * fc / fs, al = std::sin (w) / (2.0 * 0.7);
        noiseB0 = (float) (al / (1.0 + al));
        noiseA1 = (float) (-2.0 * std::cos (w) / (1.0 + al));
        noiseA2 = (float) ((1.0 - al) / (1.0 + al));
    }
};

struct VoiceParams
{
    float bark = 1.0f;     // scales reed swing into the pickup band
    float attack = 1.0f;   // upper modes + hammer thump
};

class ReedVoice
{
public:
    bool active = false;
    bool keyDown = false;
    bool damping = false;

    void strike (const KeyModel& k, double fs, float velocity, uint32_t seed)
    {
        const double vel = std::clamp ((double) velocity, 0.0, 1.0);
        const double v = std::pow (vel, k.velExponent);

        // A hammer landing on a moving reed absorbs part of its motion.
        if (active)
            for (int m = 0; m < kNumModes; ++m) { zr[(size_t) m] *= 0.45; zi[(size_t) m] *= 0.45; }

        // Felt contact lasts ~3/4 (ff) to 1 (pp) period; a Gaussian force pulse
        // filters the upper modes smoothly (no comb nulls).
        const double tDwell = std::clamp ((0.75 + 0.25 * (1.0 - vel)) / k.f0, 0.0003, 0.02);
        const double sigma2 = 64.0;
        const double ref = std::exp (-(k.f0 * tDwell) * (k.f0 * tDwell) / (2.0 * sigma2));
        for (int m = 0; m < kNumModes; ++m)
        {
            const double a = k.f0 * k.ratio[(size_t) m] * tDwell;
            const double filt = m == 0 ? 1.0 : std::exp (-a * a / (2.0 * sigma2)) / ref;
            target[(size_t) m] = k.swingFF * v * k.baseAmp[(size_t) m] * filt;
            er[(size_t) m] = 1.0; ei[(size_t) m] = 0.0;   // reed starts at rest: sine phase
        }

        // The reed builds up over the contact: ~1 period at ff, ~2 at pp (min 2 ms).
        const double periods = 1.0 + (1.0 - vel);
        onsetLength = std::max (1, (int) (std::max (periods / k.f0, 0.002) * fs));
        onsetPos = 0;
        onsetSoft = (float) (1.0 - vel);
        prevOnset = 0.0;

        noiseAmp = (float) (0.025 * vel * vel * k.swingFF);
        noiseEnv = 1.0f;
        noiseDecay = (float) std::exp (-1.0 / (0.003 * fs));
        noiseSamples = (int) (0.02 * fs);
        rng.state = seed * 747796405u + 2891336453u;
        n1 = n2 = 0.0f;

        damping = false;
        dampRamp = 0.0;
        keyDown = true;
        active = true;
    }

    void release (bool sustainHeld)
    {
        keyDown = false;
        if (! sustainHeld) damping = true;
    }

    void sustainReleased()
    {
        if (! keyDown) damping = true;
    }

    void kill() { active = false; zr.fill (0.0); zi.fill (0.0); }

    // Adds this reed's pickup signal into out[0..n).
    void render (float* out, int n, const KeyModel& k, const PickupLaw& law, const VoiceParams& p)
    {
        if (! active) return;

        constexpr int kSub = 32;   // coefficients held constant for 32 samples
        const double linGain = 0.6 * p.attack;
        const double invRange = 1.0 / PickupLaw::kRangeMm;
        const double bark = p.bark;
        const int degree = k.chebDegree;

        for (int start = 0; start < n; start += kSub)
        {
            const int len = std::min (kSub, n - start);

            double rc[kNumModes], rs[kNumModes];   // per-mode rotation × decay
            for (int m = 0; m < kNumModes; ++m)
            {
                double d = k.decay[(size_t) m];
                if (damping && k.hasDamper)
                    d *= std::pow (k.damp[(size_t) m], dampRamp);   // felt settles over ~10 ms
                rc[m] = d * k.cosInc[(size_t) m];
                rs[m] = d * k.sinInc[(size_t) m];
            }
            if (damping && k.hasDamper)
                dampRamp = std::min (1.0, dampRamp + (double) len / (0.010 * sampleRate));

            float disp[kSub], lin[kSub];
            for (int i = 0; i < len; ++i)
            {
                // Hammer contact: feed the rising part of the onset envelope into each mode.
                if (onsetPos < onsetLength)
                {
                    ++onsetPos;
                    const double ph = kPi * onsetPos / onsetLength;
                    const double raised = 0.5 * (1.0 - std::cos (ph));
                    const double env = raised * (raised * onsetSoft + (1.0 - onsetSoft));
                    const double dEnv = env - prevOnset;
                    prevOnset = env;
                    for (size_t m = 0; m < kNumModes; ++m)
                    {
                        const double g = target[m] * dEnv;
                        zr[m] += g * er[m];
                        zi[m] += g * ei[m];
                        const double c = k.cosInc[m], s = k.sinInc[m];
                        const double nr = er[m] * c - ei[m] * s;
                        ei[m] = er[m] * s + ei[m] * c;
                        er[m] = nr;
                    }
                }

                const double x0 = zi[0];
                double upper = 0.0;
                for (size_t m = 1; m < kNumModes; ++m)
                    upper += zi[m];

                for (size_t m = 0; m < kNumModes; ++m)
                {
                    const double nr = zr[m] * rc[m] - zi[m] * rs[m];
                    zi[m] = zr[m] * rs[m] + zi[m] * rc[m];
                    zr[m] = nr;
                }

                float thump = 0.0f;
                if (noiseSamples > 0)
                {
                    --noiseSamples;
                    const float w = rng.next() * noiseAmp * noiseEnv;
                    noiseEnv *= noiseDecay;
                    const float y = k.noiseB0 * w + n1;   // band-pass, TDF-II
                    n1 = -k.noiseA1 * y + n2;
                    n2 = -k.noiseB0 * w - k.noiseA2 * y;
                    thump = y;
                }

                disp[i] = (float) std::clamp (x0 * bark * invRange, -1.0, 1.0);
                lin[i] = (float) (linGain * (upper * bark + (double) thump));
            }

            // The fundamental's swing goes through the pickup law; the faint upper
            // modes and the hammer thump ride on its small-signal slope.
            law.evalBlock<kSub> (disp, len, degree);
            const float rest = (float) k.chebRest;
            float* dst = out + start;
            for (int i = 0; i < len; ++i)
                dst[i] += (disp[i] - rest) * k.sensitivity + lin[i];
        }

        // Retire the voice once it is ~100 dB below a fortissimo swing.
        if (onsetPos >= onsetLength && noiseSamples <= 0)
        {
            double e = 0.0;
            for (size_t m = 0; m < kNumModes; ++m) e += zr[m] * zr[m] + zi[m] * zi[m];
            if (e < 1.0e-10) kill();
        }
    }

    void setSampleRate (double fs) { sampleRate = fs; }

private:
    // Mode m is the complex phasor (zr, zi); the reed displacement is its imaginary part.
    std::array<double, kNumModes> zr {}, zi {}, er {}, ei {};
    std::array<double, kNumModes> target {};
    int onsetLength = 1, onsetPos = 1;
    float onsetSoft = 0.0f;
    double prevOnset = 0.0, dampRamp = 0.0, sampleRate = 48000.0;

    float noiseAmp = 0, noiseEnv = 0, noiseDecay = 0, n1 = 0, n2 = 0;
    int noiseSamples = 0;
    Lcg rng;
};

} // namespace murli
