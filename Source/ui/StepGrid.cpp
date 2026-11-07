#include "StepGrid.h"
#include "ui/RapiWidgets.h"

using namespace rapi;

namespace
{
    constexpr float groupGap = 12.0f;
    constexpr float cellGap  = 4.0f;
    constexpr float rowGap   = 2.0f;
    constexpr float sectionGap = 12.0f;
}

StepGrid::StepGrid()
{
    setWantsKeyboardFocus (true);
    setMouseClickGrabsKeyboardFocus (false);
    setTitle ("Pattern: 16 steps, one octave. Arrow keys move, Space toggles.");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void StepGrid::setPattern (const asam::Pattern& p)
{
    if (p != pattern)
    {
        pattern = p;
        repaint();
    }
}

void StepGrid::setPlayingStep (int step)
{
    if (step != playing)
    {
        playing = step;
        repaint();
    }
}

void StepGrid::setRoot (int midiRootNote)
{
    if (midiRootNote != root)
    {
        root = midiRootNote;
        repaint();
    }
}

//==============================================================================
void StepGrid::resized()
{
    const auto b = getLocalBounds().toFloat();
    constexpr float padX = 16.0f, padTop = 8.0f, padBottom = 16.0f;

    labelLeft = b.getX() + padX;
    stepsLeft = labelLeft + labelW + 16.0f;

    const auto stepsW = b.getRight() - padX - stepsLeft;
    cellW = (stepsW - 3.0f * groupGap - 12.0f * cellGap) / (float) asam::numSteps;

    for (int c = 0; c < asam::numSteps; ++c)
    {
        const auto group = c / 4;
        colX[(size_t) c] = stepsLeft + (float) c * cellW + (float) group * groupGap + (float) (group * 3 + c % 4) * cellGap;
    }

    headerTop = b.getY() + padTop;
    headerH   = 36.0f;

    const auto bodyTop = headerTop + headerH + 10.0f;
    const auto avail   = b.getBottom() - padBottom - bodyTop;

    rowPitch  = std::floor ((avail + rowGap - sectionGap - 2.0f * extraH - extraGap) / (float) numNoteRows);
    notesTop  = bodyTop;
    extrasTop = notesTop + (float) numNoteRows * rowPitch - rowGap + sectionGap;
}

juce::Rectangle<float> StepGrid::cellBounds (int row, int col) const
{
    const auto x = colX[(size_t) col];

    if (row < numNoteRows)
        return { x, notesTop + (float) row * rowPitch, cellW, rowPitch - rowGap };

    if (row == accentRow)
        return { x, extrasTop, cellW, extraH };

    return { x, extrasTop + extraH + extraGap, cellW, extraH };
}

juce::Rectangle<float> StepGrid::columnHeaderBounds (int col) const
{
    return { colX[(size_t) col], headerTop, cellW, headerH };
}

juce::Rectangle<float> StepGrid::rowLabelBounds (int row) const
{
    const auto cell = cellBounds (row, 0);
    return { labelLeft, cell.getY(), labelW, cell.getHeight() };
}

StepGrid::Cell StepGrid::cellAt (juce::Point<float> p) const
{
    for (int row = 0; row < numRows; ++row)
        for (int col = 0; col < asam::numSteps; ++col)
            if (cellBounds (row, col).contains (p))
                return { row, col };

    return {};
}

bool StepGrid::isOn (int row, int col) const
{
    const auto& s = pattern[(size_t) col];

    if (row < numNoteRows)  return s.gate && s.note == noteForRow (row);
    if (row == accentRow)   return s.accent;
    return s.slide;
}

juce::String StepGrid::noteName (int row) const
{
    return juce::MidiMessage::getMidiNoteName (root + noteForRow (row), true, true, 3);
}

//==============================================================================
void StepGrid::paint (juce::Graphics& g)
{
    const auto& c = colours();
    const auto b  = getLocalBounds().toFloat();

    // Head row: the column label, step numbers with their lamps, and a rule under them.
    g.setColour (c.border);
    g.fillRect (labelLeft, headerTop + headerH, b.getRight() - 16.0f - labelLeft, 1.0f);

    drawText (g, "NOTE", { labelLeft, headerTop, labelW, headerH }, type::label(), c.textTertiary);

    const auto numberFont = type::index();

    for (int col = 0; col < asam::numSteps; ++col)
    {
        const auto area = columnHeaderBounds (col);
        const bool beat = col % 4 == 0;
        const bool now  = col == playing;
        const auto top  = area.getCentreY() - 12.0f;

        g.setColour (now ? c.signal : c.statusOff);
        g.fillRect (area.getCentreX() - 1.0f, top, 2.0f, 10.0f);

        drawText (g, juce::String (col + 1).paddedLeft ('0', 2), { area.getX(), top + 14.0f, area.getWidth(), 10.0f },
                  numberFont, beat ? c.textSecondary : c.textTertiary, juce::Justification::centred, false);
    }

    // Row labels: pitch names inside the octave, the root and its octave a step brighter.
    for (int row = 0; row < numNoteRows; ++row)
    {
        const bool rootRow = row == 0 || row == asam::octaveSpan;
        drawText (g, noteName (row), rowLabelBounds (row), numberFont, rootRow ? c.textSecondary : c.textTertiary);
    }

    const auto rowLabelFont = font (Family::mono, Weight::medium, 10.0f, 0.06f);
    drawText (g, "ACCENT", rowLabelBounds (accentRow), rowLabelFont, c.textTertiary);
    drawText (g, "SLIDE",  rowLabelBounds (slideRow),  rowLabelFont, c.textTertiary);

    // A subtle rule between the notes and the accent and slide rows
    g.setColour (c.borderSubtle);
    g.fillRect (labelLeft, extrasTop - sectionGap * 0.5f - 0.5f, b.getRight() - 16.0f - labelLeft, 1.0f);

    // Cells, with RButton states as in the Rapi step sequencer:
    // on + playing: solid signal, on: solid brand, on but on a rest: outline brand,
    // playing column: soft signal, beat column: soft neutral, otherwise outline neutral.
    for (int col = 0; col < asam::numSteps; ++col)
    {
        const auto& step = pattern[(size_t) col];
        const bool now  = col == playing;
        const bool beat = col % 4 == 0;

        for (int row = 0; row < numRows; ++row)
        {
            const auto r     = cellBounds (row, col);
            const bool on    = isOn (row, col);
            const bool muted = row >= accentRow && on && ! step.gate;
            const bool hot   = hover.row == row && hover.col == col;

            juce::Colour fill, border;

            if (on && now && ! muted)   fill   = hot ? c.signalTone.solidHover : c.signalTone.solid;
            else if (muted)             border = c.brandTone.solid;
            else if (on)                fill   = hot ? c.brandTone.solidHover : c.brandTone.solid;
            else if (now)               fill   = hot ? c.signalTone.softHover : c.signalTone.soft;
            else if (beat)              fill   = hot ? c.bgPressed : c.bgHover;
            else
            {
                border = hot ? c.textTertiary : c.borderStrong;

                if (hot)
                    fill = c.bgHover;
            }

            if (! fill.isTransparent())
            {
                g.setColour (fill);
                g.fillRect (r);
            }

            if (! border.isTransparent())
                drawBorder (g, r, border);
        }
    }

    if (focusVisible && cursor.valid())
        drawFocusRing (g, cellBounds (cursor.row, cursor.col));
}

//==============================================================================
void StepGrid::changeStep (int col, asam::Step s)
{
    if (pattern[(size_t) col] == s)
        return;

    pattern[(size_t) col] = s;
    repaint();

    if (onStepChanged != nullptr)
        onStepChanged (col, s);
}

void StepGrid::apply (Cell cell, bool)
{
    auto s = pattern[(size_t) cell.col];

    switch (dragMode)
    {
        case DragMode::setNote:
            s.gate = true;
            s.note = noteForRow (juce::jlimit (0, numNoteRows - 1, cell.row));
            break;

        case DragMode::erase:
            s.gate = false;
            break;

        case DragMode::setFlag:
        case DragMode::clearFlag:
        {
            const bool value = dragMode == DragMode::setFlag;

            if (cursor.row == accentRow) s.accent = value;
            else                         s.slide  = value;

            break;
        }

        case DragMode::none:
            return;
    }

    changeStep (cell.col, s);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto cell = cellAt (e.position);

    if (! cell.valid())
        return;

    cursor = cell;
    lastDragCol = cell.col;

    if (cell.row < numNoteRows)
        dragMode = isOn (cell.row, cell.col) ? DragMode::erase : DragMode::setNote;
    else
        dragMode = isOn (cell.row, cell.col) ? DragMode::clearFlag : DragMode::setFlag;

    apply (cell, false);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragMode == DragMode::none)
        return;

    // Nearest column, even when the pointer is in a gap.
    int col = 0;
    float best = std::numeric_limits<float>::max();

    for (int c = 0; c < asam::numSteps; ++c)
    {
        const auto d = std::abs (e.position.x - (colX[(size_t) c] + cellW * 0.5f));

        if (d < best)
        {
            best = d;
            col = c;
        }
    }

    int row = cursor.row;

    if (dragMode == DragMode::setNote)
        row = juce::jlimit (0, numNoteRows - 1, (int) std::floor ((e.position.y - notesTop) / rowPitch));

    if (col == lastDragCol && (dragMode != DragMode::setNote || isOn (row, col)))
        return;

    lastDragCol = col;
    apply ({ row, col }, true);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::none;
    lastDragCol = -1;
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    const auto cell = cellAt (e.position);

    if (cell.row != hover.row || cell.col != hover.col)
    {
        hover = cell;
        repaint();
    }
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hover = {};
    repaint();
}

bool StepGrid::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::leftKey)  { cursor.col = (cursor.col + asam::numSteps - 1) % asam::numSteps; repaint(); return true; }
    if (code == juce::KeyPress::rightKey) { cursor.col = (cursor.col + 1) % asam::numSteps; repaint(); return true; }
    if (code == juce::KeyPress::upKey)    { cursor.row = juce::jmax (0, cursor.row - 1); repaint(); return true; }
    if (code == juce::KeyPress::downKey)  { cursor.row = juce::jmin (numRows - 1, cursor.row + 1); repaint(); return true; }

    if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey)
    {
        if (cursor.row < numNoteRows)
            dragMode = isOn (cursor.row, cursor.col) ? DragMode::erase : DragMode::setNote;
        else
            dragMode = isOn (cursor.row, cursor.col) ? DragMode::clearFlag : DragMode::setFlag;

        apply (cursor, false);
        dragMode = DragMode::none;
        return true;
    }

    return false;
}

void StepGrid::focusGained (FocusChangeType cause)
{
    focusVisible = cause == focusChangedByTabKey;
    repaint();
}

void StepGrid::focusLost (FocusChangeType)
{
    focusVisible = false;
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> StepGrid::createAccessibilityHandler()
{
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group);
}
