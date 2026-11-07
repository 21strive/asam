#pragma once

#include <algorithm>
#include <cmath>

namespace asam::dsp
{

//==============================================================================
/**
    Four-pole diode ladder low-pass, the filter topology behind classic acid basslines.

    The ladder is modelled as an RC ladder (each capacitor coupled to its neighbours,
    characteristic polynomial s^4 + 7s^3 + 15s^2 + 10s + 1), which gives the soft,
    spread-pole slope that sets it apart from a transistor ladder. Resonance comes from
    global feedback that passes through a one-pole high-pass, so the low end survives
    high resonance settings, and a tanh stage on the ladder input saturates the loop.

    Everything is solved per sample with trapezoidal (zero-delay feedback) integration:
    a closed-form linear solve estimates the loop, the saturator is applied, and the
    ladder is then solved again with the saturated input.
*/
class DiodeLadder
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        setFeedbackHighpass (150.0f);
        setCutoff (1000.0f);
        reset();
    }

    void reset() noexcept   { s1 = s2 = s3 = s4 = sh = 0.0f; }

    void setFeedbackHighpass (float hz) noexcept
    {
        gh = std::tan (pi * hz / fs);
        hpAlpha = 1.0f / (1.0f + gh);
    }

    /** Places the resonance peak at fcHz. */
    void setCutoff (float fcHz) noexcept
    {
        const auto f = std::clamp (fcHz, 8.0f, 0.45f * fs);

        // The open-loop phase of the RC ladder reaches -180 degrees at sqrt(10/7) times the
        // integrator frequency, so prewarp there: the peak lands exactly on fcHz.
        g = std::tan (pi * f / fs) * invW180;

        b4    = g / (1.0f + g);
        inv1g = 1.0f / (1.0f + g);
        const auto d3 = 1.0f + 2.0f * g - g * b4;
        invD3 = 1.0f / d3;
        b3    = g * invD3;
        const auto d2 = 1.0f + 2.0f * g - g * b3;
        invD2 = 1.0f / d2;
        b2    = g * invD2;
        d1    = 1.0f + 2.0f * g - g * b2;
        invD1 = 1.0f / d1;
        e4    = b4 * b3 * b2;
    }

    /** Loop gain. The linear ladder self-oscillates at k = 18.39. */
    void setResonance (float newK) noexcept     { k = newK; }

    float process (float x) noexcept
    {
        // Back-substitution terms: y4 = a4 + b4*y3, y3 = a3 + b3*y2, y2 = a2 + b2*y1.
        const auto a4 = s4 * inv1g;
        const auto a3 = (s3 + g * a4) * invD3;
        const auto a2 = (s2 + g * a3) * invD2;
        const auto c4 = a4 + b4 * (a3 + b3 * a2);       // y4 = c4 + e4*y1

        // Linear estimate including the high-passed feedback: fb = hpAlpha * (y4 - sh).
        const auto ka    = k * hpAlpha;
        const auto y1Lin = (g * (x - ka * (c4 - sh)) + s1 + g * a2) / (d1 + g * ka * e4);
        const auto y4Lin = c4 + e4 * y1Lin;
        const auto u     = std::tanh (x - ka * (y4Lin - sh));

        // Solve the ladder again with the saturated input.
        const auto y1 = (g * u + s1 + g * a2) * invD1;
        const auto y2 = a2 + b2 * y1;
        const auto y3 = a3 + b3 * y2;
        const auto y4 = a4 + b4 * y3;

        s1 = 2.0f * y1 - s1;
        s2 = 2.0f * y2 - s2;
        s3 = 2.0f * y3 - s3;
        s4 = 2.0f * y4 - s4;

        const auto lp = (gh * y4 + sh) * hpAlpha;
        sh = 2.0f * lp - sh;

        return y4;
    }

private:
    static constexpr float pi      = 3.14159265358979323846f;
    static constexpr float invW180 = 0.83666002653f;    // 1 / sqrt(10/7)

    float fs = 44100.0f;
    float g = 0.1f, k = 0.0f;
    float b2 = 0.0f, b3 = 0.0f, b4 = 0.0f, e4 = 0.0f, d1 = 1.0f;
    float inv1g = 1.0f, invD1 = 1.0f, invD2 = 1.0f, invD3 = 1.0f;
    float gh = 0.0f, hpAlpha = 1.0f;
    float s1 = 0.0f, s2 = 0.0f, s3 = 0.0f, s4 = 0.0f, sh = 0.0f;
};

} // namespace asam::dsp
