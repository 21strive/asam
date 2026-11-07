#pragma once

#include "Filters.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace asam::dsp
{

//==============================================================================
/**
    Distortion for acid lines, modelled loosely on an op-amp pedal into clipping
    diodes. Runs at the oversampled rate.

        input high-pass  ->  high-frequency emphasis  ->  asymmetric tanh stage
        ->  hard-knee algebraic clipper  ->  tone low-pass  ->  DC blocker  ->  makeup

    The Distortion knob sets gain (from slightly below unity to about +48 dB), how much
    the highs are emphasised before clipping, the asymmetry (even harmonics) and how
    tight the low end is. At zero the chain is close to transparent.
*/
class AcidDrive
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs = (float) sampleRate;
        emphasisLowpass.setCutoff (650.0f, fs);
        dcBlock.setCutoff (12.0f, fs);
        setParams (drive01, tone01);
        reset();
    }

    void reset() noexcept
    {
        inputHighpass.reset();
        emphasisLowpass.reset();
        toneFilter.reset();
        dcBlock.reset();
    }

    /** Call per base-rate sample with smoothed knob values. */
    void setParams (float drive, float toneValue) noexcept
    {
        drive01 = drive;
        tone01  = toneValue;

        const auto d = drive;
        const auto gainDb = -12.0f + 60.0f * std::pow (d, 1.35f);
        const auto gain   = std::pow (10.0f, gainDb / 20.0f);

        gain1    = std::pow (gain, 0.62f);
        gain2    = std::pow (gain, 0.38f);
        emphasis = 2.2f * d;
        bias     = 0.28f * d;
        biasTanh = std::tanh (bias);
        makeup   = makeupFor (d);

        inputHighpass.setCutoff (25.0f + 95.0f * d, fs);
        toneFilter.setCutoff (1200.0f * std::pow (10.0f, toneValue), fs, 0.7071f);
    }

    float process (float x) noexcept
    {
        const auto tight = inputHighpass.highpass (x);
        const auto pre   = tight + emphasis * (tight - emphasisLowpass.lowpass (tight));

        const auto stage1 = std::tanh (gain1 * pre + bias) - biasTanh;
        const auto v      = gain2 * stage1;
        const auto stage2 = v / std::sqrt (1.0f + v * v);

        return dcBlock.highpass (toneFilter.process (stage2)) * makeup;
    }

    /** Level compensation, measured with the default pattern so the knob mostly changes
        character rather than loudness. Linear interpolation over drive. */
    static float makeupFor (float d) noexcept
    {
        static constexpr std::array<float, 9> table { 4.86f, 3.60f, 2.15f, 1.22f, 0.73f, 0.52f, 0.44f, 0.42f, 0.43f };
        const auto pos = std::clamp (d, 0.0f, 1.0f) * (float) (table.size() - 1);
        const auto i   = std::min ((int) pos, (int) table.size() - 2);
        const auto f   = pos - (float) i;
        return table[(size_t) i] + (table[(size_t) i + 1] - table[(size_t) i]) * f;
    }

private:
    float fs = 44100.0f;
    float drive01 = 0.5f, tone01 = 0.5f;
    float gain1 = 1.0f, gain2 = 1.0f, emphasis = 0.0f, bias = 0.0f, biasTanh = 0.0f, makeup = 1.0f;

    OnePole inputHighpass, emphasisLowpass, dcBlock;
    SvfLowpass toneFilter;
};

} // namespace asam::dsp
