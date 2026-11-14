#include "PluginEditor.h"
#include "SoundPreset.h"
#include "dsp/FilterResponse.h"

using namespace rapi;
using namespace asam;

namespace
{
    constexpr int W = AsamEditor::logicalWidth;
    constexpr int H = AsamEditor::logicalHeight;

    constexpr int stripH    = 32;
    constexpr int headerH   = 56;
    constexpr int pagePad   = 16;
    constexpr int consoleW  = 320;
    constexpr int panelHead = 41;

    // The console keeps the same three cells on both pages: a tall one, a list, the transport.
    constexpr int topCellH  = 297;
    constexpr int listCellH = 219;

    enum Page { soundPage = 0, patternPage = 1 };

    constexpr const char* qwertyKeys = "awsedftgyhujk";

    ValueText toValueText (const DisplayValue& d)   { return { d.number, d.unit }; }

    juce::RangedAudioParameter& parameter (AsamProcessor& proc, const juce::String& id)
    {
        return *proc.getState().getParameter (id);
    }

    float rawValue (AsamProcessor& proc, const juce::String& id)
    {
        return proc.getState().getRawParameterValue (id)->load();
    }

    /** The Asam mark, Luncur: a low note slides up to its octave, drawn on the 21 Strive
        construction (each note one stem wide, 46.5; the slide rises 50 over 54.3). 147.3 x 100. */
    constexpr float asamMarkWidth = 147.3f;

    const juce::Path& asamMarkPath()
    {
        static const juce::Path p = juce::Drawable::parseSVGPath ("M0 50H46.5L100.8 0H147.3V50H100.8L46.5 100H0Z");
        return p;
    }

    /** Draws the Asam mark `height` tall with its left edge at x, and returns its width. */
    float drawAsamMark (juce::Graphics& g, float x, float centreY, float height, juce::Colour colour)
    {
        const auto scale = height / 100.0f;
        g.setColour (colour);
        g.fillPath (asamMarkPath(), juce::AffineTransform::scale (scale).translated (x, centreY - height * 0.5f));
        return asamMarkWidth * scale;
    }

    float drawBadge (juce::Graphics& g, float x, float centreY, const juce::String& text, const ToneColours& tone)
    {
        const auto f = font (Family::mono, Weight::medium, 11.0f, 0.06f);
        const auto label = text.toUpperCase();
        const auto w = std::ceil (textWidth (f, label)) + 12.0f;
        const juce::Rectangle<float> r (x, centreY - 10.0f, w, 20.0f);
        g.setColour (tone.soft);
        g.fillRect (r);
        drawText (g, label, r, f, tone.text, juce::Justification::centred, false);
        return w;
    }

    juce::String twoDigits (int v)  { return juce::String (v).paddedLeft ('0', 2); }

    /** Width of a pixel-font figure: digits take a fixed 0.65 em cell (.r-readout__cell). */
    float pixelFigureWidth (const juce::String& text, const juce::Font& f, float em)
    {
        float w = 0.0f;

        for (auto ch : text)
            w += juce::CharacterFunctions::isDigit (ch) ? 0.65f * em : textWidth (f, juce::String::charToString (ch));

        return w;
    }

    /** Draws a pixel-font figure right-aligned at rightX, each digit centred in its cell. */
    void drawPixelFigure (juce::Graphics& g, const juce::String& text, float rightX, float baseline,
                          const juce::Font& f, float em, juce::Colour colour)
    {
        auto x = rightX - pixelFigureWidth (text, f, em);
        g.setColour (colour);

        for (auto ch : text)
        {
            const auto s = juce::String::charToString (ch);
            const auto glyphW = textWidth (f, s);
            const auto cellW = juce::CharacterFunctions::isDigit (ch) ? 0.65f * em : glyphW;

            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (f, s, x + (cellW - glyphW) * 0.5f, baseline);
            glyphs.draw (g);
            x += cellW;
        }
    }

    juce::StringArray octaveOptions()
    {
        juce::StringArray options;
        options.add (withMinus ("-1"));
        options.add ("0");
        options.add ("+1");
        options.add ("+2");
        return options;
    }

    /** A kicker for a console cell: the running number, then the name in mono capitals. */
    void drawKicker (juce::Graphics& g, juce::Rectangle<int> cell, const juce::String& index, const juce::String& title)
    {
        const auto& c = colours();
        const auto line = juce::Rectangle<float> ((float) cell.getX() + 16.0f, (float) cell.getY() + 16.0f, (float) cell.getWidth() - 32.0f, 15.0f);
        drawText (g, index, line, type::index(), c.textTertiary);
        drawText (g, title.toUpperCase(), line.withTrimmedLeft (textWidth (type::index(), index) + 10.0f), type::eyebrow(), c.textSecondary);
    }
}

//==============================================================================
/** The preset list from the console, as in the Rapi sequencer's pattern bank. */
class BankList : public juce::Component,
                 public rapi::FocusVisible
{
public:
    BankList()
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Pattern bank");
    }

    std::function<void (int)> onSelect;

    void setCurrent (int index)
    {
        if (index != current)
        {
            current = index;
            repaint();
        }
    }

    static constexpr int rowH = 20;

    juce::Rectangle<float> rowBounds (int i) const
    {
        const auto colW = ((float) getWidth() - 16.0f) * 0.5f;
        const auto col  = i / 8;
        const auto row  = i % 8;
        return { (float) col * (colW + 16.0f), (float) (row * rowH), colW, (float) rowH };
    }

    int rowAt (juce::Point<float> p) const
    {
        for (int i = 0; i < (int) patternPresets().size(); ++i)
            if (rowBounds (i).contains (p))
                return i;

        return -1;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = colours();
        const auto& presets = patternPresets();

        for (int i = 0; i < (int) presets.size(); ++i)
        {
            const auto r = rowBounds (i);
            const bool isCurrent = i == current;
            const bool isHot = i == hovered;

            if (isCurrent)
            {
                // The selected row: a cobalt tint with a 2 px leading edge.
                g.setColour (c.bgSelected);
                g.fillRect (r);
                g.setColour (c.brand);
                g.fillRect (r.withWidth (2.0f));
            }
            else if (isHot)
            {
                g.setColour (c.bgHover);
                g.fillRect (r);
            }

            const auto colour = (isCurrent || isHot) ? c.text : c.textTertiary;
            auto content = r.withTrimmedLeft (8.0f).withTrimmedRight (6.0f);

            const auto idFont = font (Family::mono, Weight::semibold, 12.0f, 0.0f, true);
            drawText (g, twoDigits (i + 1), content.removeFromLeft (20.0f), idFont, colour);
            content.removeFromLeft (8.0f);

            const auto countFont = font (Family::mono, Weight::regular, 11.0f, 0.0f, true);
            const auto count = juce::String (countNotes (presets[(size_t) i].steps));
            drawText (g, count, content.removeFromRight (18.0f), countFont, colour, juce::Justification::centredRight);

            drawText (g, presets[(size_t) i].name, content, font (Family::sans, Weight::regular, 12.0f), colour);
        }

        if (focusVisible && juce::isPositiveAndBelow (cursor, (int) presets.size()))
            drawFocusRing (g, rowBounds (cursor).reduced (2.0f), 0.0f);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const auto h = rowAt (e.position);

        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        hovered = -1;
        repaint();
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto i = rowAt (e.position);

        if (i >= 0 && onSelect != nullptr)
            onSelect (i);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto n = (int) patternPresets().size();
        const auto code = key.getKeyCode();
        auto next = cursor;

        if (code == juce::KeyPress::upKey)          next = (cursor + n - 1) % n;
        else if (code == juce::KeyPress::downKey)   next = (cursor + 1) % n;
        else if (code == juce::KeyPress::leftKey)   next = (cursor + n - 8) % n;
        else if (code == juce::KeyPress::rightKey)  next = (cursor + 8) % n;
        else return false;

        cursor = next;

        if (onSelect != nullptr)
            onSelect (cursor);

        repaint();
        return true;
    }

    void focusGained (FocusChangeType cause) override
    {
        cursor = juce::jmax (0, current);
        updateFocus (*this, true, cause);
    }

    void focusLost (FocusChangeType cause) override     { updateFocus (*this, false, cause); }

    juce::Rectangle<float> getFocusRingArea() const override { return {}; }

private:
    int current = 0, hovered = -1, cursor = 0;
};

//==============================================================================
/**
    A voice's filter as a graph, the way a filter plug-in draws one: frequency across on a log
    scale from 20 Hz to 20 kHz, level up, the response of the chosen mode in cobalt, and a point
    on the curve at the cutoff.

    Click anywhere to put the cutoff there, drag across to move it and up or down for resonance
    (Shift for fine), scroll for resonance, double-click to reset. With focus, the arrows move the
    cutoff by a semitone and the resonance by a percent.

    While the voice sounds, a second curve in the signal colour shows where the envelope and
    accent have the filter at that moment.
*/
class FilterGraph : public juce::Component,
                    public rapi::FocusVisible
{
public:
    FilterGraph (AsamProcessor& p, int voiceIndex)
        : cutoffParam (parameter (p, pid::forVoice (voiceIndex, pid::cutoff))),
          resonanceParam (parameter (p, pid::forVoice (voiceIndex, pid::resonance))),
          modeParam (parameter (p, pid::forVoice (voiceIndex, pid::filterMode))),
          cutoffAttachment (cutoffParam, [this] (float v) { cutoff = v; repaint(); }),
          resonanceAttachment (resonanceParam, [this] (float v) { resonance = v; repaint(); }),
          modeAttachment (modeParam, [this] (float v) { mode = juce::jlimit (0, 3, juce::roundToInt (v)); repaint(); })
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::CrosshairCursor);
        setTitle ("Voice " + juce::String (voiceIndex + 1) + " filter");
        setDescription ("Drag across for the cutoff, up and down for resonance");

        cutoffAttachment.sendInitialUpdate();
        resonanceAttachment.sendInitialUpdate();
        modeAttachment.sendInitialUpdate();
    }

    static constexpr float minHz = 20.0f, maxHz = 20000.0f, topDb = 24.0f, bottomDb = -36.0f;

    /** Where the voice's filter is right now and how loud the voice is, from the timer. */
    void setLive (float hz, float level)
    {
        if (std::abs (hz - liveHz) > 0.5f || std::abs (level - liveLevel) > 0.004f)
        {
            liveHz = hz;
            liveLevel = level;
            repaint();
        }
    }

    float getCutoffHz() const noexcept  { return map::cutoffHz (cutoff); }

    juce::Rectangle<float> plot() const { return getLocalBounds().toFloat().reduced (1.0f); }

    float xForHz (float hz) const
    {
        const auto p = plot();
        return p.getX() + p.getWidth() * std::log (hz / minHz) / std::log (maxHz / minHz);
    }

    float hzForX (float x) const
    {
        const auto p = plot();
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, (x - p.getX()) / p.getWidth()));
    }

    float yForDb (float db) const
    {
        const auto p = plot();
        return p.getY() + p.getHeight() * (topDb - db) / (topDb - bottomDb);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = colours();
        const auto b  = getLocalBounds().toFloat();
        const auto p  = plot();

        g.setColour (c.bgMuted);
        g.fillRect (b);
        drawBorder (g, b, c.border);

        // Grid: 50 Hz to 10 kHz in 1-2-5 steps, and every 12 dB, with 0 dB on the axis colour
        g.setColour (c.chartGrid);

        for (auto f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
            g.fillRect (std::round (xForHz (f)), p.getY(), 1.0f, p.getHeight());

        for (auto db : { 12.0f, -12.0f, -24.0f })
            g.fillRect (p.getX(), std::round (yForDb (db)), p.getWidth(), 1.0f);

        g.setColour (c.chartAxis);
        g.fillRect (p.getX(), std::round (yForDb (0.0f)), p.getWidth(), 1.0f);

        const auto labelFont = font (Family::mono, Weight::regular, 10.0f, 0.02f, true);

        for (auto [f, text] : { std::pair<float, const char*> { 100.0f, "100" }, { 1000.0f, "1k" }, { 10000.0f, "10k" } })
            drawText (g, text, { xForHz (f) + 4.0f, p.getBottom() - 15.0f, 32.0f, 12.0f }, labelFont, c.chartLabel);

        // The response at the set cutoff: a cobalt line over a quiet fill, kept inside the plot
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (p.getSmallestIntegerContainer());

            const auto curve = responsePath (getCutoffHz());
            auto area = curve;
            area.lineTo (p.getRight(), p.getBottom());
            area.lineTo (p.getX(), p.getBottom());
            area.closeSubPath();
            g.setColour (c.chart1.withAlpha (0.12f));
            g.fillPath (area);
            g.setColour (c.chart1);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Where the envelope has the filter now, while the voice sounds
            if (isEnabled() && liveLevel > 0.002f && std::abs (std::log2 (liveHz / getCutoffHz())) > 0.02f)
            {
                g.setColour (c.signal.withAlpha (juce::jlimit (0.15f, 0.9f, liveLevel * 1.4f)));
                g.strokePath (responsePath (liveHz), juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
        }

        // The point at the cutoff, on the curve (.r-chart__point: ringed in the chart surface)
        const auto handle = handlePosition();
        const auto r = (dragging || isMouseOver()) && isEnabled() ? 6.0f : 5.0f;
        g.setColour (c.chart1);
        g.fillEllipse (handle.x - r, handle.y - r, 2.0f * r, 2.0f * r);
        g.setColour (c.bgSurface);
        g.drawEllipse (handle.x - r, handle.y - r, 2.0f * r, 2.0f * r, 2.0f);

        // Readouts in the top corners: the chart's unit and value styles
        const auto unitFont  = font (Family::mono, Weight::semibold, 9.5f, 0.04f);
        const auto valueFont = font (Family::mono, Weight::semibold, 10.5f, 0.0f, true);
        const auto top = juce::Rectangle<float> (p.getX() + 8.0f, p.getY() + 6.0f, p.getWidth() - 16.0f, 14.0f);

        drawText (g, "FREQ", top, unitFont, c.chartLabel);
        const auto freq = displayCutoff (cutoff);
        drawValueAndUnit (g, freq.number, freq.unit, top.withTrimmedLeft (textWidth (unitFont, "FREQ") + 6.0f), valueFont, unitFont,
                          c.text, c.chartLabel, juce::Justification::centredLeft, 3.0f);

        const auto res = displayPercent (resonance);
        const auto resW = drawValueAndUnit (g, res.number, res.unit, top, valueFont, unitFont, c.text, c.chartLabel,
                                            juce::Justification::centredRight, 3.0f);
        drawText (g, "RES", top.withTrimmedRight (resW + 6.0f), unitFont, c.chartLabel, juce::Justification::centredRight);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (! isEnabled() || ! e.mods.isLeftButtonDown())
            return;

        grabKeyboardFocus();
        dragging = true;
        cutoffAttachment.beginGesture();
        resonanceAttachment.beginGesture();

        // A click puts the cutoff where you click.
        if (! e.mods.isShiftDown())
            setCutoffHz (hzForX (e.position.x), true);

        lastPosition = e.position;
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragging)
            return;

        const auto p = plot();
        const bool fine = e.mods.isShiftDown();

        // Across: the cutoff follows the pointer (Shift: a tenth as fast). Up and down: resonance,
        // the full height for the full range.
        if (fine)
            setCutoffValue (cutoff + (e.position.x - lastPosition.x) / p.getWidth() * std::log2 (maxHz / minHz) / 8.0f * 0.1f, true);
        else
            setCutoffHz (hzForX (e.position.x), true);

        setResonance (resonance + (lastPosition.y - e.position.y) / p.getHeight() * (fine ? 0.1f : 1.0f), true);
        lastPosition = e.position;
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! dragging)
            return;

        dragging = false;
        cutoffAttachment.endGesture();
        resonanceAttachment.endGesture();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (! isEnabled())
            return;

        setCutoffValue (cutoffParam.convertFrom0to1 (cutoffParam.getDefaultValue()), false);
        setResonance (resonanceParam.convertFrom0to1 (resonanceParam.getDefaultValue()), false);
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
    {
        if (! isEnabled())
            return Component::mouseWheelMove (e, wheel);

        wheelAccumulator += wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 256.0f;

        if (std::abs (wheelAccumulator) >= 40.0f)
        {
            setResonance (resonance + (wheelAccumulator > 0.0f ? 0.01f : -0.01f) * (e.mods.isShiftDown() ? 10.0f : 1.0f), false);
            wheelAccumulator = 0.0f;
        }
    }

    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (! isEnabled())
            return false;

        const auto code = key.getKeyCode();
        const bool fine = key.getModifiers().isShiftDown();
        constexpr float semitone = 1.0f / 96.0f;   // the cutoff spans eight octaves

        if (code == juce::KeyPress::rightKey)       setCutoffValue (cutoff + semitone * (fine ? 0.1f : 1.0f), false);
        else if (code == juce::KeyPress::leftKey)   setCutoffValue (cutoff - semitone * (fine ? 0.1f : 1.0f), false);
        else if (code == juce::KeyPress::upKey)     setResonance (resonance + (fine ? 0.1f : 0.01f), false);
        else if (code == juce::KeyPress::downKey)   setResonance (resonance - (fine ? 0.1f : 0.01f), false);
        else return false;

        return true;
    }

    void focusGained (FocusChangeType cause) override   { updateFocus (*this, true, cause); }
    void focusLost (FocusChangeType cause) override     { updateFocus (*this, false, cause); }
    void enablementChanged() override                   { setAlpha (isEnabled() ? 1.0f : 0.45f); }

    juce::Rectangle<float> getFocusRingArea() const override { return getLocalBounds().toFloat(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override
    {
        class ValueInterface : public juce::AccessibilityTextValueInterface
        {
        public:
            explicit ValueInterface (FilterGraph& f) : graph (f) {}
            bool isReadOnly() const override                    { return true; }
            void setValueAsString (const juce::String&) override {}

            juce::String getCurrentValueAsString() const override
            {
                const juce::StringArray modes { "Low-pass", "Band-pass", "High-pass", "Notch" };
                return modes[graph.mode] + ", " + displayCutoff (graph.cutoff).toString() + ", resonance "
                       + displayPercent (graph.resonance).toString();
            }

        private:
            FilterGraph& graph;
        };

        return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group, juce::AccessibilityActions {},
                                                             juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
    }

private:
    float dbAt (float cutoffHz, float hz) const
    {
        const auto shaped = 1.0f - (1.0f - resonance) * (1.0f - resonance);
        const auto mag = dsp::filterMagnitude (mode, cutoffHz, map::resonanceK (shaped), map::svfQ (resonance), hz);
        return 20.0f * std::log10 (juce::jmax (mag, 1.0e-6f));
    }

    juce::Path responsePath (float cutoffHz) const
    {
        const auto p = plot();
        juce::Path path;
        bool first = true;   // Path::isEmpty() stays true after a lone startNewSubPath

        for (auto x = p.getX(); x <= p.getRight() + 1.0f; x += 2.0f)
        {
            const auto y = juce::jlimit (p.getY() - 4.0f, p.getBottom() + 4.0f, yForDb (dbAt (cutoffHz, hzForX (x))));

            if (first)
                path.startNewSubPath (x, y);
            else
                path.lineTo (x, y);

            first = false;
        }

        return path;
    }

    juce::Point<float> handlePosition() const
    {
        const auto p  = plot();
        const auto hz = getCutoffHz();
        return { xForHz (hz), juce::jlimit (p.getY() + 7.0f, p.getBottom() - 7.0f, yForDb (dbAt (hz, hz))) };
    }

    void setCutoffHz (float hz, bool asGesture)
    {
        setCutoffValue (std::log2 (juce::jmax (1.0f, hz) / 40.0f) / 8.0f, asGesture);
    }

    void setCutoffValue (float v, bool asGesture)
    {
        v = juce::jlimit (0.0f, 1.0f, v);

        if (juce::approximatelyEqual (v, cutoff))
            return;

        cutoff = v;
        asGesture ? cutoffAttachment.setValueAsPartOfGesture (v) : cutoffAttachment.setValueAsCompleteGesture (v);
        repaint();
    }

    void setResonance (float v, bool asGesture)
    {
        v = juce::jlimit (0.0f, 1.0f, v);

        if (juce::approximatelyEqual (v, resonance))
            return;

        resonance = v;
        asGesture ? resonanceAttachment.setValueAsPartOfGesture (v) : resonanceAttachment.setValueAsCompleteGesture (v);
        repaint();
    }

    juce::RangedAudioParameter& cutoffParam;
    juce::RangedAudioParameter& resonanceParam;
    juce::RangedAudioParameter& modeParam;
    juce::ParameterAttachment cutoffAttachment, resonanceAttachment, modeAttachment;

    float cutoff = 0.4f, resonance = 0.7f, liveHz = 0.0f, liveLevel = 0.0f, wheelAccumulator = 0.0f;
    int mode = 0;
    bool dragging = false;
    juce::Point<float> lastPosition;
};

//==============================================================================
/**
    One voice. The three are laid out alike and stacked, the way Vital stacks its oscillators,
    so they read as one column. The head: the power switch, then the oscillator, unison and
    filter-mode choices on the right. The body, in signal order: Detune and Width, the filter
    graph, the envelope (Env mod, Decay, Release beside it, Accent), then the voice's own
    Distortion, Tone and Volume.

    A voice that is off greys out, and a click anywhere on it switches it on.
*/
class VoicePanel : public juce::Component
{
public:
    VoicePanel (AsamProcessor& p, int voiceIndex)
        : proc (p), voice (voiceIndex), graph (p, voiceIndex)
    {
        auto param = [this] (const char* base) -> juce::RangedAudioParameter& { return parameter (proc, id (base)); };

        detune  = std::make_unique<Knob> (param (pid::detune),  "Detune",     Knob::Size::md);
        width   = std::make_unique<Knob> (param (pid::width),   "Width",      Knob::Size::md);
        envMod  = std::make_unique<Knob> (param (pid::envMod),  "Env mod",    Knob::Size::md);
        decay   = std::make_unique<Knob> (param (pid::decay),   "Decay",      Knob::Size::md);
        release = std::make_unique<Knob> (param (pid::release), "Release",    Knob::Size::md);
        accent  = std::make_unique<Knob> (param (pid::accent),  "Accent",     Knob::Size::md);
        drive   = std::make_unique<Knob> (param (pid::drive),   "Distortion", Knob::Size::md);
        tone    = std::make_unique<Knob> (param (pid::tone),    "Tone",       Knob::Size::md);
        level   = std::make_unique<Knob> (param (pid::level),   "Volume",     Knob::Size::md);

        detune->setFormatter  ([] (float v) { return toValueText (displayDetune (v)); });
        width->setFormatter   ([] (float v) { return toValueText (displayPercent (v)); });
        envMod->setFormatter  ([] (float v) { return toValueText (displayPercent (v)); });
        decay->setFormatter   ([] (float v) { return toValueText (displayDecay (v)); });
        release->setFormatter ([] (float v) { return toValueText (displayRelease (v)); });
        accent->setFormatter  ([] (float v) { return toValueText (displayPercent (v)); });
        drive->setFormatter   ([] (float v) { return toValueText (displayPercent (v)); });
        tone->setFormatter    ([] (float v) { return toValueText (displayTone (v)); });
        level->setFormatter   ([] (float v) { return toValueText (displayVolume (v)); });
        level->setStep (0.5f);

        for (auto* k : knobs())
            addAndMakeVisible (k);

        addAndMakeVisible (graph);

        waveformAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::waveform), [this] (float v) { waveform.setSelected (juce::roundToInt (v), false); });
        waveform.onChange = [this] (int i) { waveformAttachment->setValueAsCompleteGesture ((float) i); };

        unisonAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::unison), [this] (float v) { unison.setSelected (juce::roundToInt (v), false); updateEnablement(); });
        unison.onChange = [this] (int i) { unisonAttachment->setValueAsCompleteGesture ((float) i); updateEnablement(); };

        filterAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::filterMode), [this] (float v) { filterMode.setSelected (juce::roundToInt (v), false); });
        filterMode.onChange = [this] (int i) { filterAttachment->setValueAsCompleteGesture ((float) i); };

        // Take the state from the value handed to the callback. JUCE tells a parameter's listeners
        // newest first, so this runs before the value tree's own listener: reading the raw value
        // here would still give the old state, and the panel would grey out as it was switched on.
        powerAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::enabled), [this] (float v)
            {
                on = v > 0.5f;
                power.setToggleState (on, juce::dontSendNotification);
                updateEnablement();
            });
        power.onClick = [this] { powerAttachment->setValueAsCompleteGesture (power.getToggleState() ? 1.0f : 0.0f); };

        for (auto* c : { static_cast<juce::Component*> (&power), static_cast<juce::Component*> (&waveform),
                         static_cast<juce::Component*> (&unison), static_cast<juce::Component*> (&filterMode) })
        {
            addAndMakeVisible (c);
            c->setMouseClickGrabsKeyboardFocus (false);   // Space stays Play after a click
        }

        const auto name = "Voice " + juce::String (voice + 1);
        setTitle (name);
        power.setTitle (name);
        power.setTooltip ("Switch " + name.toLowerCase() + " on or off");
        filterMode.setTitle (name + " filter: low-pass, band-pass, high-pass or notch");
        unison.setTitle (name + " unison");
        waveform.setTitle (name + " waveform");

        waveformAttachment->sendInitialUpdate();
        unisonAttachment->sendInitialUpdate();
        filterAttachment->sendInitialUpdate();
        powerAttachment->sendInitialUpdate();
    }

    std::vector<Knob*> knobs() const
    {
        return { detune.get(), width.get(), envMod.get(), decay.get(), release.get(), accent.get(),
                 drive.get(), tone.get(), level.get() };
    }

    bool isOn() const noexcept   { return on; }

    void setLive (float hz, float amplitude)    { graph.setLive (hz, amplitude); }

    void paint (juce::Graphics& g) override
    {
        const auto& c = colours();
        const auto r  = getLocalBounds().toFloat();

        g.setColour (c.bgSurface);
        g.fillRect (r);
        drawBorder (g, r, (! on && hovering) ? c.borderStrong : c.border);   // off, it offers itself

        // Head: the panel-head rule and line (padding 14 16 12), with the switch leading.
        g.setColour (c.border);
        g.fillRect (r.getX(), (float) panelHead - 1.0f, r.getWidth(), 1.0f);

        const auto line  = juce::Rectangle<float> ((float) power.getRight() + 12.0f, 14.0f, 160.0f, 15.0f);
        const auto index = twoDigits (voice + 1);
        drawText (g, index, line, type::index(), c.textTertiary);
        drawText (g, "VOICE", line.withTrimmedLeft (textWidth (type::index(), index) + 10.0f), type::eyebrow(),
                  on ? c.textSecondary : c.textTertiary);

        auto label = [&] (const juce::String& text, const juce::Component& control)
        {
            const auto w = textWidth (type::label(), text);
            drawText (g, text, { (float) control.getX() - 8.0f - w, (float) control.getY(), w, (float) control.getHeight() },
                      type::label(), on ? c.textTertiary : c.textDisabled, juce::Justification::centredRight);
        };

        label ("UNISON", unison);
        label ("FILTER", filterMode);

        // Hairlines between the oscillator, the filter, the envelope and the output stage
        g.setColour (c.borderSubtle);

        for (auto x : dividerX)
            g.fillRect ((float) x, (float) panelHead + 16.0f, 1.0f, r.getHeight() - (float) panelHead - 32.0f);
    }

    void resized() override
    {
        constexpr int headCentre = 14 + 7;   // the 15 px head line starts 14 px down
        power.setBounds (16, headCentre - 9, 26, Switch::heightFor (Switch::Size::sm));

        // Head controls, right-aligned, read in signal order: waveform, unison, filter mode.
        auto right = getWidth() - 16;

        auto placeHead = [&] (Segmented& seg, const char* labelText)
        {
            const auto w = seg.getIdealWidth();
            seg.setBounds (right - w, headCentre - control::sm / 2, w, control::sm);
            right = seg.getX() - 16 - (labelText != nullptr ? 8 + juce::roundToInt (std::ceil (textWidth (type::label(), labelText))) : 0);
        };

        placeHead (filterMode, "FILTER");
        placeHead (unison, "UNISON");
        placeHead (waveform, nullptr);

        // Body, in signal order. The graph takes what the knobs leave.
        constexpr int knobW = 80, driveW = 92, groupGap = 16;
        const auto knobH   = Knob::preferredHeight (Knob::Size::md);
        const auto knobTop = panelHead + (getHeight() - panelHead - knobH) / 2;
        auto x = 16;

        auto place = [&] (Knob& k, int w)
        {
            k.setBounds (x, knobTop, w, knobH);
            x += w;
        };

        auto gap = [&] (int dividerIndex)
        {
            dividerX[(size_t) dividerIndex] = x + groupGap / 2;
            x += groupGap;
        };

        place (*detune, knobW);
        place (*width,  knobW);
        gap (0);

        const auto graphW = getWidth() - 16 - x - (groupGap + 4 * knobW + groupGap + driveW + 2 * knobW);
        graph.setBounds (x, panelHead + 16, graphW, getHeight() - panelHead - 32);
        x += graphW;
        gap (1);

        place (*envMod,  knobW);
        place (*decay,   knobW);
        place (*release, knobW);
        place (*accent,  knobW);
        gap (2);

        place (*drive, driveW);
        place (*tone,  knobW);
        place (*level, knobW);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! on)
            powerAttachment->setValueAsCompleteGesture (1.0f);
    }

    void mouseEnter (const juce::MouseEvent&) override  { hovering = true;  repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { hovering = false; repaint(); }

private:
    juce::String id (const char* base) const   { return pid::forVoice (voice, base); }

    void updateEnablement()
    {
        for (auto* k : knobs())
            if (k != nullptr)
                k->setEnabled (on);

        for (auto* s : { &waveform, &unison, &filterMode })
            s->setEnabled (on);

        graph.setEnabled (on);

        // Detune and Width only mean something with more than one oscillator.
        const bool spread = on && unison.getSelected() > 0;

        if (detune != nullptr) detune->setEnabled (spread);
        if (width != nullptr)  width->setEnabled (spread);

        // Off, the whole panel is one target that switches it on.
        setInterceptsMouseClicks (true, on);
        setMouseCursor (on ? juce::MouseCursor::NormalCursor : juce::MouseCursor::PointingHandCursor);
        repaint();
    }

    AsamProcessor& proc;
    const int voice;

    Switch power { {}, Switch::Size::sm };
    Segmented waveform   { { "Saw", "Square" }, Segmented::Variant::inset, Segmented::Size::sm };
    Segmented unison     { { "1", "3", "5", "7" }, Segmented::Variant::inset, Segmented::Size::sm };
    Segmented filterMode { { "LP", "BP", "HP", "Notch" }, Segmented::Variant::inset, Segmented::Size::sm };
    FilterGraph graph;
    std::unique_ptr<Knob> detune, width, envMod, decay, release, accent, drive, tone, level;
    std::unique_ptr<juce::ParameterAttachment> waveformAttachment, unisonAttachment, filterAttachment, powerAttachment;
    std::array<int, 3> dividerX {};
    bool on = false, hovering = false;
};

//==============================================================================
/** The sound preset's name in the header, and its chevron, as one ghost button: click it (or
    Space on it) for the presets. The name keeps the header title's type and stays aligned with
    the eyebrow above; the hover ground reaches past it on both sides. */
class PresetTitle : public juce::Button,
                    public rapi::FocusVisible
{
public:
    PresetTitle()
        : juce::Button ("Sound presets")
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTitle ("Sound presets");
    }

    static constexpr int padLeft = 8, padRight = 8, chevronGap = 6, chevronSize = 16;

    /** Sets the name, cut to maxTextWidth with an ellipsis if it is longer. */
    void setPresetName (const juce::String& newName, int maxTextWidth)
    {
        name = newName;
        textWidthLimit = maxTextWidth;
        setDescription ("Sound preset " + name);
        repaint();
    }

    int textWidthShown() const
    {
        return juce::jmin (textWidthLimit, juce::roundToInt (std::ceil (textWidth (type::headerTitle(), name))));
    }

    int getIdealWidth() const   { return padLeft + textWidthShown() + chevronGap + chevronSize + padRight; }

    void paintButton (juce::Graphics& g, bool isMouseOver, bool isDown) override
    {
        const auto& c = colours();
        const auto b  = getLocalBounds().toFloat();

        if (isDown || isMouseOver)
        {
            g.setColour (isDown ? c.bgPressed : c.bgHover);
            g.fillRect (b);
        }

        const auto textW = (float) textWidthShown();
        drawText (g, name, { (float) padLeft, 0.0f, textW, b.getHeight() }, type::headerTitle(), c.text);
        icons::draw (g, icons::chevronDown(),
                     { (float) padLeft + textW + (float) chevronGap, (b.getHeight() - (float) chevronSize) * 0.5f,
                       (float) chevronSize, (float) chevronSize },
                     isMouseOver ? c.text : c.textSecondary);
    }

    void focusGained (FocusChangeType cause) override   { updateFocus (*this, true, cause); }
    void focusLost (FocusChangeType cause) override     { updateFocus (*this, false, cause); }
    juce::Rectangle<float> getFocusRingArea() const override { return getLocalBounds().toFloat(); }

private:
    juce::String name;
    int textWidthLimit = 400;
};

//==============================================================================
class AsamEditor::MainView : public juce::Component
{
public:
    explicit MainView (AsamProcessor& p)
        : proc (p), masterSlider (parameter (p, pid::volume), "Master")
    {
        auto param = [this] (const char* id) -> juce::RangedAudioParameter& { return parameter (proc, id); };

        // Header: the page tabs; transport, the master volume, settings and theme on the right
        addAndMakeVisible (tabs);
        tabs.onChange = [this] (int i) { setPage (i); };

        themeButton.onClick  = [this] { toggleTheme(); };
        audioButton.onClick  = [this] { if (proc.showAudioSettings != nullptr) proc.showAudioSettings(); };
        addAndMakeVisible (themeButton);
        addChildComponent (audioButton);
        audioButton.setVisible (proc.showAudioSettings != nullptr);

        playButton.setIcon (&icons::play());
        playButton.onClick = [this] { proc.setPlaying (! proc.isPlayArmed()); refresh(); };
        stopButton.onClick = [this] { proc.stopAndRewind(); refresh(); };
        prevButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };

        for (auto* b : { static_cast<juce::Component*> (&playButton), static_cast<juce::Component*> (&stopButton),
                         static_cast<juce::Component*> (&prevButton), static_cast<juce::Component*> (&nextButton) })
            addAndMakeVisible (b);

        masterSlider.setFormatter ([] (float v) { return toValueText (displayVolume (v)); });
        masterSlider.setStep (0.5f);
        addAndMakeVisible (masterSlider);

        // Sound presets, beside the preset's name on the Sound page
        presetTitle.onClick = [this] { showPresetMenu(); };
        saveButton.onClick = [this] { savePreset(); };
        saveButton.setTooltip (proc.isStandalone() ? "Save the sound (Cmd+S)" : "Save the sound");
        presetTitle.setTooltip ("Sound presets");
        addAndMakeVisible (presetTitle);
        addAndMakeVisible (saveButton);

        // Sound: the three voices
        for (int v = 0; v < numVoices; ++v)
        {
            voicePanels[(size_t) v] = std::make_unique<VoicePanel> (proc, v);
            addAndMakeVisible (*voicePanels[(size_t) v]);
        }

        // Pattern
        clearButton.setIcon (&icons::eraser());
        randomButton.setIcon (&icons::shuffle());
        clearButton.onClick  = [this] { proc.clearPattern(); };
        randomButton.onClick = [this] { proc.randomisePattern(); };
        addAndMakeVisible (clearButton);
        addAndMakeVisible (randomButton);

        addAndMakeVisible (grid);
        grid.onStepChanged = [this] (int index, Step s) { proc.setStep (index, s); };

        addAndMakeVisible (keySelect);
        keyAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::key), [this] (float v) { keySelect.setSelected (juce::roundToInt (v), false); });
        keySelect.onChange = [this] (int i) { keyAttachment->setValueAsCompleteGesture ((float) i); };
        keyAttachment->sendInitialUpdate();

        addAndMakeVisible (octave);
        octaveAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::octave), [this] (float v) { octave.setSelected (juce::roundToInt (v) + 1, false); });
        octave.onChange = [this] (int i) { octaveAttachment->setValueAsCompleteGesture ((float) (i - 1)); };
        octaveAttachment->sendInitialUpdate();

        tempo = std::make_unique<Knob> (param (pid::tempo), "Tempo", Knob::Size::lg, Tone::neutral, 29);
        swing = std::make_unique<Knob> (param (pid::swing), "Swing", Knob::Size::lg);
        tempo->setFormatter ([] (float v) { return toValueText (displayTempo (v)); });
        swing->setFormatter ([] (float v) { return toValueText (displaySwing (v)); });
        tempo->setStep (0.5f);
        swing->setStep (1.0f);
        addAndMakeVisible (*tempo);
        addAndMakeVisible (*swing);

        addChildComponent (syncSwitch);
        syncAttachment = std::make_unique<juce::ButtonParameterAttachment> (param (pid::hostSync), syncSwitch);

        addAndMakeVisible (bank);
        bank.onSelect = [this] (int i) { proc.loadPreset (i); };

        // Buttons should not keep keyboard focus after a click, so Space stays Play.
        for (auto* c : getChildren())
            if (dynamic_cast<Knob*> (c) == nullptr && dynamic_cast<rapi::Slider*> (c) == nullptr)
                c->setMouseClickGrabsKeyboardFocus (false);

        addAndMakeVisible (focusOverlay);
        setSize (W, H);
        setPage (proc.editorPage.load());
        refresh();
    }

    //==============================================================================
    int getPage() const noexcept    { return page; }

    void setPage (int newPage)
    {
        page = juce::jlimit (0, 1, newPage);
        proc.editorPage.store (page);
        tabs.setSelected (page, false);

        const bool sound = page == soundPage;

        for (auto& v : voicePanels)
            v->setVisible (sound);

        presetTitle.setVisible (sound);
        saveButton.setVisible (sound);

        for (auto* c : { static_cast<juce::Component*> (&grid), static_cast<juce::Component*> (&keySelect),
                         static_cast<juce::Component*> (&octave), static_cast<juce::Component*> (tempo.get()),
                         static_cast<juce::Component*> (swing.get()), static_cast<juce::Component*> (&bank),
                         static_cast<juce::Component*> (&clearButton), static_cast<juce::Component*> (&randomButton) })
            c->setVisible (! sound);

        syncSwitch.setVisible (! sound && ! proc.isStandalone());

        resized();
        repaint();
    }

    void stepPreset (int delta)
    {
        const auto n = (int) patternPresets().size();
        const auto current = proc.getPresetIndex();
        const auto next = current < 0 ? (delta > 0 ? 0 : n - 1) : (current + delta + n) % n;
        proc.loadPreset (next);
    }

    void toggleTheme()
    {
        const bool dark = ! isDark();
        proc.darkTheme.store (dark);
        setDark (dark);

        if (auto* lnf = dynamic_cast<rapi::LookAndFeel*> (&getLookAndFeel()))
            lnf->applyPalette();

        themeButton.setIcon (dark ? icons::sun() : icons::moon());

        if (auto* top = getTopLevelComponent())
            top->repaint();

        repaint();
    }

    //==============================================================================
    void resized() override
    {
        stripArea  = { 0, 0, W, stripH };
        headerArea = { 0, stripH, W, headerH };

        const auto contentTop = stripH + headerH + pagePad;
        const auto content = juce::Rectangle<int> (pagePad, contentTop, W - 2 * pagePad, H - pagePad - contentTop);

        // Header: the tabs in the middle with their bar on the header's rule. On the right, from
        // the edge in: theme, settings, the master volume with its meter, then the transport.
        {
            const auto tabsW = tabs.getIdealWidth();
            tabs.setBounds ((W - tabsW) / 2, headerArea.getBottom() - Tabs::height, tabsW, Tabs::height);

            const auto y = headerArea.getCentreY() - control::md / 2;
            auto right = W - pagePad;

            themeButton.setBounds (right - control::md, y, control::md, control::md);
            right = themeButton.getX() - 8;

            if (audioButton.isVisible())
            {
                audioButton.setBounds (right - control::md, y, control::md, control::md);
                right = audioButton.getX() - 8;
            }

            right -= 16;
            masterSlider.setBounds (right - 168, headerArea.getY() + 4, 168, rapi::Slider::preferredHeight);
            masterMeter = { (float) masterSlider.getX(), (float) masterSlider.getBottom() + 3.0f, 168.0f, 4.0f };
            right = masterSlider.getX() - 24;

            for (auto* b : { &nextButton, &prevButton, &stopButton })
            {
                b->setBounds (right - control::md, y, control::md, control::md);
                right = b->getX() - 8;
            }

            playButton.setBounds (right - playWidth(), y, playWidth(), control::md);
        }

        layoutPresetControls();

        // Sound: three voices stacked across the full width, 16 px apart
        {
            const auto voiceH = (content.getHeight() - 16 * (numVoices - 1)) / numVoices;

            for (int v = 0; v < numVoices; ++v)
                voicePanels[(size_t) v]->setBounds (content.getX(), content.getY() + v * (voiceH + 16), content.getWidth(), voiceH);
        }

        // Pattern: the grid's panel, and the console with the clock, the bank and the keys
        {
            auto left = content;
            consoleArea = left.removeFromRight (consoleW);
            left.removeFromRight (16);
            patternPanel = left;

            topCell  = consoleArea.withHeight (topCellH);
            listCell = consoleArea.withTrimmedTop (topCellH + 1).withHeight (listCellH);
            keysCell = consoleArea.withTrimmedTop (topCellH + 1 + listCellH + 1);

            // Pattern head: Clear and Random, then Key and Octave at the right
            const auto headLineCentre = patternPanel.getY() + 14 + 7;
            const auto headY = headLineCentre - control::sm / 2;
            const auto octW = octave.getIdealWidth();
            auto right = patternPanel.getRight() - 16;

            octave.setBounds (right - octW, headY, octW, control::sm);
            right = octave.getX() - 8 - juce::roundToInt (textWidth (type::label(), "OCTAVE")) - 20;
            keySelect.setBounds (right - 72, headY, 72, control::sm);
            right = keySelect.getX() - 8 - juce::roundToInt (textWidth (type::label(), "KEY")) - 24;
            randomButton.setBounds (right - randomButton.getIdealWidth(), headY, randomButton.getIdealWidth(), control::sm);
            right = randomButton.getX() - 8;
            clearButton.setBounds (right - clearButton.getIdealWidth(), headY, clearButton.getIdealWidth(), control::sm);

            grid.setBounds (patternPanel.withTrimmedTop (panelHead));

            const auto inner = topCell.reduced (16);
            syncSwitch.setBounds (inner.getRight() - syncSwitch.getIdealWidth(), inner.getY() + 7 - 9,
                                  syncSwitch.getIdealWidth(), Switch::heightFor (Switch::Size::sm));

            const auto knobsTop = inner.getY() + 15 + 12 + 64 + 12 + 16 + 12;
            const auto free = inner.getWidth() - 2 * 104;
            tempo->setBounds (inner.getX() + free / 4, knobsTop, 104, Knob::preferredHeight (Knob::Size::lg));
            swing->setBounds (inner.getRight() - free / 4 - 104, knobsTop, 104, Knob::preferredHeight (Knob::Size::lg));

            const auto bankInner = listCell.reduced (16);
            bank.setBounds (bankInner.getX(), bankInner.getY() + 15 + 12, bankInner.getWidth(), 8 * BankList::rowH);
        }

        focusOverlay.setBounds (getLocalBounds());
    }

    int playWidth() const
    {
        // Wide enough for either word, so the transport does not shift as it toggles.
        Button probe { "Pause", Button::Variant::solid, Tone::brand, Button::Size::md };
        probe.setIcon (&icons::pause());
        return probe.getIdealWidth();
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto& c = colours();
        g.fillAll (c.bgCanvas);

        paintStrip (g);
        paintHeader (g);

        if (page == patternPage)
        {
            paintPanel (g, patternPanel, "01", "Pattern");

            // Pattern head controls carry visible labels
            const auto keyLabelW = textWidth (type::label(), "KEY");
            drawText (g, "KEY", { (float) keySelect.getX() - 8.0f - keyLabelW, (float) keySelect.getY(), keyLabelW, (float) control::sm },
                      type::label(), c.textTertiary, juce::Justification::centredRight);
            const auto octLabelW = textWidth (type::label(), "OCTAVE");
            drawText (g, "OCTAVE", { (float) octave.getX() - 8.0f - octLabelW, (float) octave.getY(), octLabelW, (float) control::sm },
                      type::label(), c.textTertiary, juce::Justification::centredRight);

            paintConsole (g);
        }
    }

    void paintStrip (juce::Graphics& g)
    {
        const auto& c = colours();
        const auto r  = stripArea.toFloat();
        g.setColour (c.bgSurface);
        g.fillRect (r);
        g.setColour (c.border);
        g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);

        const auto f = type::strip();
        const auto cy = r.getCentreY();
        auto x = r.getX() + 16.0f;

        x += drawAsamMark (g, x, cy, 10.0f, c.text) + 12.0f;

        const juce::String product ("Asam AS-16 acid line");
        drawText (g, product.toUpperCase(), { x, r.getY(), 400.0f, r.getHeight() }, f, c.text);
        x += textWidth (f, product.toUpperCase()) + 12.0f;

        const auto context = "Pattern " + presetLabel() + " " + juce::String::charToString (0x00b7) + " " + patternName;
        drawText (g, context.toUpperCase(), { x, r.getY(), 360.0f, r.getHeight() }, f, c.textTertiary);

        // Right: clock context, then the status lamp with its word.
        const bool waiting = proc.isPlayArmed() && ! transport.running;
        juce::String status;
        juce::Colour lamp;

        if (transport.running)  { status = "Playing"; lamp = c.signal.withMultipliedAlpha (blinkOn ? 1.0f : 0.25f); }
        else if (waiting)       { status = transport.hostClock ? "Armed" : "Starting"; lamp = c.statusWarning; }
        else                    { status = "Stopped"; lamp = c.statusOff; }

        const auto statusFont = type::strip();
        const auto statusW = 6.0f + 8.0f + textWidth (statusFont, status.toUpperCase());
        const auto statusX = r.getRight() - 16.0f - statusW;
        drawLamp (g, { statusX + 3.0f, cy }, 6.0f, lamp);
        drawText (g, status.toUpperCase(), { statusX + 14.0f, r.getY(), statusW, r.getHeight() }, statusFont, c.text);

        const auto dot = " " + juce::String::charToString (0x00b7) + " ";
        juce::String info = (transport.hostClock ? "Host clock" : "Internal clock")
                          + dot + juce::String (transport.bpm, 1) + " BPM"
                          + dot + juce::String (juce::roundToInt (proc.getSampleRate() / 100.0) / 10.0, 1) + " kHz";

        if (transport.running)
            info += dot + "Bar " + twoDigits (transport.bar) + dot + "Step " + twoDigits (transport.step + 1) + " / 16";

        if (transport.transpose != 0)
            info += dot + "Transpose " + (transport.transpose > 0 ? "+" : "") + withMinus (juce::String (transport.transpose));

        drawText (g, info.toUpperCase(), { r.getX(), r.getY(), statusX - 12.0f - r.getX(), r.getHeight() }, f, c.textTertiary,
                  juce::Justification::centredRight);
    }

    void paintHeader (juce::Graphics& g)
    {
        const auto& c = colours();
        const auto r  = headerArea.toFloat();
        g.setColour (c.bgSurface);
        g.fillRect (r);
        g.setColour (c.border);
        g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);

        const auto dot = " " + juce::String::charToString (0x00b7) + " ";
        const auto left = r.getX() + 16.0f;
        const auto room = (float) tabs.getX() - 24.0f - left;
        const auto titleFont = type::headerTitle();

        if (page == soundPage)
        {
            // The sound preset: the name is a button (PresetTitle); Edited follows it on its line
            drawText (g, soundEyebrow(), { left, r.getY() + 9.0f, room, 15.0f }, type::eyebrow(), c.textTertiary);

            if (presetEdited)
                drawBadge (g, (float) presetBadgeX, r.getY() + 37.0f, "Edited", c.neutral);
        }
        else
        {
            const auto eyebrow = "Pattern " + presetLabel() + dot + juce::String (countNotes (pattern)) + " notes"
                               + dot + juce::String (countAccents (pattern)) + " accents"
                               + dot + juce::String (countSlides (pattern)) + " slides";

            // Eyebrow over the title, 2 px apart, kept clear of the tabs
            drawText (g, eyebrow.toUpperCase(), { left, r.getY() + 9.0f, room, 15.0f }, type::eyebrow(), c.textTertiary);
            drawText (g, patternName, { left, r.getY() + 26.0f, room, 22.0f }, titleFont, c.text);

            if (edited)
                drawBadge (g, left + textWidth (titleFont, patternName) + 10.0f, r.getY() + 37.0f, "Edited", c.neutral);
        }

        // The output level under the master volume
        drawLevelMeter (g, masterMeter, meterDb);
    }

    void paintPanel (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& index, const juce::String& title)
    {
        const auto& c = colours();
        const auto r  = area.toFloat();
        g.setColour (c.bgSurface);
        g.fillRect (r);
        drawBorder (g, r, c.border);
        drawPanelHead (g, r.withHeight ((float) panelHead), index, title);
    }

    void paintConsole (juce::Graphics& g)
    {
        const auto& c = colours();
        const auto r  = consoleArea.toFloat();

        g.setColour (c.bgSurface);
        g.fillRect (r);
        drawBorder (g, r, c.border);

        g.setColour (c.border);
        g.fillRect (r.getX(), (float) topCell.getBottom(), r.getWidth(), 1.0f);
        g.fillRect (r.getX(), (float) listCell.getBottom(), r.getWidth(), 1.0f);

        // Instrument panel: four corner ticks
        drawRegistrationMarks (g, r);

        paintClock (g);
        drawKicker (g, listCell, "03", "Pattern bank");
        drawKicker (g, keysCell, "04", "Keys");
        paintKeys (g);
    }

    /** RReadout, pixel font, large: the one instrument readout on the page. */
    void paintClock (juce::Graphics& g)
    {
        const auto& c = colours();
        drawKicker (g, topCell, "02", "Clock");

        const auto inner = topCell.reduced (16).toFloat();
        const auto headY = inner.getY() + 15.0f + 12.0f;

        drawText (g, "TEMPO", { inner.getX(), headY, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::medium, 11.0f, 0.08f), c.textSecondary);
        drawText (g, transport.hostClock ? "HOST" : "INTERNAL", { inner.getX(), headY, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::regular, 11.0f, 0.04f), c.textTertiary, juce::Justification::centredRight);

        constexpr float em  = 40.0f;
        const auto figureY  = headY + 16.0f + 8.0f;
        const auto pixel    = font (Family::pixel, Weight::regular, em);
        const juce::String ghost ("888.8");
        const auto ghostW   = pixelFigureWidth (ghost, pixel, em);
        const auto baseline = figureY + 20.0f + (pixel.getAscent() - pixel.getDescent()) * 0.5f;

        // The ghost digits sit behind the value at 9 %, both right-aligned in the same cells.
        drawPixelFigure (g, ghost, inner.getX() + ghostW, baseline, pixel, em, c.text.withAlpha (0.09f));
        drawPixelFigure (g, juce::String (transport.bpm, 1), inner.getX() + ghostW, baseline, pixel, em, c.text);

        const auto unitFont = font (Family::mono, Weight::medium, 12.0f, 0.08f);
        juce::GlyphArrangement unitGlyphs;
        unitGlyphs.addLineOfText (unitFont, "BPM", inner.getX() + ghostW + 6.0f, baseline);
        g.setColour (c.textTertiary);
        unitGlyphs.draw (g);

        const auto stepMs = 60000.0f / juce::jmax (1.0f, transport.bpm) / 4.0f;
        const auto swingValue = juce::roundToInt (rawValue (proc, pid::swing));
        const auto sub = juce::String (juce::roundToInt (stepMs)) + " ms a step " + juce::String::charToString (0x00b7)
                       + " swing " + juce::String (swingValue) + "%";
        drawText (g, sub.toUpperCase(), { inner.getX(), figureY + 40.0f + 12.0f, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::regular, 11.0f, 0.04f, true), c.textTertiary);
    }

    void paintKeys (juce::Graphics& g)
    {
        const auto& c = colours();
        const auto inner = keysCell.reduced (16).toFloat().withTrimmedTop (15.0f + 12.0f);
        const auto captionFont = font (Family::mono, Weight::medium, 10.0f, 0.08f);
        const auto dot = " " + juce::String::charToString (0x00b7) + " ";

        if (! proc.isStandalone())
        {
            drawText (g, "MIDI NOTES PLAY THE VOICES WHILE STOPPED", { inner.getX(), inner.getY(), inner.getWidth(), 20.0f },
                      captionFont, c.textTertiary);
            drawText (g, "AND TRANSPOSE THE PATTERN WHILE IT RUNS", { inner.getX(), inner.getY() + 28.0f, inner.getWidth(), 20.0f },
                      captionFont, c.textTertiary);
            return;
        }

        // Two rows of key caps with their captions
        auto row = [&] (float y, std::initializer_list<std::pair<juce::StringArray, juce::String>> items)
        {
            auto x = inner.getX();

            for (auto& [keys, caption] : items)
            {
                for (int i = 0; i < keys.size(); ++i)
                {
                    if (keys[i] == "-")
                    {
                        drawText (g, juce::String::charToString (0x2013), { x, y, 8.0f, 20.0f }, captionFont, c.textTertiary,
                                  juce::Justification::centred);
                        x += 10.0f;
                        continue;
                    }

                    x += drawKbd (g, { x, y, keys[i].length() > 1 ? 60.0f : 30.0f, 20.0f }, keys[i]) + 2.0f;
                }

                x += 4.0f;
                const auto w = textWidth (captionFont, caption);
                drawText (g, caption, { x, y, w + 2.0f, 20.0f }, captionFont, c.textTertiary);
                x += w + 14.0f;
            }
        };

        const auto left  = juce::String::charToString (0x2190), rightArrow = juce::String::charToString (0x2192);
        row (inner.getY(),         { { { "Space" }, "PLAY" }, { { left, rightArrow }, "PATTERN" } });
        row (inner.getY() + 28.0f, { { { "A", "-", "K" }, "NOTES" }, { { "Z", "X" }, "OCTAVE" } });
        drawText (g, ("Shift with a note" + dot + "accent").toUpperCase(), { inner.getX(), inner.getY() + 58.0f, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::regular, 10.0f, 0.04f), c.textTertiary);
    }

    //==============================================================================
    // Sound presets

    /** The eyebrow over the preset's name on the Sound page. */
    juce::String soundEyebrow() const
    {
        const auto dot = " " + juce::String::charToString (0x00b7) + " ";
        return ("Sound preset" + dot + juce::String (voicesOn) + " of " + juce::String (numVoices) + " voices on").toUpperCase();
    }

    void layoutPresetControls()
    {
        //  SOUND PRESET . 2 OF 3 VOICES ON
        //  Name v  EDITED                       [ Save ]
        //
        // Two lines like any header title, Edited on the name's line as the pattern has it, and
        // Save as the header's action: centred on the header, 24 px clear of the title block.
        const auto saveW  = saveButton.getIdealWidth();
        const auto x      = 16 - PresetTitle::padLeft;
        const auto chrome = PresetTitle::padLeft + PresetTitle::chevronGap + PresetTitle::chevronSize + PresetTitle::padRight;
        const auto limit  = tabs.getX() - 24;   // keep clear of the tabs

        presetTitle.setPresetName (presetName, juce::jmax (40, limit - x - chrome - 4 - badgeWidth - 24 - saveW));
        presetTitle.setBounds (x, headerArea.getY() + 37 - control::sm / 2, presetTitle.getIdealWidth(), control::sm);

        presetBadgeX = presetTitle.getRight() - PresetTitle::padRight + 12;
        const auto nameLineRight = presetEdited ? presetBadgeX + badgeWidth : presetTitle.getRight() - PresetTitle::padRight;
        const auto eyebrowRight  = 16 + juce::roundToInt (std::ceil (textWidth (type::eyebrow(), soundEyebrow())));
        const auto saveX = juce::jmin (juce::jmax (nameLineRight, eyebrowRight) + 24, limit - saveW);

        saveButton.setBounds (saveX, headerArea.getCentreY() - control::md / 2, saveW, control::md);
    }

    void showPresetMenu()
    {
        juce::PopupMenu menu;
        menu.setLookAndFeel (&getLookAndFeel());

        const auto files   = preset::list();
        const auto current = proc.getSoundPresetFile();
        const bool keys    = proc.isStandalone();   // in a DAW, Cmd+S and Cmd+O are the host's

        if (files.isEmpty())
            menu.addItem (juce::PopupMenu::Item ("None saved yet").setEnabled (false));

        juce::Component::SafePointer<MainView> safe (this);

        for (const auto& file : files)
            menu.addItem (juce::PopupMenu::Item (file.getFileNameWithoutExtension())
                              .setTicked (file == current)
                              .setAction ([safe, file] { if (safe != nullptr) safe->loadPreset (file); }));

        auto item = [&] (const juce::String& text, const juce::String& shortcut, std::function<void()> action)
        {
            juce::PopupMenu::Item i (text);
            i.setAction ([safe, action] { if (safe != nullptr) action(); });

            if (keys)
                i.shortcutKeyDescription = shortcut;

            menu.addItem (std::move (i));
        };

        menu.addSeparator();
        item ("Save", "Cmd S", [this] { savePreset(); });
        item ("Save as" + juce::String::charToString (0x2026), "Shift Cmd S", [this] { savePresetAs(); });
        item ("Open" + juce::String::charToString (0x2026), "Cmd O", [this] { openPreset(); });
        item ("Init sound", {}, [this] { proc.initSound(); refresh(); });
        menu.addSeparator();
        item ("Show presets folder", {}, []
        {
            const auto folder = preset::folder();
            folder.createDirectory();
            folder.revealToUser();
        });

        menu.showMenuAsync (juce::PopupMenu::Options()
                                .withTargetComponent (&presetTitle)
                                .withMinimumWidth (240)
                                .withStandardItemHeight (control::sm));
    }

    void loadPreset (const juce::File& file)
    {
        report (proc.loadSoundPreset (file));
    }

    /** Save over the preset in use; a sound that has no file yet asks where. */
    void savePreset()
    {
        const auto file = proc.getSoundPresetFile();

        if (file == juce::File())
            savePresetAs();
        else
            report (proc.saveSoundPreset (file));
    }

    void savePresetAs()
    {
        const auto folder = preset::folder();
        folder.createDirectory();

        auto name = proc.getSoundPresetName();

        if (name == "Init" || name == "Untitled")
            name = "New sound";

        chooser = std::make_unique<juce::FileChooser> ("Save the sound as a preset",
                                                       folder.getChildFile (juce::File::createLegalFileName (name) + preset::extension),
                                                       juce::String ("*") + preset::extension);

        juce::Component::SafePointer<MainView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult() != juce::File())
                                      safe->report (safe->proc.saveSoundPreset (fc.getResult()));
                              });
    }

    void openPreset()
    {
        chooser = std::make_unique<juce::FileChooser> ("Open a sound preset", preset::folder(), juce::String ("*") + preset::extension);

        juce::Component::SafePointer<MainView> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe] (const juce::FileChooser& fc)
                              {
                                  if (safe != nullptr && fc.getResult().existsAsFile())
                                      safe->report (safe->proc.loadSoundPreset (fc.getResult()));
                              });
    }

    void report (const juce::Result& result)
    {
        if (result.failed())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Sound preset", result.getErrorMessage());

        refresh();
    }

    //==============================================================================
    juce::String presetLabel() const
    {
        return presetIndex >= 0 ? twoDigits (presetIndex + 1) : juce::String ("R");
    }

    void refresh()
    {
        transport = proc.getTransportView();

        const auto version = proc.getPatternVersion();
        bool headerChanged = false;

        if (version != lastPatternVersion)
        {
            lastPatternVersion = version;
            pattern     = proc.getPattern();
            patternName = proc.getPatternName();
            presetIndex = proc.getPresetIndex();
            edited      = proc.isPatternEdited();
            grid.setPattern (pattern);
            bank.setCurrent (presetIndex);
            headerChanged = true;
        }

        const auto key = juce::roundToInt (rawValue (proc, pid::key));
        const auto oct = juce::roundToInt (rawValue (proc, pid::octave));
        grid.setRoot (36 + key + 12 * oct);
        grid.setPlayingStep (transport.running ? transport.step : -1);

        const bool armed = proc.isPlayArmed();
        playButton.setButtonText (armed ? "Pause" : "Play");
        playButton.setIcon (armed ? &icons::pause() : &icons::play());

        tempo->setEnabled (! transport.hostClock);

        const auto peak = proc.takeOutputPeak();
        const auto db = juce::Decibels::gainToDecibels (peak, -60.0f);
        meterDb = db > meterDb ? db : juce::jmax (-60.0f, meterDb - 1.6f);
        blinkOn = (juce::Time::getMillisecondCounter() % 1200) < 600;

        if (page == soundPage)
            for (int v = 0; v < numVoices; ++v)
                voicePanels[(size_t) v]->setLive (proc.getLiveCutoff (v), proc.getLiveLevel (v));

        // The sound preset's name, whether it has been edited, how many voices are on
        {
            const auto name = proc.getSoundPresetName();
            const auto isEdited = proc.isSoundEdited();
            int on = 0;

            for (int v = 0; v < numVoices; ++v)
                on += rawValue (proc, pid::forVoice (v, pid::enabled)) > 0.5f ? 1 : 0;

            if (name != presetName || isEdited != presetEdited || on != voicesOn)
            {
                presetName = name;
                presetEdited = isEdited;
                voicesOn = on;
                layoutPresetControls();
                headerChanged = true;
            }
        }

        if (headerChanged)
            repaint (headerArea);

        repaint (stripArea);
        repaint (masterMeter.getSmallestIntegerContainer().expanded (2));

        if (page == patternPage)
            repaint (topCell.withTrimmedBottom (topCell.getHeight() - 150));

        focusOverlay.refresh();
    }

private:
    AsamProcessor& proc;
    int page = soundPage;

    // Header
    Tabs tabs { { "Sound", "Pattern" } };
    Button playButton { "Play", Button::Variant::solid, Tone::brand, Button::Size::md };
    IconButton stopButton { "Stop and rewind", icons::square(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::md };
    IconButton prevButton { "Previous pattern", icons::chevronLeft(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::md };
    IconButton nextButton { "Next pattern", icons::chevronRight(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::md };
    rapi::Slider masterSlider;
    PresetTitle presetTitle;
    Button saveButton { "Save", Button::Variant::outline, Tone::neutral, Button::Size::md };
    std::unique_ptr<juce::FileChooser> chooser;
    IconButton themeButton { "Switch light or dark theme", isDark() ? icons::sun() : icons::moon(), IconButton::Variant::ghost };
    IconButton audioButton { "Audio and MIDI settings", icons::settings(), IconButton::Variant::ghost };

    // Sound
    std::array<std::unique_ptr<VoicePanel>, numVoices> voicePanels;

    // Pattern
    Button clearButton  { "Clear",  Button::Variant::ghost,   Tone::neutral, Button::Size::sm };
    Button randomButton { "Random", Button::Variant::outline, Tone::neutral, Button::Size::sm };
    StepGrid grid;
    Select keySelect { keyNames(), Select::Size::sm };
    Segmented octave { octaveOptions(), Segmented::Variant::inset, Segmented::Size::sm };
    std::unique_ptr<juce::ParameterAttachment> keyAttachment, octaveAttachment;
    std::unique_ptr<Knob> tempo, swing;
    Switch syncSwitch { "Host sync", Switch::Size::sm };
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment;
    BankList bank;

    FocusRingOverlay focusOverlay;

    juce::Rectangle<int> stripArea, headerArea, patternPanel, consoleArea, topCell, listCell, keysCell;
    juce::Rectangle<float> masterMeter;

    AsamProcessor::TransportView transport;
    Pattern pattern {};
    juce::String patternName;
    int presetIndex = 0;
    bool edited = false;
    std::uint32_t lastPatternVersion = 0;
    float meterDb = -60.0f;
    bool blinkOn = true;

    juce::String presetName;
    bool presetEdited = false;
    int voicesOn = 0, presetBadgeX = 0;
    static constexpr int badgeWidth = 64;   // "EDITED" in its badge
};

//==============================================================================
AsamEditor::AsamEditor (AsamProcessor& p)
    : AudioProcessorEditor (p), asamProcessor (p)
{
    setDark (p.darkTheme.load());
    sharedLookAndFeel->lnf.applyPalette();
    setLookAndFeel (&sharedLookAndFeel->lnf);

    view = std::make_unique<MainView> (p);
    addAndMakeVisible (*view);

    setWantsKeyboardFocus (true);
    setResizable (true, false);
    setResizeLimits (W * 7 / 10, H * 7 / 10, W * 3 / 2, H * 3 / 2);

    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio ((double) W / (double) H);

    setSize (W, H);
    startTimerHz (30);
}

AsamEditor::~AsamEditor()
{
    releaseAllKeys();
    view = nullptr;
    setLookAndFeel (nullptr);
}

juce::Component* AsamEditor::getView() const noexcept   { return view.get(); }

void AsamEditor::refreshNow()
{
    view->refresh();
}

void AsamEditor::showPage (int page)
{
    view->setPage (page);
}

void AsamEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours().bgCanvas);
}

void AsamEditor::resized()
{
    // Fit the whole view into whatever shape the window or host hands over, never cropping an
    // edge. The strip stays flush with the top: spare height goes below, spare width either side.
    const auto scale = juce::jmin ((float) getWidth() / (float) W, (float) getHeight() / (float) H);
    const auto x = std::floor (((float) getWidth() - (float) W * scale) * 0.5f);
    view->setBounds (0, 0, W, H);
    view->setTransform (juce::AffineTransform::scale (scale).translated (x, 0.0f));
}

void AsamEditor::timerCallback()
{
    if (! grabbedInitialFocus && isShowing())
    {
        grabbedInitialFocus = true;

        if (asamProcessor.isStandalone())
            grabKeyboardFocus();
    }

    view->refresh();
}

//==============================================================================
bool AsamEditor::keyPressed (const juce::KeyPress& key)
{
    // Inside a DAW, Space, the arrows and the letter keys belong to the host.
    if (! asamProcessor.isStandalone())
        return false;

    // Sound presets
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0))
    {
        view->savePreset();
        return true;
    }

    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
    {
        view->savePresetAs();
        return true;
    }

    if (key == juce::KeyPress ('o', juce::ModifierKeys::commandModifier, 0))
    {
        view->openPreset();
        return true;
    }

    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::spaceKey)
    {
        asamProcessor.setPlaying (! asamProcessor.isPlayArmed());
        view->refresh();
        return true;
    }

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey)
    {
        const auto n = (int) patternPresets().size();
        const auto current = asamProcessor.getPresetIndex();
        const auto delta = code == juce::KeyPress::rightKey ? 1 : -1;
        asamProcessor.loadPreset (current < 0 ? 0 : (current + delta + n) % n);
        return true;
    }

    const auto ch = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());

    if (ch == 'z' || ch == 'x')
    {
        releaseAllKeys();
        keyboardOctave = juce::jlimit (-2, 2, keyboardOctave + (ch == 'x' ? 1 : -1));
        return true;
    }

    // Note keys are handled in keyStateChanged; claim them here so they don't beep.
    return juce::String (qwertyKeys).containsChar (ch) && ! key.getModifiers().isCommandDown();
}

bool AsamEditor::keyStateChanged (bool)
{
    if (! asamProcessor.isStandalone())
        return false;

    bool used = false;

    for (int i = 0; i < 13; ++i)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (qwertyKeys[i]) && ! juce::ModifierKeys::currentModifiers.isCommandDown();

        if (down == keyIsDown[(size_t) i])
            continue;

        keyIsDown[(size_t) i] = down;
        used = true;

        if (down)
        {
            keyNote[(size_t) i] = juce::jlimit (0, 127, 60 + 12 * keyboardOctave + i);
            const auto velocity = juce::ModifierKeys::currentModifiers.isShiftDown() ? 1.0f : 0.7f;
            asamProcessor.keyboardState.noteOn (1, keyNote[(size_t) i], velocity);
        }
        else
        {
            asamProcessor.keyboardState.noteOff (1, keyNote[(size_t) i], 0.0f);
        }
    }

    return used;
}

void AsamEditor::focusLost (FocusChangeType)
{
    releaseAllKeys();
}

void AsamEditor::releaseAllKeys()
{
    for (int i = 0; i < 13; ++i)
    {
        if (keyIsDown[(size_t) i])
        {
            asamProcessor.keyboardState.noteOff (1, keyNote[(size_t) i], 0.0f);
            keyIsDown[(size_t) i] = false;
        }
    }
}
