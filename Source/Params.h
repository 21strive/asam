#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace asam
{

/** Voices: up to three layers play every note, each with its own oscillator, filter and envelope. */
inline constexpr int numVoices = 3;

//==============================================================================
// Parameter ids. These are part of saved sessions, so never rename them.
namespace pid
{
    // Per voice. These name voice 1; voices 2 and 3 add a prefix (see forVoice), so sessions
    // saved before there were three voices load into voice 1 unchanged.
    inline constexpr const char* cutoff     = "cutoff";
    inline constexpr const char* resonance  = "resonance";
    inline constexpr const char* envMod     = "envmod";
    inline constexpr const char* decay      = "decay";
    inline constexpr const char* release    = "release";
    inline constexpr const char* accent     = "accent";
    inline constexpr const char* waveform   = "waveform";
    inline constexpr const char* unison     = "unison";
    inline constexpr const char* detune     = "detune";
    inline constexpr const char* width      = "width";
    inline constexpr const char* level      = "level";
    inline constexpr const char* filterMode = "filtermode";
    inline constexpr const char* enabled    = "enabled";
    inline constexpr const char* drive      = "drive";     // was the one distortion; now voice 1's
    inline constexpr const char* tone       = "tone";

    /** The id of a per-voice parameter: forVoice (0, cutoff) is "cutoff", forVoice (1, cutoff) "v2_cutoff". */
    juce::String forVoice (int voiceIndex, const char* base);

    // Global
    inline constexpr const char* volume     = "volume";    // the master volume
    inline constexpr const char* tempo      = "tempo";
    inline constexpr const char* swing      = "swing";
    inline constexpr const char* hostSync   = "hostsync";
    inline constexpr const char* key        = "key";
    inline constexpr const char* octave     = "octave";
}

/** The filter in each voice. */
enum class FilterMode { lowpass, bandpass, highpass, notch };

//==============================================================================
// Mappings from the 0..1 knob values to physical units, shared by the DSP and the UI.
namespace map
{
    /** Filter knob: 40 Hz .. 10.24 kHz, eight octaves, exponential. */
    inline float cutoffHz (float v) noexcept        { return 40.0f * std::exp2 (8.0f * v); }

    /** Decay knob: 200 ms .. 2.5 s, the length of the filter sweep. */
    inline float decaySeconds (float v) noexcept    { return 0.2f * std::pow (12.5f, v); }

    /** Release knob: how long a note rings after its gate closes, 16.5 ms .. 3 s, read the way
        Decay reads (three time constants). The bottom is the classic machine's snappy VCA. */
    inline float releaseSeconds (float v) noexcept  { return 0.0165f * std::pow (3.0f / 0.0165f, v); }

    /** Resonance knob to the Q of the band-pass and high-pass filter, 0.55 .. 11. */
    inline float svfQ (float v) noexcept            { return 0.55f * std::pow (20.0f, v); }

    /** Tone knob: low-pass after the drive, 1.2 kHz .. 12 kHz. */
    inline float toneHz (float v) noexcept          { return 1200.0f * std::pow (10.0f, v); }

    /** Resonance knob to ladder feedback. The ladder self-oscillates near 18.4. */
    inline float resonanceK (float v) noexcept      { return 17.2f * v; }

    /** Env mod knob to the filter envelope depth in octaves. */
    inline float envModOctaves (float v) noexcept   { return 4.5f * v; }

    /** Detune knob: where the outer unison voices sit, up to +/- 50 cents. Finer at the bottom. */
    inline float detuneCents (float v) noexcept     { return 50.0f * v * v; }

    /** Unison choice index to voice count: 1, 3, 5 or 7. */
    inline int unisonVoices (int index) noexcept    { return 1 + 2 * juce::jlimit (0, 3, index); }
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
DisplayValue displayRelease   (float v);
DisplayValue displayTone      (float v);
DisplayValue displayDetune    (float v);
DisplayValue displayVolume    (float dB);
DisplayValue displayTempo     (float bpm);
DisplayValue displaySwing     (float percent);

const juce::StringArray& keyNames();        // C, C#, D ... B

/** What a sound preset holds: every per-voice parameter of the three voices, and the master
    volume. Never the pattern, the clock, the key or the octave. */
const juce::StringArray& soundParameterIds();

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace asam
