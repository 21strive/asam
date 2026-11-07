#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace asam
{

//==============================================================================
// Parameter ids. These are part of saved sessions, so never rename them.
namespace pid
{
    inline constexpr const char* cutoff    = "cutoff";
    inline constexpr const char* resonance = "resonance";
    inline constexpr const char* envMod    = "envmod";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* accent    = "accent";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* volume    = "volume";
    inline constexpr const char* waveform  = "waveform";
    inline constexpr const char* tempo     = "tempo";
    inline constexpr const char* swing     = "swing";
    inline constexpr const char* hostSync  = "hostsync";
    inline constexpr const char* key       = "key";
    inline constexpr const char* octave    = "octave";
}

//==============================================================================
// Mappings from the 0..1 knob values to physical units, shared by the DSP and the UI.
namespace map
{
    /** Filter knob: 40 Hz .. 10.24 kHz, eight octaves, exponential. */
    inline float cutoffHz (float v) noexcept        { return 40.0f * std::exp2 (8.0f * v); }

    /** Decay knob: 200 ms .. 2.5 s, the length of the filter sweep. */
    inline float decaySeconds (float v) noexcept    { return 0.2f * std::pow (12.5f, v); }

    /** Tone knob: low-pass after the drive, 1.2 kHz .. 12 kHz. */
    inline float toneHz (float v) noexcept          { return 1200.0f * std::pow (10.0f, v); }

    /** Resonance knob to ladder feedback. The ladder self-oscillates near 18.4. */
    inline float resonanceK (float v) noexcept      { return 17.2f * v; }

    /** Env mod knob to the filter envelope depth in octaves. */
    inline float envModOctaves (float v) noexcept   { return 4.5f * v; }
}

//==============================================================================
/** A value split the Rapi way: the figure and its unit are set separately. */
struct DisplayValue
{
    juce::String number;
    juce::String unit;

    juce::String toString() const   { return unit.isEmpty() ? number : number + " " + unit; }
};

/** Formats numbers with a true minus sign (U+2212), as Rapi requires. */
juce::String withMinus (const juce::String& s);

DisplayValue displayCutoff    (float v);
DisplayValue displayPercent   (float v);    // 0..1 -> "72 %"
DisplayValue displayDecay     (float v);
DisplayValue displayTone      (float v);
DisplayValue displayVolume    (float dB);
DisplayValue displayTempo     (float bpm);
DisplayValue displaySwing     (float percent);

const juce::StringArray& keyNames();        // C, C#, D ... B

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace asam
