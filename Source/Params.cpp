#include "Params.h"

namespace asam
{

juce::String pid::forVoice (int voiceIndex, const char* base)
{
    if (voiceIndex <= 0)
        return base;

    return "v" + juce::String (voiceIndex + 1) + "_" + base;
}

juce::String withMinus (const juce::String& s)
{
    if (s.startsWithChar ('-'))
        return juce::String::charToString ((juce::juce_wchar) 0x2212) + s.substring (1);

    return s;
}

DisplayValue displayCutoff (float v)
{
    const auto hz = map::cutoffHz (v);

    if (hz >= 1000.0f)
        return { juce::String (hz / 1000.0f, hz >= 10000.0f ? 1 : 2), "kHz" };

    return { juce::String (juce::roundToInt (hz)), "Hz" };
}

DisplayValue displayPercent (float v)
{
    return { juce::String (juce::roundToInt (v * 100.0f)), "%" };
}

DisplayValue displayDecay (float v)
{
    const auto s = map::decaySeconds (v);

    if (s >= 1.0f)
        return { juce::String (s, 2), "s" };

    return { juce::String (juce::roundToInt (s * 1000.0f)), "ms" };
}

DisplayValue displayRelease (float v)
{
    const auto s = map::releaseSeconds (v);

    if (s >= 1.0f)
        return { juce::String (s, 2), "s" };

    return { juce::String (juce::roundToInt (s * 1000.0f)), "ms" };
}

DisplayValue displayTone (float v)
{
    return { juce::String (map::toneHz (v) / 1000.0f, 1), "kHz" };
}

DisplayValue displayDetune (float v)
{
    const auto cents = map::detuneCents (v);

    if (cents < 9.95f)
        return { juce::String (cents, 1), "ct" };

    return { juce::String (juce::roundToInt (cents)), "ct" };
}

DisplayValue displayVolume (float dB)
{
    if (dB <= -35.95f)
        return { withMinus ("-inf"), "dB" };

    auto s = juce::String (dB, 1);

    if (s == "-0.0")
        s = "0.0";

    return { withMinus (s), "dB" };
}

DisplayValue displayTempo (float bpm)
{
    return { juce::String (bpm, 1), "BPM" };
}

DisplayValue displaySwing (float percent)
{
    return { juce::String (juce::roundToInt (percent)), "%" };
}

const juce::StringArray& keyNames()
{
    static const juce::StringArray names { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names;
}

const juce::StringArray& soundParameterIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray list;

        for (int v = 0; v < numVoices; ++v)
            for (auto* base : { pid::enabled, pid::waveform, pid::unison, pid::detune, pid::width, pid::filterMode, pid::cutoff,
                                pid::resonance, pid::envMod, pid::decay, pid::release, pid::accent, pid::drive, pid::tone, pid::level })
                list.add (pid::forVoice (v, base));

        list.add (pid::volume);
        return list;
    }();

    return ids;
}

//==============================================================================
namespace
{
    /** Where each voice starts. Only voice 1 sounds at first; 2 and 3 wait with a square and a
        band-passed unison saw, so switching one on adds something straight away. */
    struct VoiceDefaults
    {
        float cutoff, resonance, envMod, decay, release, accent, detune, width, levelDb, drive, tone;
        int waveform, unison, filterMode;
        bool enabled;
    };

    constexpr std::array<VoiceDefaults, numVoices> voiceDefaults {{
        { 0.40f, 0.78f, 0.55f, 0.30f, 0.00f, 0.70f, 0.45f, 0.80f,  0.0f, 0.62f, 0.45f, 0, 0, 0, true  },
        { 0.55f, 0.62f, 0.45f, 0.36f, 0.00f, 0.60f, 0.45f, 0.80f, -6.0f, 0.45f, 0.55f, 1, 0, 0, false },
        { 0.36f, 0.50f, 0.60f, 0.42f, 0.15f, 0.50f, 0.30f, 0.80f, -9.0f, 0.30f, 0.60f, 0, 1, 1, false },
    }};

    // Parameters that shipped in the first release keep version 1; everything added since is 2.
    constexpr int firstRelease = 1, sinceVoices = 2;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto unitParam = [] (const String& id, const String& name, float def, std::function<DisplayValue (float)> fmt, int version)
    {
        auto attrs = AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([fmt] (float v, int) { return fmt (v).toString(); });

        return std::make_unique<AudioParameterFloat> (ParameterID { id, version }, name,
                                                      NormalisableRange<float> (0.0f, 1.0f, 0.0f), def, attrs);
    };

    auto levelParam = [] (const String& id, const String& name, float defDb, int version)
    {
        return std::make_unique<AudioParameterFloat> (
            ParameterID { id, version }, name, NormalisableRange<float> (-36.0f, 6.0f, 0.1f), defDb,
            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return displayVolume (v).toString(); }));
    };

    auto choiceParam = [] (const String& id, const String& name, StringArray choices, int def, int version)
    {
        return std::make_unique<AudioParameterChoice> (ParameterID { id, version }, name, std::move (choices), def);
    };

    auto id   = [] (int v, const char* base)    { return pid::forVoice (v, base); };
    auto name = [] (int v, const char* what)    { return "Voice " + String (v + 1) + " " + what; };

    const StringArray waveforms { "Saw", "Square" }, unisonCounts { "1", "3", "5", "7" }, filterModes { "LP", "BP", "HP", "Notch" };
    const auto& v1 = voiceDefaults[0];

    // Voice 1, in the order it shipped
    layout.add (unitParam (pid::cutoff,    name (0, "Filter"),    v1.cutoff,    displayCutoff,  firstRelease));
    layout.add (unitParam (pid::resonance, name (0, "Resonance"), v1.resonance, displayPercent, firstRelease));
    layout.add (unitParam (pid::envMod,    name (0, "Env mod"),   v1.envMod,    displayPercent, firstRelease));
    layout.add (unitParam (pid::decay,     name (0, "Decay"),     v1.decay,     displayDecay,   firstRelease));
    layout.add (unitParam (pid::accent,    name (0, "Accent"),    v1.accent,    displayPercent, firstRelease));

    // Shipped as the one distortion and tone; now voice 1's. Then the master volume.
    layout.add (unitParam (pid::drive,     name (0, "Distortion"), v1.drive, displayPercent, firstRelease));
    layout.add (unitParam (pid::tone,      name (0, "Tone"),       v1.tone,  displayTone,    firstRelease));
    layout.add (levelParam (pid::volume,   "Master volume",        -6.0f,                    firstRelease));

    layout.add (choiceParam (pid::waveform, name (0, "Waveform"), waveforms, v1.waveform, firstRelease));

    // Unison: detuned copies of the oscillator, spread across the stereo field.
    layout.add (choiceParam (pid::unison, name (0, "Unison"), unisonCounts, v1.unison, sinceVoices));
    layout.add (unitParam (pid::detune, name (0, "Detune"), v1.detune, displayDetune,  sinceVoices));
    layout.add (unitParam (pid::width,  name (0, "Width"),  v1.width,  displayPercent, sinceVoices));

    // Sequencer
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::tempo, 1 }, "Tempo", NormalisableRange<float> (60.0f, 200.0f, 0.5f), 130.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return displayTempo (v).toString(); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::swing, 1 }, "Swing", NormalisableRange<float> (50.0f, 75.0f, 1.0f), 50.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return displaySwing (v).toString(); })));

    layout.add (std::make_unique<AudioParameterBool> (ParameterID { pid::hostSync, 1 }, "Host sync", true));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::key, 1 }, "Key", keyNames(), 0));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { pid::octave, 1 }, "Octave", -1, 2, 0,
        AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int)
        {
            return v > 0 ? "+" + String (v) : withMinus (String (v));
        })));

    // What voice 1 gained with the voices, then all of voices 2 and 3
    for (int v = 0; v < numVoices; ++v)
    {
        const auto& d = voiceDefaults[(size_t) v];

        if (v > 0)
        {
            layout.add (unitParam (id (v, pid::cutoff),    name (v, "Filter"),    d.cutoff,    displayCutoff,  sinceVoices));
            layout.add (unitParam (id (v, pid::resonance), name (v, "Resonance"), d.resonance, displayPercent, sinceVoices));
            layout.add (unitParam (id (v, pid::envMod),    name (v, "Env mod"),   d.envMod,    displayPercent, sinceVoices));
            layout.add (unitParam (id (v, pid::decay),     name (v, "Decay"),     d.decay,     displayDecay,   sinceVoices));
            layout.add (unitParam (id (v, pid::accent),    name (v, "Accent"),    d.accent,    displayPercent, sinceVoices));
            layout.add (choiceParam (id (v, pid::waveform), name (v, "Waveform"), waveforms, d.waveform, sinceVoices));
            layout.add (choiceParam (id (v, pid::unison),   name (v, "Unison"), unisonCounts, d.unison, sinceVoices));
            layout.add (unitParam (id (v, pid::detune),    name (v, "Detune"),    d.detune,    displayDetune,  sinceVoices));
            layout.add (unitParam (id (v, pid::width),     name (v, "Width"),     d.width,     displayPercent, sinceVoices));
            layout.add (unitParam (id (v, pid::drive),     name (v, "Distortion"), d.drive,    displayPercent, sinceVoices));
            layout.add (unitParam (id (v, pid::tone),      name (v, "Tone"),      d.tone,      displayTone,    sinceVoices));
        }

        layout.add (unitParam (id (v, pid::release), name (v, "Release"), d.release, displayRelease, sinceVoices));
        layout.add (levelParam (id (v, pid::level), name (v, "Volume"), d.levelDb, sinceVoices));
        layout.add (choiceParam (id (v, pid::filterMode), name (v, "Filter mode"), filterModes, d.filterMode, sinceVoices));
        layout.add (std::make_unique<AudioParameterBool> (ParameterID { id (v, pid::enabled), sinceVoices },
                                                          name (v, "On"), d.enabled));
    }

    return layout;
}

} // namespace asam
