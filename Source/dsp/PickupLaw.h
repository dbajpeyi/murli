// Murli — electrostatic pickup law, evaluated as a band-limited Chebyshev series.
//
// Each reed swings through a slot in the shared, polarised pickup comb. At rest it
// sits just outside the finger band; on one half of its swing it enters the band and
// the reed-to-finger overlap (hence capacitance) grows roughly linearly, saturating
// once the reed is fully inside. Outside the band only a weak fringe field couples.
// That one-sided law is what turns a nearly sinusoidal reed into the 200A "bark":
// soft notes stay round, hard notes push deep into the band and grow even harmonics.
//
// The law is expanded in Chebyshev polynomials over the reed's displacement range.
// For a sinusoid that spans the range, T_k(cos θ) = cos(kθ), so truncating the
// series at degree D produces harmonics up to D·f0 and nothing above — the waveshaper
// is band-limited per note with zero oversampling and zero latency.
#pragma once

#include "DspUtil.h"
#include <array>

namespace murli
{

struct PickupLaw
{
    // Shape fitted so a C4 at the pickup goes from H2/H1 ≈ -28 dB (pp) through
    // -13 dB (mf) to -10 dB (ff), with odd harmonics appearing as the reed bottoms out.
    static constexpr int    kMaxDegree   = 32;
    static constexpr double kRangeMm     = 2.0;    // evaluated domain: ±2 mm swing
    static constexpr double kRestOffset  = 0.06;   // rest gap outside the finger band (mm)
    static constexpr double kRounding    = 0.15;   // fringe rounding of the band edges (mm)
    static constexpr double kBandDepth   = 1.00;   // penetration at which overlap saturates (mm)
    static constexpr double kFringeSlope = 0.10;   // weak coupling outside the band

    std::array<double, kMaxDegree + 1> c {};

    static double softplus (double v, double s)
    {
        const double a = v / s;
        return a > 30.0 ? v : s * std::log1p (std::exp (a));
    }

    // Capacitance change vs reed displacement (mm), zero at rest.
    static double law (double xMm)
    {
        auto raw = [] (double x)
        {
            const double u = x - kRestOffset;
            const double overlap = softplus (u, kRounding) - softplus (u - kBandDepth, kRounding);
            return overlap + kFringeSlope * x;
        };
        return raw (xMm) - raw (0.0);
    }

    PickupLaw()
    {
        constexpr int n = 256;   // Chebyshev–Gauss nodes
        for (int k = 0; k <= kMaxDegree; ++k)
        {
            double sum = 0.0;
            for (int j = 0; j < n; ++j)
            {
                const double th = kPi * (j + 0.5) / n;
                sum += law (kRangeMm * std::cos (th)) * std::cos (k * th);
            }
            c[(size_t) k] = (k == 0 ? 1.0 : 2.0) * sum / n;
        }
    }

    // Scalar evaluation; t = displacement / kRangeMm, already clamped to [-1, 1].
    double eval (double t, int degree) const noexcept
    {
        double b1 = 0.0, b2 = 0.0;
        const double t2 = 2.0 * t;
        for (int k = degree; k >= 1; --k)
        {
            const double b0 = c[(size_t) k] + t2 * b1 - b2;
            b2 = b1; b1 = b0;
        }
        return c[0] + t * b1 - b2;
    }

    // Clenshaw over a block of independent samples, in place. The inner loop runs
    // across samples so it vectorises and hides the recurrence's latency.
    template <std::size_t N>
    void evalBlock (float* t, int n, int degree) const noexcept
    {
        float b1[N] = {}, b2[N] = {};
        for (int k = degree; k >= 1; --k)
        {
            const float ck = (float) c[(size_t) k];
            for (int i = 0; i < n; ++i)
            {
                const float b0 = ck + 2.0f * t[i] * b1[i] - b2[i];
                b2[i] = b1[i]; b1[i] = b0;
            }
        }
        const float c0 = (float) c[0];
        for (int i = 0; i < n; ++i)
            t[i] = c0 + t[i] * b1[i] - b2[i];
    }
};

} // namespace murli
