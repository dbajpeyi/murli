// Murli — "effects loop" mod: three stompboxes patched between the preamp and the
// volume pot, the way a tech would add a send/return to a 200A amp board.
//
//   preamp ─▶ SEND ─▶ chorus ─▶ drum echo ─▶ spring tank ─▶ RETURN ─▶ volume ─▶ power amp
//
// The loop is where the instrument turns stereo; everything downstream runs per side.
// No effect adds latency. Buffers are sized in prepare(); process() never allocates.
#pragma once

#include "DspUtil.h"
#include <vector>

namespace murli
{

// Power-of-two circular delay with 4-point Hermite reads. read() is called before
// push() for the current sample, so the newest stored sample is at delay 1.
class DelayLine
{
public:
    void prepare (int maxDelaySamples)
    {
        int size = 1;
        while (size < maxDelaySamples + 8) size <<= 1;
        buf.assign ((size_t) size, 0.0f);
        mask = size - 1;
        w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }

    void push (float x) noexcept { buf[(size_t) w] = x; w = (w + 1) & mask; }

    float read (double delay) const noexcept   // delay >= 2 samples
    {
        const double pos = (double) w - delay;
        const double fl = std::floor (pos);
        const float f = (float) (pos - fl);
        const int i = (int) fl;
        const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

private:
    float at (int i) const noexcept { return buf[(size_t) (i & mask)]; }
    std::vector<float> buf;
    int mask = 0, w = 0;
};

inline float onePoleCoeff (double seconds, double fs) { return (float) std::exp (-1.0 / (seconds * fs)); }

//==============================================================================
// Chorus after the Sea Device (EQD Sea Machine layout): a PT2399 delay whose clock
// is swept by an LM324 triangle/square LFO.
//   RATE      LFO speed (reverse-log pot, so the slow end has room)
//   SHAPE     soft triangle → hard square (the 100k-A pot sets the clipping stage's gain)
//   DEPTH     how much LFO reaches the delay clock
//   ANIMATE   the VCO's base resistor: longer base delay, so the same LFO swings the
//             pitch further — tight chorus at the bottom, seasick at the top
//   DIMENSION regeneration around the PT2399: slap at low, ambience mid, ringing at max
//   INTENSITY wet blend
// The pedal is mono; here a second read of the same delay uses the inverted LFO for the
// right side, which is the usual stereo chorus mod.
struct ChorusSettings { bool on = false; float rate = 0.8f, shape = 0.0f, depth = 0.5f, dimension = 0.15f, animate = 0.35f, intensity = 0.6f; };

class SeaChorus
{
public:
    void prepare (double fs)
    {
        sampleRate = fs;
        line.prepare ((int) (0.06 * fs));
        // PT2399 LPF1/LPF2 sections (33k / 470p-3n3 networks): the wet path rolls off ~6–7 kHz
        wetLP1.lowPass (6800.0, 0.6, fs);
        wetLP2.lowPass (6800.0, 0.6, fs);
        wetLP3.lowPass (6800.0, 0.6, fs);
        wetLP4.lowPass (6800.0, 0.6, fs);
        fbLP.setCutoff (4000.0, fs);
        fbHP.setCutoff (90.0, fs);
        lfoSmooth = onePoleCoeff (0.012, fs);   // C20 rounds the square's corners
        paramSmooth = onePoleCoeff (0.03, fs);
        engageStep = (float) (1.0 / (0.02 * fs));   // 20 ms S-curve crossfade
        reset();
    }

    void reset()
    {
        line.clear(); wetLP1.reset(); wetLP2.reset(); wetLP3.reset(); wetLP4.reset();
        fbLP.reset(); fbHP.reset(); fb = 0.0f; lfoL = lfoR = 0.0f;
    }

    void set (const ChorusSettings& s) { target = s; }

    // LFO position for the rate LED (0..1); it blinks even when bypassed, like the pedal.
    float led() const noexcept { return 0.5f + 0.5f * lfoL; }
    bool idle() const noexcept { return engaged <= 0.0f && ! target.on; }

    void process (float x, float& outL, float& outR) noexcept
    {
        // LFO always runs (the rate LED shows tempo in bypass)
        phase += target.rate / sampleRate;
        if (phase >= 1.0) phase -= 1.0;
        const float tri = (float) (phase < 0.5 ? 4.0 * phase - 1.0 : 3.0 - 4.0 * phase);
        shape += (target.shape - shape) * (1.0f - paramSmooth);
        const float g = 1.0f + 40.0f * shape * shape;   // audio-taper pot into the clipper
        const float norm = 1.0f / std::tanh (g);
        lfoL = lfoSmooth * lfoL + (1.0f - lfoSmooth) * std::tanh (g * tri) * norm;
        lfoR = lfoSmooth * lfoR + (1.0f - lfoSmooth) * -std::tanh (g * tri) * norm;

        if (idle())
        {
            line.push (x);   // keep the chip's memory current so engaging is seamless
            outL = outR = x;
            return;
        }
        if (engaged <= 0.0f)   // waking up: start the wet filters from silence
        {
            wetLP1.reset(); wetLP2.reset(); wetLP3.reset(); wetLP4.reset(); fbLP.reset(); fbHP.reset();
        }
        engaged = std::clamp (engaged + (target.on ? engageStep : -engageStep), 0.0f, 1.0f);
        const float fade = engaged * engaged * (3.0f - 2.0f * engaged);

        auto glide = [this] (float& v, float t) { v += (t - v) * (1.0f - paramSmooth); };
        glide (depth, target.depth); glide (animate, target.animate);
        glide (dimension, target.dimension); glide (intensity, target.intensity);

        // VCO: base delay set by ANIMATE, modulation proportional to it
        const double base = (0.006 + 0.024 * animate) * sampleRate;
        const double swing = base * 0.62 * depth;
        const float wl = wetLP2.process (wetLP1.process (line.read (base + swing * lfoL)));
        const float wr = wetLP4.process (wetLP3.process (line.read (base + swing * lfoR)));

        // DIMENSION: regeneration around the delay chip
        fb = fbHP.process (fbLP.process (0.5f * (wl + wr)));
        line.push (x + 0.88f * dimension * fb);

        const float wet = intensity * fade;
        const float dry = 1.0f - 0.35f * wet;
        outL = dry * x + wet * wl;
        outR = dry * x + wet * wr;
    }

private:
    double sampleRate = 48000.0, phase = 0.0;
    DelayLine line;
    Biquad wetLP1, wetLP2, wetLP3, wetLP4;
    OnePoleLP fbLP;
    OnePoleHP fbHP;
    ChorusSettings target;
    float lfoL = 0, lfoR = 0, fb = 0, engaged = 0;
    float shape = 0, depth = 0.5f, animate = 0.35f, dimension = 0.15f, intensity = 0.6f;
    float lfoSmooth = 0, paramSmooth = 0, engageStep = 0.001f;
};

//==============================================================================
// Magnetic-drum echo inspired by the Binson Echorec 2: one record head and four
// equally spaced playback heads around a spinning drum (≈75/150/225/300 ms), a
// 12-way head selector, a valve record amplifier that saturates as the swell builds,
// head-gap treble loss on every pass, and a little wow from the drum drive.
//   ECHO   the selected heads play; the last of them is fed back (single repeat line)
//   REPEAT every selected head is fed back — rhythmic patterns that regenerate
//   SWELL  all four heads are fed back whatever plays — dense, reverb-like build
struct EchoSettings { bool on = false; float time = 1.0f, swell = 0.35f, tone = 0.5f, mix = 0.4f; int heads = 3, mode = 0; };

class DrumEcho
{
public:
    static constexpr int kHeads = 4;
    static constexpr double kHeadSpacing = 0.075;   // seconds per head at nominal drum speed

    // Head combinations on the 12-position selector (bit h = playback head h+1).
    static constexpr int kSelector[12] = { 0b0001, 0b0010, 0b0100, 0b1000, 0b0011, 0b0110,
                                           0b1100, 0b0101, 0b1010, 0b0111, 0b1110, 0b1111 };

    void prepare (double fs)
    {
        sampleRate = fs;
        line.prepare ((int) (kHeadSpacing * kHeads * 1.6 * fs) + 64);
        gapLP.setCutoff (5200.0, fs);    // head gap + drum coating
        fbHP.setCutoff (110.0, fs);
        smooth = onePoleCoeff (0.012, fs);
        speedSmooth = onePoleCoeff (0.25, fs);   // the drum motor takes a moment to settle
        engageSmooth = onePoleCoeff (0.008, fs);
        reset();
    }

    void reset()
    {
        line.clear(); gapLP.reset(); fbHP.reset(); toneL.reset(); toneR.reset(); rec.reset();
        tail = 0; fbState = 0.0f;
    }

    void set (const EchoSettings& s)
    {
        target = s;
        const double toneHz = 2200.0 * std::pow (4.0, (double) s.tone);   // 2.2–8.8 kHz
        toneL.lowPass (toneHz, 0.6, sampleRate);
        toneR.lowPass (toneHz, 0.6, sampleRate);
    }

    bool idle() const noexcept { return tail <= 0 && ! target.on; }

    void process (float x, float& outL, float& outR) noexcept
    {
        engaged += ((target.on ? 1.0f : 0.0f) - engaged) * (1.0f - engageSmooth);
        if (target.on) tail = (int) (2.0 * sampleRate);
        if (idle()) { outL = outR = 0.0f; return; }

        speed += (target.time - speed) * (1.0f - speedSmooth);
        swell += (target.swell - swell) * (1.0f - smooth);
        mix += (target.mix - mix) * (1.0f - smooth);

        // drum wow and a little flutter
        wowPhase += 0.71 / sampleRate;  if (wowPhase >= 1.0) wowPhase -= 1.0;
        flutPhase += 5.3 / sampleRate;  if (flutPhase >= 1.0) flutPhase -= 1.0;
        const double wobble = 1.0 + 0.0016 * std::sin (kTwoPi * wowPhase) + 0.0004 * std::sin (kTwoPi * flutPhase);
        const double spacing = kHeadSpacing * sampleRate * (double) speed * wobble;

        const int sel = kSelector[std::clamp (target.heads, 0, 11)];
        const int last = sel & 0b1000 ? 3 : sel & 0b0100 ? 2 : sel & 0b0010 ? 1 : 0;
        int nPlay = 0;
        for (int h = 0; h < kHeads; ++h) nPlay += (sel >> h) & 1;

        float l = 0.0f, r = 0.0f, fbSum = 0.0f;
        for (int h = 0; h < kHeads; ++h)
        {
            const float y = line.read (spacing * (h + 1));
            const bool plays = (sel >> h) & 1;
            const bool feeds = target.mode == 2 ? true : target.mode == 1 ? plays : h == last;
            playGain[h] += ((plays ? 1.0f / std::sqrt ((float) nPlay) : 0.0f) - playGain[h]) * (1.0f - smooth);
            const float fbT = feeds ? (target.mode == 2 ? 0.25f : target.mode == 1 ? 1.0f / (float) nPlay : 1.0f) : 0.0f;
            fbGain[h] += (fbT - fbGain[h]) * (1.0f - smooth);
            l += y * playGain[h] * kPanL[h];
            r += y * playGain[h] * kPanR[h];
            fbSum += y * fbGain[h];
        }

        // Record amp: valve stage with a little bias asymmetry; it is what keeps the
        // swell from running away even past unity loop gain.
        const float regen = target.on ? swell * 1.08f : std::min (swell, 0.8f) * 1.08f;
        fbState = fbHP.process (gapLP.process (fbSum));
        const float drive = x * engaged + regen * fbState;
        const float recorded = (rec.process (1.4f * drive + kBias) - kBiasOut) / 1.4f;
        line.push (recorded);

        outL = toneL.process (l) * mix;
        outR = toneR.process (r) * mix;

        if (! target.on)
        {
            if (std::abs (outL) + std::abs (outR) > 1.0e-6f) tail = (int) (0.5 * sampleRate);
            else if (--tail <= 0) reset();
        }
    }

private:
    static constexpr float kPanL[kHeads] = { 0.95f, 0.45f, 0.85f, 0.55f };
    static constexpr float kPanR[kHeads] = { 0.45f, 0.95f, 0.55f, 0.85f };
    static constexpr float kBias = 0.12f;
    static constexpr float kBiasOut = 0.11942f;   // tanh(kBias)

    double sampleRate = 48000.0, wowPhase = 0.0, flutPhase = 0.0;
    DelayLine line;
    OnePoleLP gapLP;
    OnePoleHP fbHP;
    Biquad toneL, toneR;
    SoftRailADAA rec;
    EchoSettings target;
    float playGain[kHeads] = {}, fbGain[kHeads] = {};
    float speed = 1.0f, swell = 0.35f, mix = 0.4f, engaged = 0.0f, fbState = 0.0f;
    float smooth = 0, speedSmooth = 0, engageSmooth = 0;
    int tail = 0;
};

//==============================================================================
// Two-spring amp reverb tank. Each spring is a feedback loop around a delay and a
// cascade of stretched all-pass sections; the cascade delays low frequencies more
// than highs, so every round trip arrives as a descending chirp — the "drip".
// The driver transformer saturates when DWELL is high, which gives the crash on hard notes.
struct ReverbSettings { bool on = false; float dwell = 0.5f, decay = 0.5f, tone = 0.5f, mix = 0.35f; };

class SpringTank
{
public:
    static constexpr int kStages = 60;

    void prepare (double fs)
    {
        sampleRate = fs;
        // stretch factor puts the chirp's transition band near 4 kHz at any sample rate
        stretch = std::max (1, (int) std::lround (fs / 8000.0));
        for (int s = 0; s < 2; ++s)
        {
            springs[s].ap.assign ((size_t) (kStages * stretch), 0.0f);
            springs[s].line.prepare ((int) (0.06 * fs));
            springs[s].loopLP.setCutoff (4300.0, fs);
            springs[s].loopHP.setCutoff (60.0, fs);
        }
        springs[0].delay = 0.0353 * fs; springs[0].a = -0.62f;
        springs[1].delay = 0.0417 * fs; springs[1].a = -0.58f;
        driveHP.setCutoff (250.0, fs);    // the tank's driver can't do bass
        outHP.setCutoff (180.0, fs);
        outHPR.setCutoff (180.0, fs);
        smooth = onePoleCoeff (0.02, fs);
        engageSmooth = onePoleCoeff (0.008, fs);
        reset();
        set (target);
    }

    void reset()
    {
        for (auto& s : springs)
        {
            std::fill (s.ap.begin(), s.ap.end(), 0.0f);
            s.line.clear(); s.loopLP.reset(); s.loopHP.reset(); s.y = 0.0f;
        }
        driveHP.reset(); outHP.reset(); outHPR.reset(); toneL.reset(); toneR.reset(); driver.reset();
        tail = 0;
    }

    void set (const ReverbSettings& s)
    {
        target = s;
        const double toneHz = 2500.0 * std::pow (3.0, (double) s.tone);   // 2.5–7.5 kHz
        toneL.lowPass (toneHz, 0.65, sampleRate);
        toneR.lowPass (toneHz, 0.65, sampleRate);
        // RT60 from 1.2 s to 4.5 s; the loop length includes the chirp's mean delay
        const double rt60 = 1.2 * std::pow (3.75, (double) s.decay);
        for (auto& sp : springs)
        {
            const double loop = sp.delay + 0.5 * kStages * stretch * (1.0 - sp.a) / (1.0 + sp.a);
            sp.gain = (float) std::pow (10.0, -3.0 * loop / (rt60 * sampleRate));
        }
    }

    bool idle() const noexcept { return tail <= 0 && ! target.on; }

    void process (float x, float& outL, float& outR) noexcept
    {
        engaged += ((target.on ? 1.0f : 0.0f) - engaged) * (1.0f - engageSmooth);
        if (target.on) tail = (int) (2.0 * sampleRate);
        if (idle()) { outL = outR = 0.0f; return; }

        dwell += (target.dwell - dwell) * (1.0f - smooth);
        mix += (target.mix - mix) * (1.0f - smooth);

        // driver: DWELL is the drive into the tank's transducer
        const float d = 0.6f + 5.0f * dwell * dwell;
        const float in = driver.process (driveHP.process (x * engaged) * d) * 0.5f;

        float wet[2];
        for (int s = 0; s < 2; ++s)
        {
            auto& sp = springs[s];
            float v = in + sp.gain * sp.loopHP.process (sp.loopLP.process (sp.line.read (sp.delay)));
            // dispersion: kStages stretched all-passes (a + z^-K) / (1 + a z^-K)
            float* st = sp.ap.data() + apIndex;
            for (int k = 0; k < kStages; ++k, st += stretch)
            {
                const float old = *st;
                const float w = v - sp.a * old;
                v = sp.a * w + old;
                *st = w;
            }
            sp.line.push (v);
            sp.y = v;
            wet[s] = v;
        }
        apIndex = (apIndex + 1) % stretch;

        const float l = outHP.process (wet[0] + 0.3f * wet[1]);
        const float r = wet[1] + 0.3f * wet[0];
        outL = toneL.process (l) * mix * kWetGain;
        outR = toneR.process (outHPR.process (r)) * mix * kWetGain;

        if (! target.on)
        {
            if (std::abs (outL) + std::abs (outR) > 1.0e-6f) tail = (int) (0.5 * sampleRate);
            else if (--tail <= 0) reset();
        }
    }

private:
    struct Spring
    {
        std::vector<float> ap;   // kStages delay cells of `stretch` samples, interleaved
        DelayLine line;
        OnePoleLP loopLP;
        OnePoleHP loopHP;
        double delay = 1000.0;
        float a = -0.6f, gain = 0.5f, y = 0.0f;
    };

    static constexpr float kWetGain = 0.9f;
    double sampleRate = 48000.0;
    int stretch = 6, apIndex = 0, tail = 0;
    Spring springs[2];
    OnePoleHP driveHP, outHP, outHPR;
    Biquad toneL, toneR;
    SoftRailADAA driver;
    ReverbSettings target;
    float dwell = 0.5f, mix = 0.35f, engaged = 0.0f, smooth = 0, engageSmooth = 0;
};

//==============================================================================
struct PedalSettings
{
    ChorusSettings chorus;
    EchoSettings echo;
    ReverbSettings reverb;
};

class Pedalboard
{
public:
    void prepare (double fs) { chorus.prepare (fs); echo.prepare (fs); reverb.prepare (fs); }
    void reset() { chorus.reset(); echo.reset(); reverb.reset(); }

    void set (const PedalSettings& s) { chorus.set (s.chorus); echo.set (s.echo); reverb.set (s.reverb); }

    // True when the loop is a straight wire this sample (lets the amp skip the right side).
    bool transparent() const noexcept { return chorus.idle() && echo.idle() && reverb.idle(); }

    float chorusLed() const noexcept { return chorus.led(); }

    void process (float x, float& outL, float& outR) noexcept
    {
        float l, r;
        chorus.process (x, l, r);

        // echo is mono in (both sides of the chorus), wet added on top of each side
        float el, er;
        echo.process (0.5f * (l + r), el, er);
        l += el; r += er;

        float rl, rr;
        reverb.process (0.5f * (l + r), rl, rr);
        outL = l + rl;
        outR = r + rr;
    }

private:
    SeaChorus chorus;
    DrumEcho echo;
    SpringTank reverb;
};

} // namespace murli
