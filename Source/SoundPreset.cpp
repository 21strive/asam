#include "SoundPreset.h"
#include "Params.h"

namespace asam::preset
{

namespace
{
    void setAsGesture (juce::RangedAudioParameter& p, float realValue)
    {
        const auto normalised = p.convertTo0to1 (p.getNormalisableRange().snapToLegalValue (realValue));

        if (juce::approximatelyEqual (normalised, p.getValue()))
            return;

        p.beginChangeGesture();
        p.setValueNotifyingHost (normalised);
        p.endChangeGesture();
    }
}

juce::File folder()
{
   #if JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets/21 Strive/Asam");
   #else
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("21 Strive/Asam Presets");
   #endif
}

juce::Array<juce::File> list()
{
    auto files = folder().findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension);

    std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
    {
        return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0;
    });

    return files;
}

std::unique_ptr<juce::XmlElement> toXml (juce::AudioProcessorValueTreeState& state, const juce::String& name)
{
    auto xml = std::make_unique<juce::XmlElement> ("AsamPreset");
    xml->setAttribute ("name", name);
    xml->setAttribute ("format", formatVersion);
   #ifdef JucePlugin_VersionString
    xml->setAttribute ("asam", JucePlugin_VersionString);
   #endif

    auto* sound = xml->createNewChildElement ("Sound");

    for (auto& id : soundParameterIds())
    {
        if (auto* raw = state.getRawParameterValue (id))
        {
            auto* param = sound->createNewChildElement ("PARAM");
            param->setAttribute ("id", id);
            param->setAttribute ("value", (double) raw->load());
        }
    }

    return xml;
}

juce::Result apply (juce::AudioProcessorValueTreeState& state, const juce::XmlElement& preset)
{
    if (! preset.hasTagName ("AsamPreset"))
        return juce::Result::fail ("This file is not an Asam sound preset.");

    auto* sound = preset.getChildByName ("Sound");

    if (sound == nullptr)
        return juce::Result::fail ("This preset holds no sound.");

    if (preset.getIntAttribute ("format", 1) > formatVersion)
        return juce::Result::fail ("This preset was saved by a newer Asam.");

    // What the file names, then defaults for the rest of the sound.
    std::map<juce::String, float> values;

    for (auto* param : sound->getChildWithTagNameIterator ("PARAM"))
        if (soundParameterIds().contains (param->getStringAttribute ("id")))
            values[param->getStringAttribute ("id")] = (float) param->getDoubleAttribute ("value");

    for (auto& id : soundParameterIds())
    {
        if (auto* p = state.getParameter (id))
        {
            const auto it = values.find (id);
            setAsGesture (*p, it != values.end() ? it->second : p->convertFrom0to1 (p->getDefaultValue()));
        }
    }

    return juce::Result::ok();
}

void applyDefaults (juce::AudioProcessorValueTreeState& state)
{
    for (auto& id : soundParameterIds())
        if (auto* p = state.getParameter (id))
            setAsGesture (*p, p->convertFrom0to1 (p->getDefaultValue()));
}

juce::String nameOf (const juce::XmlElement& preset, const juce::File& file)
{
    const auto name = preset.getStringAttribute ("name").trim();
    return name.isNotEmpty() ? name : file.getFileNameWithoutExtension();
}

} // namespace asam::preset
