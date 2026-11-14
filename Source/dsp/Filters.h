#pragma once

#include <cmath>

namespace asam::dsp
{

inline constexpr float kPi = 3.14159265358979323846f;

/** Trapezoidal one-pole, low-pass and high-pass outputs. */
class OnePole
{
public:
    void setCutoff (float hz, float sampleRate) noexcept
    {
        const auto g = std::tan (kPi * hz / sampleRate);
        G = g / (1.0f + g);
    }

    float lowpass (float x) noexcept
    {
        const auto v = (x - s) * G;
        const auto y = v + s;
        s = y + v;
        return y;
    }

    float highpass (float x) noexcept     { return x - lowpass (x); }

    void reset() noexcept                  { s = 0.0f; }

private:
    float G = 0.5f, s = 0.0f;
};

/** Trapezoidal state variable filter, low-pass output. */
class SvfLowpass
{
public:
    void setCutoff (float hz, float sampleRate, float q = 0.7071f) noexcept
    {
        g  = std::tan (kPi * hz / sampleRate);
        r2 = 1.0f / q;
        h  = 1.0f / (1.0f + r2 * g + g * g);
    }

    float process (float x) noexcept
    {
        const auto hp = (x - (r2 + g) * s1 - s2) * h;
        const auto bp = g * hp + s1;
        s1 = g * hp + bp;
        const auto lp = g * bp + s2;
        s2 = g * bp + lp;
        return lp;
    }

    void reset() noexcept                  { s1 = s2 = 0.0f; }

private:
    float g = 0.1f, r2 = 1.414f, h = 1.0f, s1 = 0.0f, s2 = 0.0f;
};

/** Trapezoidal state variable filter with all three outputs, for cutoffs that move every sample.
    Coefficients live apart from the state so the left and right filters can share them. */
class SvfMulti
{
public:
    struct Coeffs
    {
        float g = 0.1f, k = 1.414f, h = 1.0f;
    };

    struct Out
    {
        float lp = 0.0f, bp = 0.0f, hp = 0.0f;
    };

    static Coeffs makeCoeffs (float hz, float sampleRate, float q) noexcept
    {
        Coeffs c;
        c.g = std::tan (kPi * std::fmin (std::fmax (hz, 8.0f), 0.45f * sampleRate) / sampleRate);
        c.k = 1.0f / q;
        c.h = 1.0f / (1.0f + c.k * c.g + c.g * c.g);
        return c;
    }

    Out process (float x, const Coeffs& c) noexcept
    {
        Out o;
        o.hp = (x - (c.k + c.g) * s1 - s2) * c.h;
        const auto v1 = c.g * o.hp;
        o.bp = v1 + s1;
        s1 = o.bp + v1;
        const auto v2 = c.g * o.bp;
        o.lp = v2 + s2;
        s2 = o.lp + v2;
        return o;
    }

    void reset() noexcept                  { s1 = s2 = 0.0f; }

private:
    float s1 = 0.0f, s2 = 0.0f;
};

/** Returns the per-sample coefficient of a one-pole smoother with time constant tau. */
inline float smoothingCoef (float tauSeconds, float sampleRate) noexcept
{
    return 1.0f - std::exp (-1.0f / (tauSeconds * sampleRate));
}

/** Returns the per-sample multiplier of an exponential decay with time constant tau. */
inline float decayCoef (float tauSeconds, float sampleRate) noexcept
{
    return std::exp (-1.0f / (tauSeconds * sampleRate));
}

} // namespace asam::dsp
