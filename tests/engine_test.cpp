// Offline checks and calibration renders for the Murli engine (no JUCE needed).
//   build:  cmake -B build-tests -S tests && cmake --build build-tests
//   run:    ./build-tests/murli_engine_test [output-dir-for-wavs]
#include "../Source/dsp/MurliEngine.h"

#include <chrono>
#include <complex>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace murli;

static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("FAIL: " __VA_ARGS__); std::printf ("\n"); } } while (0)

struct Event { double time; int note; float vel; };   // vel 0 = note off

static std::vector<float> render (double fs, const EngineParams& p, const std::vector<Event>& ev,
                                  double seconds, int block = 64)
{
    Engine e;
    e.prepare (fs);
    e.setParams (p);
    const int total = (int) (seconds * fs);
    std::vector<float> out ((size_t) total, 0.0f);
    size_t next = 0;
    int pos = 0;
    while (pos < total)
    {
        while (next < ev.size() && (int) (ev[next].time * fs) <= pos)
        {
            if (ev[next].vel > 0) e.noteOn (ev[next].note, ev[next].vel);
            else e.noteOff (ev[next].note);
            ++next;
        }
        int end = std::min (pos + block, total);
        if (next < ev.size()) end = std::min (end, std::max (pos + 1, (int) (ev[next].time * fs)));
        e.render (out.data() + pos, end - pos);   // sample-accurate split at events
        pos = end;
    }
    return out;
}

static double peakOf (const std::vector<float>& x, size_t from = 0, size_t to = SIZE_MAX)
{
    double p = 0;
    for (size_t i = from; i < std::min (to, x.size()); ++i) p = std::max (p, (double) std::abs (x[i]));
    return p;
}

static double db (double v) { return 20.0 * std::log10 (std::max (v, 1e-12)); }

// Amplitude of frequency f over x[from, from+len) with a Hann window.
static double toneAmp (const std::vector<float>& x, size_t from, size_t len, double f, double fs)
{
    std::complex<double> acc = 0;
    for (size_t i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (kTwoPi * (double) i / (double) len);
        acc += w * (double) x[from + i] * std::polar (1.0, -kTwoPi * f * (double) i / fs);
    }
    return 4.0 * std::abs (acc) / (double) len;
}

static void writeWav (const std::string& path, const std::vector<float>& x, int fs)
{
    std::ofstream f (path, std::ios::binary);
    auto u32 = [&] (uint32_t v) { f.write ((const char*) &v, 4); };
    auto u16 = [&] (uint16_t v) { f.write ((const char*) &v, 2); };
    const uint32_t bytes = (uint32_t) x.size() * 2;
    f.write ("RIFF", 4); u32 (36 + bytes); f.write ("WAVEfmt ", 8);
    u32 (16); u16 (1); u16 (1); u32 ((uint32_t) fs); u32 ((uint32_t) fs * 2); u16 (2); u16 (16);
    f.write ("data", 4); u32 (bytes);
    for (float s : x) { const int16_t v = (int16_t) std::lrint (std::clamp (s, -1.0f, 1.0f) * 32767.0f); f.write ((const char*) &v, 2); }
}

static double noteHz (int n) { return 440.0 * std::pow (2.0, (n - 69) / 12.0); }

int main (int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1] : "";
    const double fs = 48000.0;

    // --- reed physics sanity -------------------------------------------------
    {
        auto bare = cantileverRatios (0.0);
        std::printf ("bare cantilever ratios: %.3f %.3f %.3f\n", bare[1], bare[2], bare[3]);
        CHECK (std::abs (bare[1] - 6.267) < 0.01, "bare beam mode-2 ratio %.3f", bare[1]);
        CHECK (std::abs (bare[2] - 17.55) < 0.05, "bare beam mode-3 ratio %.3f", bare[2]);
        for (int n : { 33, 45, 60, 72, 96 })
        {
            auto r = cantileverRatios (tipMassRatio (n));
            std::printf ("  note %3d  mu=%.3f  ratios %.2f %.2f %.2f\n", n, tipMassRatio (n), r[1], r[2], r[3]);
        }
    }

    // --- pickup: bark grows with velocity, alias-free top ---------------------
    EngineParams dry;   // direct out: look at the instrument, not the lid speakers
    dry.speaker = 0.0f;
    std::printf ("\nC4 pickup harmonics (direct out):\n");
    double prevRatio = -200;
    for (float v : { 0.16f, 0.5f, 0.79f, 1.0f })
    {
        auto x = render (fs, dry, { { 0.0, 60, v } }, 0.6);
        const size_t from = (size_t) (0.2 * fs), len = 16384;
        const double f0 = noteHz (60);
        const double h1 = toneAmp (x, from, len, f0, fs);
        const double h2 = toneAmp (x, from, len, 2 * f0, fs);
        const double h3 = toneAmp (x, from, len, 3 * f0, fs);
        std::printf ("  vel %3d  H1 %6.1f dBFS  H2/H1 %6.1f dB  H3/H1 %6.1f dB  peak %6.1f dBFS\n",
                     (int) std::lrint (v * 127), db (h1), db (h2 / h1), db (h3 / h1), db (peakOf (x)));
        CHECK (db (h2 / h1) > prevRatio - 0.5, "bark should grow with velocity");
        prevRatio = db (h2 / h1);
    }

    {
        // Top note, hardest blow, 44.1 kHz: everything that isn't a harmonic of the
        // reed modes must be far down (aliases of the pickup harmonics would show here).
        const double fs441 = 44100.0;
        EngineParams p = dry; p.bark = 1.5f; p.attack = 0.0f;
        auto x = render (fs441, p, { { 0.0, 96, 1.0f } }, 0.5);
        const size_t from = (size_t) (0.1 * fs441), len = 8192;
        const double f0 = noteHz (96) * (1.0 + hashUnit (96) * 0.00173);
        double harm = 0, worstAlias = 0;
        for (int h = 1; h * f0 < fs441 / 2; ++h) harm = std::max (harm, toneAmp (x, from, len, h * f0, fs441));
        for (int h = 2; h < 40; ++h)
        {
            const double f = std::fmod (h * f0, fs441);
            const double folded = f > fs441 / 2 ? fs441 - f : f;
            if (h * f0 < fs441 / 2) continue;
            worstAlias = std::max (worstAlias, toneAmp (x, from, len, folded, fs441));
        }
        std::printf ("\nC7 ff @44.1k: strongest alias %.1f dB below the strongest harmonic\n", db (harm / worstAlias));
        CHECK (db (harm / worstAlias) > 70.0, "aliasing too high");
    }

    // --- keyboard balance: RMS of the first half second, mf -----------------------------
    {
        std::printf ("\nKeyboard balance at mf (RMS 0-0.5 s, dB re C4):\n  note   direct  speaker\n");
        EngineParams spk;
        auto rmsOf = [&] (const EngineParams& p, int n)
        {
            auto x = render (fs, p, { { 0.0, n, 0.7f } }, 0.5);
            double e = 0; for (float v : x) e += (double) v * v;
            return std::sqrt (e / (double) x.size());
        };
        const double refD = rmsOf (dry, 60), refS = rmsOf (spk, 60);
        for (int n = 33; n <= 96; n += 6)
            std::printf ("  %3d   %6.1f   %6.1f\n", n, db (rmsOf (dry, n) / refD), db (rmsOf (spk, n) / refS));
    }

    // --- gain staging -----------------------------------------------------------
    EngineParams def;
    std::printf ("\nPeaks at default settings (vol 0.7, lid speakers):\n");
    for (int n : { 33, 45, 57, 69, 81, 96 })
    {
        auto x = render (fs, def, { { 0.0, n, 0.8f } }, 1.0);
        std::printf ("  note %3d mf: %6.1f dBFS\n", n, db (peakOf (x)));
    }
    {
        std::vector<Event> chord;
        for (int n : { 48, 52, 55, 59, 62, 64 }) chord.push_back ({ 0.0, n, 1.0f });
        auto x = render (fs, def, chord, 1.5);
        std::printf ("  6-note ff chord: %6.1f dBFS\n", db (peakOf (x)));
        EngineParams loud = def; loud.volume = 1.0f; loud.driveDb = 12.0f;
        auto y = render (fs, loud, chord, 1.5);
        std::printf ("  6-note ff chord, vol 10, drive +12: %6.1f dBFS\n", db (peakOf (y)));
        CHECK (peakOf (y) <= 1.0, "output must stay inside full scale");
    }

    // --- dampers, release and voice retirement ---------------------------------------
    {
        auto x = render (fs, def, { { 0.0, 60, 0.8f }, { 0.5, 60, 0.0f } }, 1.5);
        const double before = peakOf (x, (size_t) (0.4 * fs), (size_t) (0.5 * fs));
        const double after = peakOf (x, (size_t) (0.9 * fs), (size_t) (1.0 * fs));
        std::printf ("\nDamper on C4: %.1f dB drop within 400 ms of key-up\n", db (before / after));
        CHECK (db (before / after) > 40.0, "damper too weak");

        Engine e; e.prepare (fs); e.setParams (def);
        std::vector<float> buf (256);
        for (int n = 33; n <= 96; ++n) e.noteOn (n, 1.0f);
        for (int i = 0; i < 20; ++i) e.render (buf.data(), 256);
        for (int n = 33; n <= 96; ++n) e.noteOff (n);
        for (int i = 0; i < (int) (3.0 * fs / 256); ++i) e.render (buf.data(), 256);
        std::printf ("voices still sounding 3 s after releasing all keys: %d (undamped top keys allowed)\n", e.voicesActive());
        CHECK (e.voicesActive() <= 5, "voices not retired");
    }

    // --- re-striking ringing reeds must not click (sustain-pedal chords) -------------------
    {
        Engine e; e.prepare (fs); e.setParams (dry); e.setSustain (true);
        std::vector<float> x ((size_t) fs);
        const int chord[] = { 48, 52, 55, 59, 62, 64, 67, 71 };
        const int t1 = (int) (0.6 * fs);
        for (int n : chord) e.noteOn (n, 0.85f);
        e.render (x.data(), t1);
        for (int n : chord) { e.noteOff (n); e.noteOn (n, 0.85f); }   // pedal holds, keys re-struck
        e.render (x.data() + t1, (int) x.size() - t1);
        auto d2 = [&] (int a, int b)   // largest second difference: a step shows up here
        {
            double m = 0;
            for (int i = a; i < b; ++i) m = std::max (m, (double) std::abs (x[(size_t) i] - 2 * x[(size_t) i - 1] + x[(size_t) i - 2]));
            return m;
        };
        const double ratio = d2 (t1, t1 + (int) (0.004 * fs)) / d2 ((int) (0.3 * fs), (int) (0.55 * fs));
        std::printf ("\nRe-strike under sustain: transient %.1fx steady ringing\n", ratio);
        CHECK (ratio < 3.0, "re-striking a ringing reed clicks (%.1fx)", ratio);
    }

    // --- NaN / denormal / extreme settings --------------------------------------------
    {
        EngineParams p; p.volume = 1; p.vibrato = 1; p.speed = 9; p.bark = 1.5f; p.driveDb = 12; p.attack = 2; p.noise = 1;
        std::vector<Event> ev;
        for (int i = 0; i < 400; ++i) ev.push_back ({ i * 0.005, 21 + (i * 37) % 88, (float) ((i * 13) % 127 + 1) / 127.0f });
        auto x = render (fs, p, ev, 3.0, 37);
        bool finite = true;
        for (float s : x) finite &= std::isfinite (s);
        CHECK (finite, "non-finite output");
        std::printf ("\nstress render peak: %.1f dBFS, finite: %s\n", db (peakOf (x)), finite ? "yes" : "NO");
    }

    // --- CPU ------------------------------------------------------------------
    {
        for (int voicesHeld : { 10, 64 })
        {
            Engine e; e.prepare (fs); e.setParams (def);
            std::vector<float> buf (64);
            const int seconds = 10;
            auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < (int) (seconds * fs / 64); ++i)
            {
                if (i % 2000 == 0)   // keep re-striking so voices never retire
                    for (int v = 0; v < voicesHeld; ++v) e.noteOn (33 + v, 0.9f);
                e.render (buf.data(), 64);
            }
            const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("CPU: %2d reeds ringing, 64-sample blocks @48k: %.2f%% of one core\n", voicesHeld, 100.0 * sec / seconds);
        }
    }

    // --- demo renders --------------------------------------------------------------
    if (! outDir.empty())
    {
        std::vector<Event> ev;
        double t = 0.0;
        // velocity ladder on C4, then a chromatic run, then chords
        for (float v : { 0.2f, 0.4f, 0.6f, 0.8f, 1.0f }) { ev.push_back ({ t, 60, v }); ev.push_back ({ t + 0.7, 60, 0 }); t += 0.9; }
        for (int n = 33; n <= 96; n += 3) { ev.push_back ({ t, n, 0.75f }); ev.push_back ({ t + 0.18, n, 0 }); t += 0.2; }
        t += 0.4;
        const int chords[][4] = { { 50, 53, 57, 60 }, { 48, 52, 55, 59 }, { 45, 52, 55, 60 }, { 43, 50, 53, 59 } };
        for (auto& c : chords)
        {
            for (int n : c) ev.push_back ({ t, n, 0.85f });
            for (int n : c) ev.push_back ({ t + 1.3, n, 0 });
            ev.push_back ({ t, c[0] - 12, 0.9f }); ev.push_back ({ t + 1.3, c[0] - 12, 0 });
            t += 1.5;
        }
        std::sort (ev.begin(), ev.end(), [] (auto& a, auto& b) { return a.time < b.time; });

        writeWav (outDir + "/murli_demo_dry.wav", render (fs, dry, ev, t + 1.5), (int) fs);
        writeWav (outDir + "/murli_demo_speaker.wav", render (fs, def, ev, t + 1.5), (int) fs);
        EngineParams trem = def; trem.vibrato = 0.6f;
        writeWav (outDir + "/murli_demo_vibrato.wav", render (fs, trem, ev, t + 1.5), (int) fs);
        std::printf ("\nwrote demo WAVs to %s\n", outDir.c_str());
    }

    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
