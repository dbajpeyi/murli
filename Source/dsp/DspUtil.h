// Murli — small, allocation-free DSP building blocks.
#pragma once

#include <cmath>
#include <cstdint>
#include <algorithm>

namespace murli
{

constexpr double kPi    = 3.14159265358979323846;
constexpr double kTwoPi = 6.28318530717958647692;

inline double dbToGain (double db)  { return std::pow (10.0, db / 20.0); }

// Per-sample multiplier giving `dbPerSecond` of decay at sample rate `fs`.
inline double decayMultiplier (double dbPerSecond, double fs)
{
    return std::pow (10.0, -dbPerSecond / (20.0 * fs));
}

// Cheap deterministic hash in [-1, 1] — per-key "manufacturing tolerance".
inline double hashUnit (uint32_t a, uint32_t b = 0)
{
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u;
    h ^= h >> 15; h *= 0x2C1B3C6Du;
    h ^= h >> 12; h *= 0x297A2D39u;
    h ^= h >> 15;
    return (double) h / 2147483647.5 - 1.0;
}

struct Lcg
{
    uint32_t state = 22222u;
    float next() noexcept   // white noise in [-1, 1)
    {
        state = state * 1664525u + 1013904223u;
        return (float) (int32_t) state * (1.0f / 2147483648.0f);
    }
};

// Linear parameter ramp used to de-zipper knob moves.
struct Smoothed
{
    float current = 0.0f, target = 0.0f, step = 0.0f;
    int remaining = 0, rampLength = 64;

    void reset (double fs, double seconds, float value)
    {
        rampLength = std::max (1, (int) (fs * seconds));
        current = target = value; remaining = 0; step = 0.0f;
    }
    void setTarget (float v)
    {
        if (std::abs (v - target) < 1.0e-7f) return;
        target = v;
        remaining = rampLength;
        step = (target - current) / (float) rampLength;
    }
    float next() noexcept
    {
        if (remaining > 0) { current += step; if (--remaining == 0) current = target; }
        return current;
    }
    bool isSmoothing() const noexcept { return remaining > 0; }
};

// One-pole filters (matched-pole, no warping needed at the corners we use).
struct OnePoleLP
{
    float a = 0.0f, z = 0.0f;
    void setCutoff (double fc, double fs) { a = (float) std::exp (-kTwoPi * fc / fs); }
    float process (float x) noexcept { z = x + a * (z - x); return z; }
    void reset() { z = 0.0f; }
};

struct OnePoleHP   // bilinear one-pole high-pass
{
    float b0 = 1.0f, a1 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    void setCutoff (double fc, double fs)
    {
        const double k = std::tan (kPi * fc / fs);
        b0 = (float) (1.0 / (1.0 + k));
        a1 = (float) ((1.0 - k) / (1.0 + k));
    }
    float process (float x) noexcept
    {
        const float y = b0 * (x - x1) + a1 * y1;
        x1 = x; y1 = y;
        return y;
    }
    void reset() { x1 = y1 = 0.0f; }
};

// RBJ biquad, transposed direct form II.
struct Biquad
{
    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, s1 = 0, s2 = 0;

    float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
    void reset() { s1 = s2 = 0.0f; }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
    {
        b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
        a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
    }
    void lowPass (double fc, double q, double fs)
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void highPass (double fc, double q, double fs)
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + al, -2 * c, 1 - al);
    }
    void bandPass (double fc, double q, double fs)   // 0 dB peak gain
    {
        const double w = kTwoPi * fc / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (al, 0, -al, 1 + al, -2 * c, 1 - al);
    }
    void peak (double fc, double q, double gainDb, double fs)
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w = kTwoPi * fc / fs, c = std::cos (w), al = std::sin (w) / (2 * q);
        set (1 + al * A, -2 * c, 1 - al * A, 1 + al / A, -2 * c, 1 - al / A);
    }
};

// Asymmetric exponential soft clipper with first-order antiderivative
// anti-aliasing (ADAA). ADAA costs half a sample of group delay, no latency.
//   f(x) =  P (1 - e^{-x/P})   x >= 0   (soft approach to the +P rail)
//   f(x) = -N (1 - e^{ x/N})   x <  0   (soft approach to the -N rail)
struct AsymClipperADAA
{
    double P = 1.0, N = 1.0, x1 = 0.0, F1 = 0.0;

    void setRails (double pos, double neg) { P = pos; N = neg; x1 = 0.0; F1 = 0.0; }
    void reset() { x1 = 0.0; F1 = 0.0; }

    double f (double x) const noexcept
    {
        return x >= 0.0 ? P * (1.0 - std::exp (-x / P)) : -N * (1.0 - std::exp (x / N));
    }
    double F (double x) const noexcept   // antiderivative, F(0) = 0
    {
        return x >= 0.0 ? P * x + P * P * (std::exp (-x / P) - 1.0)
                        : -N * x + N * N * (std::exp (x / N) - 1.0);
    }
    float process (float xf) noexcept
    {
        const double x = xf, Fx = F (x), d = x - x1;
        const double y = std::abs (d) > 1.0e-5 ? (Fx - F1) / d : f (0.5 * (x + x1));
        x1 = x; F1 = Fx;
        return (float) y;
    }
};

// Symmetric tanh-ish rail clipper with ADAA (power amp rails).
struct SoftRailADAA
{
    double x1 = 0.0, F1 = 0.0;
    static double f (double x) noexcept { return std::tanh (x); }
    static double F (double x) noexcept   // log(cosh x), overflow-safe
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994531;
    }
    void reset() { x1 = 0.0; F1 = 0.0; }
    float process (float xf) noexcept
    {
        const double x = xf, Fx = F (x), d = x - x1;
        const double y = std::abs (d) > 1.0e-5 ? (Fx - F1) / d : f (0.5 * (x + x1));
        x1 = x; F1 = Fx;
        return (float) y;
    }
};

} // namespace murli
