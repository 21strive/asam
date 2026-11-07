#include "Pattern.h"

namespace asam
{

Pattern parsePattern (const juce::String& text)
{
    Pattern pattern {};
    auto tokens = juce::StringArray::fromTokens (text, " \t\n|", "");
    tokens.removeEmptyStrings();

    for (int i = 0; i < numSteps && i < tokens.size(); ++i)
    {
        const auto token = tokens[i].trim().toLowerCase();
        auto& step = pattern[(size_t) i];

        if (token.isEmpty() || token.startsWithChar ('-'))
        {
            step = {};
            continue;
        }

        step.gate   = true;
        step.note   = juce::jlimit (0, octaveSpan, token.getIntValue());
        step.accent = token.containsChar ('a');
        step.slide  = token.containsChar ('s');
    }

    return pattern;
}

juce::String patternToString (const Pattern& pattern)
{
    juce::StringArray tokens;

    for (const auto& step : pattern)
    {
        if (! step.gate)
        {
            tokens.add ("-");
            continue;
        }

        auto token = juce::String (step.note);
        if (step.accent) token << "a";
        if (step.slide)  token << "s";
        tokens.add (token);
    }

    return tokens.joinIntoString (" ");
}

const std::vector<PatternPreset>& patternPresets()
{
    // Sixteen original lines. Notes are semitones above the root (0..12), so every
    // pattern stays inside one octave; 'a' is accent, 's' slides into the next step.
    static const std::vector<PatternPreset> presets
    {
        { "Warehouse",  parsePattern ("0a 0 12s 0   -  0a 3 0    0 12a 0 10s  7 0a 3s 0") },
        { "Squelch",    parsePattern ("0 3s 7 0a    0 12s 10 7a  0 3s 5 0a    12 10s 7a 3") },
        { "Rave alarm", parsePattern ("12a 0 12 0s  12a 0 10 0s  12a 0 7 0s   12a 0 5 3s") },
        { "Rubber",     parsePattern ("0 12s 0 0a   - 12s 0 3    0 12s 0 0a   - 10s 12 7") },
        { "Phrygian",   parsePattern ("0a 1 0 -     0 1s 0a 12   0 1 0 -      7a 6s 1 0") },
        { "Rolling",    parsePattern ("0 0a 12 0    0 0a 10 0    0 0a 7 0     3 0a 5s 7") },
        { "Stab",       parsePattern ("0a - - 0     - - 12as 0   - 0a - -     3 - 0s 12a") },
        { "Climb",      parsePattern ("0 0a 3 3     5s 7 7a 10   0 3a 5 7s    10 12a 10s 7") },
        { "Fall",       parsePattern ("12a 10 12 7s 10 7a 5 3s   7 5a 3 1s    0a 0 12s 0") },
        { "Hypno",      parsePattern ("0 3a 0 3     0 3as 3 0    0 3a 0 3     0 7as 5 3") },
        { "Gallop",     parsePattern ("0a 0 0s 12   0a 0 0s 10   0a 0 0s 7    0a 3s 5 7") },
        { "Midnight",   parsePattern ("0a - 6s 7    0 - 3a -     0a 12 10s 6  7 - 3s 0") },
        { "Acid rain",  parsePattern ("12 7s 10 12a 3 5 7a 12s   10 7 5a 3s   5 7 10a 12s") },
        { "Snake",      parsePattern ("0a 1s 2 3s   0a 12s 11 10s 0a 1s 2 3s  7a 6s 5 3s") },
        { "Pump",       parsePattern ("0 0a - 0     0 0a 12s 0   0 0a - 0     0 0a 10s 7") },
        { "Malam",      parsePattern ("0a 7 12s 10  7a 3 5s 7    0a 7 12s 10  12a 10s 7 3") },
    };

    return presets;
}

Pattern randomPattern (juce::Random& random)
{
    static const std::array<int, 6> minorPentatonic { 0, 3, 5, 7, 10, 12 };
    static const std::array<int, 8> phrygian        { 0, 1, 3, 5, 7, 8, 10, 12 };

    const bool usePhrygian = random.nextFloat() < 0.35f;
    Pattern pattern {};

    for (int i = 0; i < numSteps; ++i)
    {
        auto& step = pattern[(size_t) i];
        const bool downbeat = (i % 4) == 0;

        step.gate = i == 0 || random.nextFloat() < (downbeat ? 0.92f : 0.74f);

        if (! step.gate)
            continue;

        const auto r = random.nextFloat();

        if (r < 0.38f)
            step.note = 0;
        else if (r < 0.54f)
            step.note = 12;
        else if (usePhrygian)
            step.note = phrygian[(size_t) random.nextInt ((int) phrygian.size())];
        else
            step.note = minorPentatonic[(size_t) random.nextInt ((int) minorPentatonic.size())];

        step.accent = random.nextFloat() < (downbeat ? 0.22f : 0.32f);
        step.slide  = random.nextFloat() < 0.22f;
    }

    return pattern;
}

int countNotes (const Pattern& p)
{
    return (int) std::count_if (p.begin(), p.end(), [] (const Step& s) { return s.gate; });
}

int countAccents (const Pattern& p)
{
    return (int) std::count_if (p.begin(), p.end(), [] (const Step& s) { return s.gate && s.accent; });
}

int countSlides (const Pattern& p)
{
    return (int) std::count_if (p.begin(), p.end(), [] (const Step& s) { return s.gate && s.slide; });
}

} // namespace asam
