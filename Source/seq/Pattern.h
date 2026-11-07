#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <vector>

namespace asam
{

inline constexpr int numSteps   = 16;
inline constexpr int octaveSpan = 12;   // a step's note sits 0..12 semitones above the root

//==============================================================================
/** One step of an acid line. Slide on a step means: hold the gate and glide into the next step. */
struct Step
{
    int  note   = 0;
    bool gate   = false;
    bool accent = false;
    bool slide  = false;

    std::uint32_t pack() const noexcept
    {
        return (std::uint32_t) (note & 0x0f)
             | (gate   ? 0x10u : 0u)
             | (accent ? 0x20u : 0u)
             | (slide  ? 0x40u : 0u);
    }

    static Step unpack (std::uint32_t v) noexcept
    {
        Step s;
        s.note   = juce::jlimit (0, octaveSpan, (int) (v & 0x0f));
        s.gate   = (v & 0x10u) != 0;
        s.accent = (v & 0x20u) != 0;
        s.slide  = (v & 0x40u) != 0;
        return s;
    }

    bool operator== (const Step&) const = default;
};

using Pattern = std::array<Step, numSteps>;

struct PatternPreset
{
    juce::String name;
    Pattern steps;
};

/** Parses 16 space-separated tokens: a note 0..12 with optional 'a' (accent) and 's' (slide),
    or '-' for a rest. Example: "0a 0 12s 0 - 3 ...". */
Pattern parsePattern (const juce::String& text);

/** The inverse of parsePattern(), used for saving state. */
juce::String patternToString (const Pattern& pattern);

/** The built-in rhythm presets. All original, all inside one octave. */
const std::vector<PatternPreset>& patternPresets();

/** A fresh acid line inside one octave: minor or phrygian, weighted towards the root. */
Pattern randomPattern (juce::Random& random);

int countNotes   (const Pattern& p);
int countAccents (const Pattern& p);
int countSlides  (const Pattern& p);

} // namespace asam
