#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
    The Lucide icons Rapi uses (lucide.dev, ISC licence), as 24 x 24 outline paths.
    They are stroked, never filled: 2 units wide with round caps and joins.
*/
namespace rapi::icons
{
    const juce::Path& play();
    const juce::Path& pause();
    const juce::Path& square();
    const juce::Path& chevronLeft();
    const juce::Path& chevronRight();
    const juce::Path& chevronDown();
    const juce::Path& check();
    const juce::Path& eraser();
    const juce::Path& shuffle();
    const juce::Path& settings();
    const juce::Path& sun();
    const juce::Path& moon();

    /** Strokes a 24-unit icon into a square of the given size. */
    void draw (juce::Graphics& g, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour colour);
}
