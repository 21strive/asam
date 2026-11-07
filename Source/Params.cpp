#include "Params.h"

namespace asam
{

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

DisplayValue displayTone (float v)
{
    return { juce::String (map::toneHz (v) / 1000.0f, 1), "kHz" };
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

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    auto unitParam = [] (const char* id, const char* name, float def, std::function<DisplayValue (float)> fmt)
    {
        auto attrs = AudioParameterFloatAttributes()
                         .withStringFromValueFunction ([fmt] (float v, int) { return fmt (v).toString(); });

        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                      NormalisableRange<float> (0.0f, 1.0f, 0.0f), def, attrs);
    };

    // Voice
    layout.add (unitParam (pid::cutoff,    "Filter",     0.40f, displayCutoff));
    layout.add (unitParam (pid::resonance, "Resonance",  0.78f, displayPercent));
    layout.add (unitParam (pid::envMod,    "Env mod",    0.55f, displayPercent));
    layout.add (unitParam (pid::decay,     "Decay",      0.30f, displayDecay));
    layout.add (unitParam (pid::accent,    "Accent",     0.70f, displayPercent));

    // Drive
    layout.add (unitParam (pid::drive,     "Distortion", 0.62f, displayPercent));
    layout.add (unitParam (pid::tone,      "Tone",       0.45f, displayTone));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::volume, 1 }, "Volume", NormalisableRange<float> (-36.0f, 6.0f, 0.1f), -6.0f,
        AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return displayVolume (v).toString(); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pid::waveform, 1 }, "Waveform",
                                                        StringArray { "Saw", "Square" }, 0));

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

    return layout;
}

} // namespace asam
