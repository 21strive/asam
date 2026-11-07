#pragma once

#include "DiodeLadder.h"
#include "Filters.h"

namespace asam::dsp
{

//==============================================================================
/** Knob values for the voice, already mapped to physical units. */
struct VoiceParams
{
    float cutoffHz      = 400.0f;   // base cutoff (the Filter knob)
    float resonanceK    = 12.0f;    // ladder feedback
    float resonance01   = 0.7f;     // raw knob, shapes the accent sweep
    float envModOctaves = 2.0f;
    float decaySeconds  = 0.4f;
    float accent01      = 0.6f;
    bool  square        = false;
};

//==============================================================================
/**
    Monophonic acid voice: band-limited saw or square, the diode ladder, a filter
    envelope with accent sweep, a VCA and a fixed-time glide for slides.

    Behaviour follows the classic bassline machine:
      - a normal note retriggers the filter envelope (decay set by the Decay knob);
      - an accented note uses the shortest decay, gets louder, and charges an accent
        "sweep" capacitor that pushes the cutoff further; consecutive accents stack;
      - a slid note keeps the gate open, glides in pitch and does not retrigger.
*/
class AcidVoice
{
public:
    void prepare (double sampleRate) noexcept
    {
        fs    = (float) sampleRate;
        invFs = 1.0f / fs;

        ladder.prepare (sampleRate);
        dcBlock.setCutoff (18.0f, fs);
        squareSoften.setCutoff (7000.0f, fs);

        slideCoef      = smoothingCoef (0.040f, fs);
        megAttackCoef  = smoothingCoef (0.0006f, fs);
        vcaAttackCoef  = smoothingCoef (0.0012f, fs);
        vcaReleaseCoef = decayCoef (0.0055f, fs);
        vcaHoldCoef    = decayCoef (4.0f, fs);
        accentGainCoef = smoothingCoef (0.002f, fs);
        accentDecay    = decayCoef (accentDecaySeconds / 3.0f, fs);

        setParams (params);
        reset();
    }

    void reset() noexcept
    {
        ladder.reset();
        dcBlock.reset();
        squareSoften.reset();
        phase = 0.0;
        meg = vca = accentCap = 0.0f;
        accentGain = 1.0f;
        gate = megAttacking = vcaAttacking = accentOn = false;
    }

    void setParams (const VoiceParams& p) noexcept
    {
        params = p;
        normalDecay = decayCoef (p.decaySeconds / 3.0f, fs);

        // The accent sweep is slower and builds up more at high resonance.
        const auto r = p.resonance01;
        accentChargeCoef    = smoothingCoef (0.010f + 0.030f * r, fs);
        accentDischargeCoef = smoothingCoef (0.090f + 0.260f * r, fs);
    }

    /** Starts a new note: pitch jumps, envelopes retrigger. */
    void noteOn (int midiNote, bool accent) noexcept
    {
        targetPitch = pitch = (float) midiNote;
        accentOn     = accent;
        gate         = true;
        megAttacking = true;
        vcaAttacking = true;
    }

    /** Legato note: glides from the current pitch, envelopes keep running. */
    void glideTo (int midiNote, bool accent) noexcept
    {
        if (! gate)
        {
            noteOn (midiNote, accent);
            return;
        }

        targetPitch = (float) midiNote;
        accentOn    = accent;
    }

    void noteOff() noexcept                     { gate = false; }

    bool isGateOpen() const noexcept            { return gate; }

    float render() noexcept
    {
        // Pitch and oscillator
        pitch += (targetPitch - pitch) * slideCoef;
        const auto freq = 440.0f * std::exp2 ((pitch - 69.0f) * (1.0f / 12.0f));
        const auto dt   = std::min (freq * invFs, 0.45f);

        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        const auto t = (float) phase;
        float osc;

        if (params.square)
        {
            auto sq = t < 0.5f ? 1.0f : -1.0f;
            sq += polyBlep (t, dt);
            auto t2 = t + 0.5f;
            if (t2 >= 1.0f) t2 -= 1.0f;
            sq -= polyBlep (t2, dt);
            osc = 0.72f * squareSoften.lowpass (sq);
        }
        else
        {
            osc = 2.0f * t - 1.0f - polyBlep (t, dt);
        }

        // Filter envelope
        if (megAttacking)
        {
            meg += (1.0f - meg) * megAttackCoef;

            if (meg > 0.995f)
            {
                meg = 1.0f;
                megAttacking = false;
            }
        }
        else
        {
            meg *= accentOn ? accentDecay : normalDecay;
        }

        // Accent sweep capacitor
        const auto source = accentOn ? params.accent01 * meg : 0.0f;
        accentCap += (source - accentCap) * (source > accentCap ? accentChargeCoef : accentDischargeCoef);

        const auto octaves = params.envModOctaves * meg + accentOctaves * accentCap;
        ladder.setCutoff (params.cutoffHz * std::exp2 (octaves));
        ladder.setResonance (params.resonanceK);

        // Resonance pulls the passband down; win part of it back so the knob mostly changes
        // the character rather than the loudness.
        const auto filtered = dcBlock.highpass (ladder.process (osc)) * (1.0f + 0.12f * params.resonanceK);

        // VCA
        if (gate)
        {
            if (vcaAttacking)
            {
                vca += (1.0f - vca) * vcaAttackCoef;

                if (vca > 0.999f)
                {
                    vca = 1.0f;
                    vcaAttacking = false;
                }
            }
            else
            {
                vca *= vcaHoldCoef;
            }
        }
        else
        {
            vca *= vcaReleaseCoef;
        }

        const auto accentTarget = accentOn ? 1.0f + accentLevel * params.accent01 : 1.0f;
        accentGain += (accentTarget - accentGain) * accentGainCoef;

        return filtered * vca * accentGain;
    }

private:
    static float polyBlep (float t, float dt) noexcept
    {
        if (t < dt)
        {
            t /= dt;
            return t + t - t * t - 1.0f;
        }

        if (t > 1.0f - dt)
        {
            t = (t - 1.0f) / dt;
            return t * t + t + t + 1.0f;
        }

        return 0.0f;
    }

    static constexpr float accentOctaves      = 3.0f;    // cutoff push at full accent
    static constexpr float accentLevel        = 0.9f;    // VCA boost at full accent (~ +5.6 dB)
    static constexpr float accentDecaySeconds = 0.2f;    // accented notes use the shortest decay

    VoiceParams params;
    DiodeLadder ladder;
    OnePole dcBlock, squareSoften;

    float fs = 44100.0f, invFs = 1.0f / 44100.0f;
    double phase = 0.0;
    float pitch = 48.0f, targetPitch = 48.0f;
    float meg = 0.0f, vca = 0.0f, accentCap = 0.0f, accentGain = 1.0f;
    bool gate = false, megAttacking = false, vcaAttacking = false, accentOn = false;

    float slideCoef = 0.0f, megAttackCoef = 0.0f, vcaAttackCoef = 0.0f, vcaReleaseCoef = 0.0f;
    float vcaHoldCoef = 1.0f, accentGainCoef = 0.0f, normalDecay = 0.0f, accentDecay = 0.0f;
    float accentChargeCoef = 0.0f, accentDischargeCoef = 0.0f;
};

} // namespace asam::dsp
