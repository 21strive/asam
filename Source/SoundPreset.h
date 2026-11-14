#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

/*
    Sound presets: the three voices and the master volume, as plain XML in a .asam file.

        <AsamPreset name="Rubber Squelch" format="1" asam="0.1.0">
          <Sound>
            <PARAM id="cutoff" value="0.42"/>
            ...
          </Sound>
        </AsamPreset>

    Values are the parameters' own units, as in a saved session. A preset sets the whole sound:
    any sound parameter the file does not name (one saved by an older Asam, say) goes back to its
    default, and anything it names that is not part of the sound is ignored.
*/
namespace asam::preset
{
    inline constexpr const char* extension = ".asam";
    inline constexpr int formatVersion = 1;

    /** ~/Library/Audio/Presets/21 Strive/Asam on macOS, shared by the standalone app and the plug-ins. */
    juce::File folder();

    /** The .asam files in the folder, by name. */
    juce::Array<juce::File> list();

    /** The current sound as a preset document. */
    std::unique_ptr<juce::XmlElement> toXml (juce::AudioProcessorValueTreeState& state, const juce::String& name);

    /** Sets the sound from a preset document, as gestures the host can record. Fails, changing
        nothing, if the document is not an Asam sound preset. */
    juce::Result apply (juce::AudioProcessorValueTreeState& state, const juce::XmlElement& preset);

    /** Sets every sound parameter back to its default. */
    void applyDefaults (juce::AudioProcessorValueTreeState& state);

    /** The preset's name: its name attribute, else the file name. */
    juce::String nameOf (const juce::XmlElement& preset, const juce::File& file);
}
