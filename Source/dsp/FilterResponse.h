#pragma once

#include "AcidVoice.h"

#include <cmath>
#include <complex>

namespace asam::dsp
{

/**
    The magnitude response of a voice's filter at one frequency, for drawing it: the linear part
    of each mode with the voice's own output gain, without the saturating input.

        mode 0  the diode ladder low-pass: an RC ladder (s^4 + 7s^3 + 15s^2 + 10s + 1) closed
                through a 150 Hz high-pass, so its peak lands on the cutoff and the low end
                comes back at high resonance
        mode 1  band-pass, 2 high-pass, 3 notch: the state variable filter

    ladderK is the ladder's loop gain (map::resonanceK), q the state variable filter's Q
    (map::svfQ). Returns a linear gain.
*/
inline float filterMagnitude (int mode, float cutoffHz, float ladderK, float q, float hz) noexcept
{
    using C = std::complex<double>;

    if (mode == 0)
    {
        const auto integrator = (double) cutoffHz / std::sqrt (10.0 / 7.0);
        const C s (0.0, (double) hz / integrator);
        const C ladder = (((s + 7.0) * s + 15.0) * s + 10.0) * s + 1.0;
        const C feedbackHighpass = s / (s + 150.0 / integrator);
        return (float) (std::abs (1.0 / (ladder + (double) ladderK * feedbackHighpass)))
               * AcidVoice::ladderCompensation (ladderK);
    }

    const C s (0.0, (double) hz / (double) cutoffHz);
    const C d = s * s + s / (double) q + 1.0;

    switch (mode)
    {
        case 1:  return (float) std::abs (s / d) * AcidVoice::bandGainFor (q);
        case 2:  return (float) std::abs (s * s / d) * AcidVoice::highGainFor (q);
        default: return (float) std::abs ((s * s + 1.0) / d) * AcidVoice::notchGain;
    }
}

} // namespace asam::dsp
