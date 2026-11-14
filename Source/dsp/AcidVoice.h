#pragma once

#include <juce_core/juce_core.h>

#include "DiodeLadder.h"
#include "Filters.h"

#include <array>

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
    int   unison        = 1;        // 1, 3, 5 or 7 oscillators
    float detuneCents   = 0.0f;     // the outer unison voices sit at +/- this
    float width         = 0.0f;     // 0..1, how far the outer voices are panned
    float releaseSeconds = 0.0165f; // how long the VCA rings after the gate closes (three time constants)
    int   filterMode    = 0;        // 0 low-pass (the ladder), 1 band-pass, 2 high-pass, 3 notch
    float svfQ          = 0.7f;     // resonance of the band-pass, high-pass and notch filter
};

struct StereoSample
{
    float left = 0.0f, right = 0.0f;
    bool stereo = false;            // false: right equals left
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

    Unison stacks up to seven detuned copies of the oscillator and pans them across the
    stereo field. Left and right each get their own ladder (sharing the cutoff), so the
    width survives the filter. One voice, or no width, runs a single ladder and keeps the
    other side's state in step, so switching is seamless.

    The centre oscillator runs free, like the original. The extra voices are re-aligned to a
    fixed phase spread around it whenever a note starts from silence, and whenever they are
    switched on, so unison sounds the same every time instead of depending on where the
    detuned voices happened to have drifted.

    The filter is the ladder in low-pass mode. Band-pass, high-pass and notch use a trapezoidal
    state variable filter swept by the same envelope and accent, with the same saturating input.
    Changing the mode crossfades the two over 8 ms, so it never clicks. The voice's distortion,
    tone and level come after it, in the processor.
*/
class AcidVoice
{
public:
    static constexpr int maxUnison = 7;

    void prepare (double sampleRate) noexcept
    {
        fs    = (float) sampleRate;
        invFs = 1.0f / fs;

        ladderL.prepare (sampleRate);
        ladderR.prepare (sampleRate);

        for (auto* f : { &dcL, &dcR })
            f->setCutoff (18.0f, fs);

        for (auto* f : { &softenL, &softenR })
            f->setCutoff (7000.0f, fs);

        slideCoef      = smoothingCoef (0.040f, fs);
        megAttackCoef  = smoothingCoef (0.0006f, fs);
        vcaAttackCoef  = smoothingCoef (0.0012f, fs);
        vcaReleaseCoef = decayCoef (0.0055f, fs);
        vcaHoldCoef    = decayCoef (4.0f, fs);
        accentGainCoef = smoothingCoef (0.002f, fs);
        accentDecay    = decayCoef (accentDecaySeconds / 3.0f, fs);
        unisonCoef     = smoothingCoef (0.005f, fs);
        modeFadeStep   = 1.0f / (0.008f * fs);

        lastUnison = -1;
        setParams (params);
        reset();
    }

    void reset() noexcept
    {
        ladderL.reset();
        ladderR.reset();
        svfL.reset();
        svfR.reset();
        modeFade = 1.0f;
        fadeFrom = params.filterMode;

        for (auto* f : { &dcL, &dcR, &softenL, &softenR })
            f->reset();

        for (auto& s : slots)
        {
            s.ratio = s.ratioTarget;
            s.gainL = s.gainLTarget;
            s.gainR = s.gainRTarget;
        }

        slots[0].phase = 0.0;
        alignUnison (1);

        meg = vca = accentCap = 0.0f;
        accentGain = 1.0f;
        gate = megAttacking = vcaAttacking = accentOn = false;
    }

    void setParams (const VoiceParams& p) noexcept
    {
        if (! juce::approximatelyEqual (p.decaySeconds, params.decaySeconds) || lastUnison < 0)
            normalDecay = decayCoef (p.decaySeconds / 3.0f, fs);

        if (! juce::approximatelyEqual (p.releaseSeconds, params.releaseSeconds) || lastUnison < 0)
            vcaReleaseCoef = decayCoef (p.releaseSeconds / 3.0f, fs);

        if (p.filterMode != params.filterMode && lastUnison >= 0)
            startModeFade (params.filterMode, p.filterMode);

        if (! juce::approximatelyEqual (p.resonance01, params.resonance01) || lastUnison < 0)
        {
            // The accent sweep is slower and builds up more at high resonance.
            const auto r = p.resonance01;
            accentChargeCoef    = smoothingCoef (0.010f + 0.030f * r, fs);
            accentDischargeCoef = smoothingCoef (0.090f + 0.260f * r, fs);
        }

        if (! juce::approximatelyEqual (p.svfQ, params.svfQ) || lastUnison < 0)
        {
            bandGain = bandGainFor (p.svfQ);
            highGain = highGainFor (p.svfQ);
        }

        if (p.unison != lastUnison || ! juce::approximatelyEqual (p.detuneCents, lastDetune)
                                   || ! juce::approximatelyEqual (p.width, lastWidth))
            updateUnisonTargets (p.unison, p.detuneCents, p.width);

        params = p;
    }

    /** Starts a new note: pitch jumps, envelopes retrigger. */
    void noteOn (int midiNote, bool accent) noexcept
    {
        // Re-align the unison voices while the VCA is closed, so it cannot click.
        if (vca < 0.05f)
            alignUnison (1);

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

    // Output gain of each filter mode, level-matched against the ladder with the default pattern:
    // the band-pass narrows as Q rises, the high-pass keeps its passband and grows a peak.
    static float ladderCompensation (float k) noexcept  { return 1.0f + 0.12f * k; }
    static float bandGainFor (float q) noexcept         { return 2.0f * std::sqrt (1.0f / q); }
    static float highGainFor (float q) noexcept         { return 1.2f / (1.0f + 0.08f * q); }
    static constexpr float notchGain = 0.42f;

    bool isGateOpen() const noexcept            { return gate; }

    /** Where the filter is right now, envelope and accent included, and how loud the VCA is:
        for drawing the filter as it moves. */
    float getLiveCutoff() const noexcept        { return liveCutoff; }
    float getLiveAmplitude() const noexcept     { return vca * accentGain; }

    /** Takes the pitch and the centre oscillator's phase from a voice that kept running, so a
        voice switched back on lines up with it instead of landing at a random phase. */
    void followPhase (const AcidVoice& leader) noexcept
    {
        pitch          = leader.pitch;
        targetPitch    = leader.targetPitch;
        slots[0].phase = leader.slots[0].phase;
        alignUnison (1);
    }

    StereoSample render() noexcept
    {
        // Pitch
        pitch += (targetPitch - pitch) * slideCoef;
        const auto freq = 440.0f * std::exp2 ((pitch - 69.0f) * (1.0f / 12.0f));

        // Unison oscillators into a left and a right bus
        float busL = 0.0f, busR = 0.0f;
        bool stereo = false;

        for (auto& s : slots)
        {
            s.ratio += (s.ratioTarget - s.ratio) * unisonCoef;
            s.gainL += (s.gainLTarget - s.gainL) * unisonCoef;
            s.gainR += (s.gainRTarget - s.gainR) * unisonCoef;

            if (s.gainLTarget == 0.0f && s.gainRTarget == 0.0f && s.gainL < 1.0e-5f && s.gainR < 1.0e-5f)
            {
                s.gainL = s.gainR = 0.0f;
                continue;
            }

            const auto dt = std::min (freq * s.ratio * invFs, 0.45f);
            s.phase += dt;
            if (s.phase >= 1.0)
                s.phase -= 1.0;

            const auto osc = oscillator ((float) s.phase, dt);
            busL += osc * s.gainL;
            busR += osc * s.gainR;
            stereo = stereo || std::abs (s.gainL - s.gainR) > 1.0e-6f;
        }

        if (params.square)
        {
            busL = 0.72f * softenL.lowpass (busL);
            busR = stereo ? 0.72f * softenR.lowpass (busR) : busL;
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
        const auto fc = params.cutoffHz * std::exp2 (octaves);
        liveCutoff = fc;
        const bool fading    = modeFade < 1.0f;
        const bool useLadder = params.filterMode == 0 || (fading && fadeFrom == 0);
        const bool useSvf    = params.filterMode != 0 || (fading && fadeFrom != 0);

        if (useLadder)
        {
            ladderL.setCutoff (fc);
            ladderL.setResonance (params.resonanceK);
        }

        if (useSvf)
            svfCoeffs = SvfMulti::makeCoeffs (fc, fs, params.svfQ);

        const auto yL = filter (ladderL, svfL, dcL, busL, useLadder, useSvf);
        float yR;

        if (stereo)
        {
            if (useLadder)
            {
                ladderR.setCoeffs (ladderL.getCoeffs());
                ladderR.setResonance (params.resonanceK);
            }

            yR = filter (ladderR, svfR, dcR, busR, useLadder, useSvf);
        }
        else
        {
            // Keep the right side in step so widening later starts without a click.
            ladderR = ladderL;
            svfR = svfL;
            dcR = dcL;
            softenR = softenL;
            yR = yL;
        }

        if (fading)
            modeFade = std::min (1.0f, modeFade + modeFadeStep);

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

        const auto amp = vca * accentGain;
        return { yL * amp, yR * amp, stereo };
    }

private:
    /** One side through the filter of the current mode, crossfaded from the last one while a
        mode change settles. */
    float filter (DiodeLadder& ladder, SvfMulti& svf, OnePole& dc, float x, bool useLadder, bool useSvf) noexcept
    {
        // Resonance pulls the ladder's passband down; win part of it back so the knob mostly
        // changes the character rather than the loudness.
        const auto low = useLadder ? ladder.process (x) * ladderCompensation (params.resonanceK) : 0.0f;
        const auto s   = useSvf ? svf.process (std::tanh (x), svfCoeffs) : SvfMulti::Out {};

        auto pick = [&] (int mode) noexcept
        {
            switch (mode)
            {
                case 1:  return s.bp * bandGain;
                case 2:  return s.hp * highGain;
                case 3:  return (s.lp + s.hp) * notchGain;
                default: return low;
            }
        };

        const auto to = pick (params.filterMode);
        const auto y  = modeFade < 1.0f ? pick (fadeFrom) + (to - pick (fadeFrom)) * modeFade : to;
        return dc.highpass (y);
    }

    void startModeFade (int from, int to) noexcept
    {
        // A second change before the first has settled starts from whichever side is louder.
        if (modeFade < 0.5f)
            from = fadeFrom;

        // A filter that sat idle starts from rest; the crossfade covers its first moments.
        if (to == 0 && from != 0)
        {
            ladderL.reset();
            ladderR.reset();
        }

        if (to != 0 && from == 0)
        {
            svfL.reset();
            svfR.reset();
        }

        fadeFrom = from;
        modeFade = 0.0f;
    }

    struct Slot
    {
        double phase = 0.0;
        float ratio = 1.0f, ratioTarget = 1.0f;
        float gainL = 0.0f, gainR = 0.0f, gainLTarget = 0.0f, gainRTarget = 0.0f;
    };

    /** Where slot i sits for a given voice count, -1 (left, flat) to +1 (right, sharp).
        Slot 0 is always the centre, so changing the count never moves the main voice. */
    static float slotPosition (int slot, int count) noexcept
    {
        if (slot == 0 || count <= 1)
            return 0.0f;

        const auto pairs = (count - 1) / 2;
        const auto pair  = (slot + 1) / 2;                  // 1, 1, 2, 2, 3, 3
        const auto side  = (slot % 2 == 1) ? -1.0f : 1.0f;  // odd slots left, even slots right
        return side * (float) pair / (float) pairs;
    }

    void updateUnisonTargets (int count, float detuneCents, float width) noexcept
    {
        count = juce::jlimit (1, maxUnison, count);
        const auto level = 1.0f / std::sqrt ((float) count);

        for (int i = 0; i < maxUnison; ++i)
        {
            auto& s = slots[(size_t) i];

            if (i >= count)
            {
                s.gainLTarget = s.gainRTarget = 0.0f;
                continue;
            }

            const auto x = slotPosition (i, count);
            s.ratioTarget = std::exp2 (detuneCents * x / 1200.0f);

            // Equal-power pan, scaled so the centre is unity on both sides (one voice sounds
            // exactly like the mono voice it replaces).
            const auto pan   = juce::jlimit (-1.0f, 1.0f, width * x);
            const auto theta = (pan + 1.0f) * 0.25f * kPi;
            s.gainLTarget = level * 1.41421356f * std::cos (theta);
            s.gainRTarget = level * 1.41421356f * std::sin (theta);

            if (i >= lastUnison)
                s.ratio = s.ratioTarget;   // a voice coming in starts on its own pitch
        }

        // Voices being switched on join at their place in the spread, not wherever they stopped.
        if (lastUnison >= 1 && count > lastUnison)
            alignUnison (lastUnison);

        lastUnison = count;
        lastDetune = detuneCents;
        lastWidth  = width;
    }

    /** Puts slots [firstSlot, maxUnison) at their fixed offset from the centre oscillator.
        The spread keeps the fundamentals close (a solid low end) and was chosen so no
        harmonic up to the 12th is cancelled deeply when the voices start. */
    void alignUnison (int firstSlot) noexcept
    {
        static constexpr std::array<double, maxUnison> spread { 0.0, -0.02, 0.02, -0.10, 0.10, -0.24, 0.24 };
        const auto centre = slots[0].phase;

        for (int i = juce::jmax (1, firstSlot); i < maxUnison; ++i)
        {
            auto p = centre + spread[(size_t) i];
            slots[(size_t) i].phase = p - std::floor (p);
        }
    }

    float oscillator (float t, float dt) const noexcept
    {
        if (params.square)
        {
            // Low for the first half: its fundamental then sits in phase with the saw's, so a saw
            // voice and a square voice add up instead of cancelling.
            auto sq = t < 0.5f ? -1.0f : 1.0f;
            sq -= polyBlep (t, dt);
            auto t2 = t + 0.5f;
            if (t2 >= 1.0f) t2 -= 1.0f;
            sq += polyBlep (t2, dt);
            return sq;
        }

        return 2.0f * t - 1.0f - polyBlep (t, dt);
    }

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
    DiodeLadder ladderL, ladderR;
    SvfMulti svfL, svfR;
    SvfMulti::Coeffs svfCoeffs;
    float bandGain = 1.0f, highGain = 1.0f, modeFade = 1.0f, modeFadeStep = 0.01f, liveCutoff = 400.0f;
    int fadeFrom = 0;
    OnePole dcL, dcR, softenL, softenR;
    std::array<Slot, maxUnison> slots {};
    int lastUnison = -1;
    float lastDetune = -1.0f, lastWidth = -1.0f;

    float fs = 44100.0f, invFs = 1.0f / 44100.0f;
    float pitch = 48.0f, targetPitch = 48.0f;
    float meg = 0.0f, vca = 0.0f, accentCap = 0.0f, accentGain = 1.0f;
    bool gate = false, megAttacking = false, vcaAttacking = false, accentOn = false;

    float slideCoef = 0.0f, megAttackCoef = 0.0f, vcaAttackCoef = 0.0f, vcaReleaseCoef = 0.0f;
    float vcaHoldCoef = 1.0f, accentGainCoef = 0.0f, normalDecay = 0.0f, accentDecay = 0.0f;
    float accentChargeCoef = 0.0f, accentDischargeCoef = 0.0f, unisonCoef = 1.0f;
};

} // namespace asam::dsp
