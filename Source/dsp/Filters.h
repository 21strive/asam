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
