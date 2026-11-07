#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "seq/Pattern.h"
#include "ui/RapiTheme.h"

/**
    The pattern, drawn on a grid the way the Rapi step sequencer sample draws one:
    sixteen steps in four groups, a lamp over each step number (orange on the step that
    is playing), and square cells that follow RButton states.

    Thirteen note rows span one octave (root at the bottom, octave at the top), then
    an Accent row and a Slide row. Click a cell to place the step's note, click it again
    for a rest, drag to draw. Tab in, then arrow keys move and Space toggles.
*/
class StepGrid : public juce::Component
{
public:
    StepGrid();

    void setPattern (const asam::Pattern& p);
    void setPlayingStep (int step);
    void setRoot (int midiRootNote);

    /** Called whenever the user changes a step. */
    std::function<void (int index, asam::Step step)> onStepChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    static constexpr int numNoteRows = asam::octaveSpan + 1;   // 13
    static constexpr int accentRow   = numNoteRows;            // 13
    static constexpr int slideRow    = numNoteRows + 1;        // 14
    static constexpr int numRows     = numNoteRows + 2;        // 15

private:
    struct Cell { int row = -1, col = -1; bool valid() const { return row >= 0 && col >= 0; } };

    juce::Rectangle<float> cellBounds (int row, int col) const;
    juce::Rectangle<float> columnHeaderBounds (int col) const;
    juce::Rectangle<float> rowLabelBounds (int row) const;
    Cell cellAt (juce::Point<float> p) const;
    int noteForRow (int row) const noexcept     { return asam::octaveSpan - row; }
    int rowForNote (int note) const noexcept    { return asam::octaveSpan - note; }
    bool isOn (int row, int col) const;
    void apply (Cell cell, bool fromDrag);
    void changeStep (int col, asam::Step s);
    juce::String noteName (int row) const;

    asam::Pattern pattern {};
    int playing = -1;
    int root = 36;

    // Geometry, computed in resized()
    float stepsLeft = 0.0f, cellW = 0.0f, headerTop = 0.0f, headerH = 36.0f;
    float notesTop = 0.0f, rowPitch = 20.0f, extrasTop = 0.0f, extraH = 28.0f, extraGap = 4.0f;
    float labelLeft = 0.0f, labelW = 52.0f;
    std::array<float, asam::numSteps> colX {};

    // Interaction
    enum class DragMode { none, setNote, erase, setFlag, clearFlag };
    DragMode dragMode = DragMode::none;
    int lastDragCol = -1;
    Cell hover, cursor { 0, 0 };
    bool focusVisible = false;
};
