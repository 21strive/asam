#include "PluginEditor.h"

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
    constexpr int voiceH    = 207;

    constexpr int clockCellH = 297;
    constexpr int bankCellH  = 219;

    constexpr const char* qwertyKeys = "awsedftgyhujk";

    ValueText toValueText (const DisplayValue& d)   { return { d.number, d.unit }; }

    /** The 21 Strive logogram: a 2 and a 1 cut from one block (201.6 x 100). */
    const juce::Path& strivePath()
    {
        static const juce::Path p = []
        {
            juce::Path path;
            path.addPath (juce::Drawable::parseSVGPath ("M0 0H77.55L23.25 50H100.8L46.5 100H0Z"));
            path.addPath (juce::Drawable::parseSVGPath ("M100.8 50L155.1 0H201.6V100H155.1V50Z"));
            return path;
        }();
        return p;
    }

    void drawStriveMark (juce::Graphics& g, float x, float centreY, float height, juce::Colour colour)
    {
        const auto scale = height / 100.0f;
        g.setColour (colour);
        g.fillPath (strivePath(), juce::AffineTransform::scale (scale).translated (x, centreY - height * 0.5f));
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
class AsamEditor::MainView : public juce::Component
{
public:
    explicit MainView (AsamProcessor& p)
        : proc (p)
    {
        auto& state = proc.getState();
        auto param  = [&state] (const char* id) -> juce::RangedAudioParameter& { return *state.getParameter (id); };

        // Header actions
        clearButton.setIcon (&icons::eraser());
        randomButton.setIcon (&icons::shuffle());
        clearButton.onClick  = [this] { proc.clearPattern(); };
        randomButton.onClick = [this] { proc.randomisePattern(); };
        themeButton.onClick  = [this] { toggleTheme(); };
        audioButton.onClick  = [this] { if (proc.showAudioSettings != nullptr) proc.showAudioSettings(); };

        for (auto* b : { static_cast<juce::Component*> (&clearButton), static_cast<juce::Component*> (&randomButton),
                         static_cast<juce::Component*> (&themeButton) })
            addAndMakeVisible (b);

        addChildComponent (audioButton);
        audioButton.setVisible (proc.showAudioSettings != nullptr);

        // 01 Voice: the Filter and the Distortion are the two big knobs.
        cutoff    = std::make_unique<Knob> (param (pid::cutoff),    "Filter",     Knob::Size::lg);
        resonance = std::make_unique<Knob> (param (pid::resonance), "Resonance",  Knob::Size::md);
        envMod    = std::make_unique<Knob> (param (pid::envMod),    "Env mod",    Knob::Size::md);
        decay     = std::make_unique<Knob> (param (pid::decay),     "Decay",      Knob::Size::md);
        accent    = std::make_unique<Knob> (param (pid::accent),    "Accent",     Knob::Size::md);
        drive     = std::make_unique<Knob> (param (pid::drive),     "Distortion", Knob::Size::lg);
        tone      = std::make_unique<Knob> (param (pid::tone),      "Tone",       Knob::Size::md);
        volume    = std::make_unique<Knob> (param (pid::volume),    "Volume",     Knob::Size::md);

        cutoff->setFormatter    ([] (float v) { return toValueText (displayCutoff (v)); });
        resonance->setFormatter ([] (float v) { return toValueText (displayPercent (v)); });
        envMod->setFormatter    ([] (float v) { return toValueText (displayPercent (v)); });
        decay->setFormatter     ([] (float v) { return toValueText (displayDecay (v)); });
        accent->setFormatter    ([] (float v) { return toValueText (displayPercent (v)); });
        drive->setFormatter     ([] (float v) { return toValueText (displayPercent (v)); });
        tone->setFormatter      ([] (float v) { return toValueText (displayTone (v)); });
        volume->setFormatter    ([] (float v) { return toValueText (displayVolume (v)); });
        volume->setStep (0.5f);

        for (auto* k : voiceKnobs())
            addAndMakeVisible (k);

        addAndMakeVisible (waveform);
        waveformAttachment = std::make_unique<juce::ParameterAttachment> (
            param (pid::waveform), [this] (float v) { waveform.setSelected (juce::roundToInt (v), false); });
        waveform.onChange = [this] (int i) { waveformAttachment->setValueAsCompleteGesture ((float) i); };
        waveformAttachment->sendInitialUpdate();

        // 02 Pattern
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

        // 03 Clock
        tempo = std::make_unique<Knob> (param (pid::tempo), "Tempo", Knob::Size::lg, Tone::neutral, 29);
        swing = std::make_unique<Knob> (param (pid::swing), "Swing", Knob::Size::lg);
        tempo->setFormatter ([] (float v) { return toValueText (displayTempo (v)); });
        swing->setFormatter ([] (float v) { return toValueText (displaySwing (v)); });
        tempo->setStep (0.5f);
        swing->setStep (1.0f);
        addAndMakeVisible (*tempo);
        addAndMakeVisible (*swing);

        addChildComponent (syncSwitch);
        syncSwitch.setVisible (! proc.isStandalone());
        syncAttachment = std::make_unique<juce::ButtonParameterAttachment> (param (pid::hostSync), syncSwitch);

        // 04 Pattern bank
        addAndMakeVisible (bank);
        bank.onSelect = [this] (int i) { proc.loadPreset (i); };

        // 05 Transport
        playButton.setIcon (&icons::play());
        playButton.onClick = [this] { proc.setPlaying (! proc.isPlayArmed()); refresh(); };
        stopButton.onClick = [this] { proc.stopAndRewind(); refresh(); };
        prevButton.onClick = [this] { stepPreset (-1); };
        nextButton.onClick = [this] { stepPreset (1); };

        for (auto* b : { static_cast<juce::Component*> (&playButton), static_cast<juce::Component*> (&stopButton),
                         static_cast<juce::Component*> (&prevButton), static_cast<juce::Component*> (&nextButton) })
            addAndMakeVisible (b);

        // Buttons should not keep keyboard focus after a click, so Space stays Play.
        for (auto* c : getChildren())
            if (dynamic_cast<Knob*> (c) == nullptr)
                c->setMouseClickGrabsKeyboardFocus (false);

        addAndMakeVisible (focusOverlay);
        setSize (W, H);
        refresh();
    }

    std::vector<Knob*> voiceKnobs() const
    {
        return { cutoff.get(), resonance.get(), envMod.get(), decay.get(), accent.get(), drive.get(), tone.get(), volume.get() };
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
        auto content = juce::Rectangle<int> (pagePad, contentTop, W - 2 * pagePad, H - pagePad - contentTop);

        consoleArea = content.removeFromRight (consoleW);
        content.removeFromRight (16);

        voicePanel = content.removeFromTop (voiceH);
        content.removeFromTop (16);
        patternPanel = content;

        // Header actions, right-aligned and vertically centred
        {
            const auto y = headerArea.getCentreY() - control::md / 2;
            auto right = W - pagePad;

            randomButton.setBounds (right - randomButton.getIdealWidth(), y, randomButton.getIdealWidth(), control::md);
            right = randomButton.getX() - 8;
            clearButton.setBounds (right - clearButton.getIdealWidth(), y, clearButton.getIdealWidth(), control::md);
            right = clearButton.getX() - 16;
            themeButton.setBounds (right - control::md, y, control::md, control::md);
            right = themeButton.getX() - 8;
            audioButton.setBounds (right - control::md, y, control::md, control::md);
        }

        // 01 Voice
        {
            const auto headLineCentre = voicePanel.getY() + 14 + 7;
            const auto segW = waveform.getIdealWidth();
            waveform.setBounds (voicePanel.getRight() - 16 - segW, headLineCentre - control::sm / 2, segW, control::sm);

            auto body = voicePanel.withTrimmedTop (panelHead).reduced (16);
            constexpr int lgW = 104, mdW = 80;

            auto place = [&body] (Knob& k, int width, Knob::Size s)
            {
                k.setBounds (body.removeFromLeft (width).withHeight (Knob::preferredHeight (s)));
            };

            place (*cutoff,    lgW, Knob::Size::lg);
            place (*resonance, mdW, Knob::Size::md);
            place (*envMod,    mdW, Knob::Size::md);
            place (*decay,     mdW, Knob::Size::md);
            place (*accent,    mdW, Knob::Size::md);
            dividerX = body.getX() + 16;
            body.removeFromLeft (32);
            place (*drive,  lgW, Knob::Size::lg);
            place (*tone,   mdW, Knob::Size::md);
            place (*volume, mdW, Knob::Size::md);
        }

        // 02 Pattern
        {
            const auto headLineCentre = patternPanel.getY() + 14 + 7;
            const auto octW = octave.getIdealWidth();
            auto right = patternPanel.getRight() - 16;

            octave.setBounds (right - octW, headLineCentre - control::sm / 2, octW, control::sm);
            right = octave.getX() - 8 - juce::roundToInt (textWidth (type::label(), "OCTAVE")) - 20;
            keySelect.setBounds (right - 72, headLineCentre - control::sm / 2, 72, control::sm);

            grid.setBounds (patternPanel.withTrimmedTop (panelHead));
        }

        // Console cells
        clockCell     = consoleArea.withHeight (clockCellH);
        bankCell      = consoleArea.withTrimmedTop (clockCellH + 1).withHeight (bankCellH);
        transportCell = consoleArea.withTrimmedTop (clockCellH + 1 + bankCellH + 1);

        {
            const auto inner = clockCell.reduced (16);
            syncSwitch.setBounds (inner.getRight() - syncSwitch.getIdealWidth(), inner.getY() + 7 - 9,
                                  syncSwitch.getIdealWidth(), Switch::heightFor (Switch::Size::sm));

            const auto knobsTop = inner.getY() + 15 + 12 + 64 + 12 + 16 + 12;
            const auto free = inner.getWidth() - 2 * 104;
            tempo->setBounds (inner.getX() + free / 4, knobsTop, 104, Knob::preferredHeight (Knob::Size::lg));
            swing->setBounds (inner.getRight() - free / 4 - 104, knobsTop, 104, Knob::preferredHeight (Knob::Size::lg));
        }

        {
            const auto inner = bankCell.reduced (16);
            bank.setBounds (inner.getX(), inner.getY() + 15 + 12, inner.getWidth(), 8 * BankList::rowH);
        }

        {
            auto row = transportCell.reduced (16).withTrimmedTop (15 + 12).withHeight (control::lg);
            nextButton.setBounds (row.removeFromRight (control::lg));
            row.removeFromRight (8);
            prevButton.setBounds (row.removeFromRight (control::lg));
            row.removeFromRight (8);
            stopButton.setBounds (row.removeFromRight (control::lg));
            row.removeFromRight (8);
            playButton.setBounds (row);
        }

        focusOverlay.setBounds (getLocalBounds());
    }

    //==============================================================================
    void paint (juce::Graphics& g) override
    {
        const auto& c = colours();
        g.fillAll (c.bgCanvas);

        paintStrip (g);
        paintHeader (g);
        paintPanel (g, voicePanel, "01", "Voice");
        paintPanel (g, patternPanel, "02", "Pattern");

        // Voice: a hairline between the filter section and the drive section
        g.setColour (c.borderSubtle);
        g.fillRect ((float) dividerX, (float) (voicePanel.getY() + panelHead + 16), 1.0f, (float) (voicePanel.getHeight() - panelHead - 32));

        // Pattern head controls carry visible labels
        drawText (g, "KEY", juce::Rectangle<float> ((float) keySelect.getX() - 36.0f, (float) keySelect.getY(), 28.0f, (float) control::sm),
                  type::label(), c.textTertiary, juce::Justification::centredRight);
        const auto octLabelW = textWidth (type::label(), "OCTAVE");
        drawText (g, "OCTAVE", { (float) octave.getX() - 8.0f - octLabelW, (float) octave.getY(), octLabelW, (float) control::sm },
                  type::label(), c.textTertiary, juce::Justification::centredRight);

        paintConsole (g);
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

        drawStriveMark (g, x, cy, 10.0f, c.text);
        x += 20.16f + 12.0f;

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
        const auto eyebrow = "Pattern " + presetLabel() + dot + juce::String (countNotes (pattern)) + " notes"
                           + dot + juce::String (countAccents (pattern)) + " accents"
                           + dot + juce::String (countSlides (pattern)) + " slides";

        // Eyebrow over the title, 2 px apart
        const auto left = r.getX() + 16.0f;
        drawText (g, eyebrow.toUpperCase(), { left, r.getY() + 9.0f, 600.0f, 15.0f }, type::eyebrow(), c.textTertiary);

        const auto titleFont = type::headerTitle();
        drawText (g, patternName, { left, r.getY() + 26.0f, 600.0f, 22.0f }, titleFont, c.text);

        if (edited)
            drawBadge (g, left + textWidth (titleFont, patternName) + 10.0f, r.getY() + 37.0f, "Edited", c.neutral);
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
        g.fillRect (r.getX(), (float) clockCell.getBottom(), r.getWidth(), 1.0f);
        g.fillRect (r.getX(), (float) bankCell.getBottom(), r.getWidth(), 1.0f);

        // Instrument panel: four corner ticks
        drawRegistrationMarks (g, r);

        paintClock (g);

        drawKicker (g, bankCell, "04", "Pattern bank");
        drawKicker (g, transportCell, "05", "Transport");
        paintTransport (g);
    }

    void drawKicker (juce::Graphics& g, juce::Rectangle<int> cell, const juce::String& index, const juce::String& title)
    {
        const auto& c = colours();
        const auto line = juce::Rectangle<float> ((float) cell.getX() + 16.0f, (float) cell.getY() + 16.0f, (float) cell.getWidth() - 32.0f, 15.0f);
        drawText (g, index, line, type::index(), c.textTertiary);
        drawText (g, title.toUpperCase(), line.withTrimmedLeft (textWidth (type::index(), index) + 10.0f), type::eyebrow(), c.textSecondary);
    }

    void paintClock (juce::Graphics& g)
    {
        const auto& c = colours();
        drawKicker (g, clockCell, "03", "Clock");

        // RReadout, pixel font, large: the one instrument readout on this screen.
        const auto inner = clockCell.reduced (16).toFloat();
        const auto headY = inner.getY() + 15.0f + 12.0f;

        drawText (g, "TEMPO", { inner.getX(), headY, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::medium, 11.0f, 0.08f), c.textSecondary);
        drawText (g, transport.hostClock ? "HOST" : "INTERNAL", { inner.getX(), headY, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::regular, 11.0f, 0.04f), c.textTertiary, juce::Justification::centredRight);

        constexpr float em  = 40.0f;
        const auto figureY  = headY + 16.0f + 8.0f;
        const auto pixel    = font (Family::pixel, Weight::regular, em);
        const auto value    = juce::String (transport.bpm, 1);
        const juce::String ghost ("888.8");
        const auto ghostW   = pixelFigureWidth (ghost, pixel, em);
        const auto baseline = figureY + 20.0f + (pixel.getAscent() - pixel.getDescent()) * 0.5f;

        // The ghost digits sit behind the value at 9 %, both right-aligned in the same cells.
        drawPixelFigure (g, ghost, inner.getX() + ghostW, baseline, pixel, em, c.text.withAlpha (0.09f));
        drawPixelFigure (g, value, inner.getX() + ghostW, baseline, pixel, em, c.text);

        const auto unitFont = font (Family::mono, Weight::medium, 12.0f, 0.08f);
        juce::GlyphArrangement unitGlyphs;
        unitGlyphs.addLineOfText (unitFont, "BPM", inner.getX() + ghostW + 6.0f, baseline);
        g.setColour (c.textTertiary);
        unitGlyphs.draw (g);

        // Sub line
        const auto stepMs = 60000.0f / juce::jmax (1.0f, transport.bpm) / 4.0f;
        const auto swingValue = juce::roundToInt (proc.getState().getRawParameterValue (pid::swing)->load());
        const auto sub = juce::String (juce::roundToInt (stepMs)) + " ms a step " + juce::String::charToString (0x00b7)
                       + " swing " + juce::String (swingValue) + "%";
        drawText (g, sub.toUpperCase(), { inner.getX(), figureY + 40.0f + 12.0f, inner.getWidth(), 16.0f },
                  font (Family::mono, Weight::regular, 11.0f, 0.04f, true), c.textTertiary);
    }

    void paintTransport (juce::Graphics& g)
    {
        const auto& c = colours();
        const auto inner = transportCell.reduced (16).toFloat();

        // Output level
        const auto meterY = inner.getY() + 15.0f + 12.0f + (float) control::lg + 12.0f;
        const auto labelFont = font (Family::mono, Weight::medium, 10.0f, 0.08f);
        drawText (g, "OUTPUT", { inner.getX(), meterY, 60.0f, 16.0f }, labelFont, c.textTertiary);

        const auto shown = meterDb <= -59.5f ? juce::String (withMinus ("-inf")) : withMinus (juce::String (meterDb, 1));
        drawValueAndUnit (g, shown, "dB", { inner.getRight() - 72.0f, meterY, 72.0f, 16.0f },
                          font (Family::mono, Weight::medium, 12.0f, 0.0f, true), font (Family::mono, Weight::medium, 10.0f, 0.08f),
                          c.text, c.textTertiary, juce::Justification::centredRight);

        drawLevelMeter (g, { inner.getX() + 60.0f, meterY + 5.0f, inner.getWidth() - 60.0f - 84.0f, 6.0f }, meterDb);

        // Keys
        const auto keysY = meterY + 16.0f + 12.0f;
        auto x = inner.getX();
        const auto captionFont = font (Family::mono, Weight::medium, 10.0f, 0.08f);

        if (! proc.isStandalone())
        {
            const auto dot = " " + juce::String::charToString (0x00b7) + " ";
            drawText (g, "MIDI NOTES" + dot + "PLAY OR TRANSPOSE LIVE", { x, keysY, inner.getWidth(), 20.0f },
                      captionFont, c.textTertiary);
            return;
        }

        auto caption = [&] (const juce::String& text)
        {
            const auto w = textWidth (captionFont, text);
            drawText (g, text, { x, keysY, w + 2.0f, 20.0f }, captionFont, c.textTertiary);
            x += w + 12.0f;
        };

        x += drawKbd (g, { x, keysY, 60.0f, 20.0f }, "Space") + 6.0f;
        caption ("PLAY");
        x += drawKbd (g, { x, keysY, 30.0f, 20.0f }, "A") + 2.0f;
        drawText (g, juce::String::charToString (0x2013), { x, keysY, 8.0f, 20.0f }, captionFont, c.textTertiary, juce::Justification::centred);
        x += 10.0f;
        x += drawKbd (g, { x, keysY, 30.0f, 20.0f }, "K") + 6.0f;
        caption ("NOTES");
        x += drawKbd (g, { x, keysY, 30.0f, 20.0f }, "Z") + 2.0f;
        x += drawKbd (g, { x, keysY, 30.0f, 20.0f }, "X") + 6.0f;
        caption ("OCT");
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

        auto& state = proc.getState();
        const auto key = juce::roundToInt (state.getRawParameterValue (pid::key)->load());
        const auto oct = juce::roundToInt (state.getRawParameterValue (pid::octave)->load());
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

        if (headerChanged)
            repaint (headerArea);

        repaint (stripArea);
        repaint (clockCell.withTrimmedBottom (clockCell.getHeight() - 150));
        repaint (transportCell);
        focusOverlay.refresh();
    }

private:
    AsamProcessor& proc;

    Button clearButton  { "Clear",  Button::Variant::ghost,   Tone::neutral, Button::Size::md };
    Button randomButton { "Random", Button::Variant::outline, Tone::neutral, Button::Size::md };
    IconButton themeButton { "Switch light or dark theme", isDark() ? icons::sun() : icons::moon(), IconButton::Variant::ghost };
    IconButton audioButton { "Audio and MIDI settings", icons::settings(), IconButton::Variant::ghost };

    std::unique_ptr<Knob> cutoff, resonance, envMod, decay, accent, drive, tone, volume, tempo, swing;
    Segmented waveform { { "Saw", "Square" }, Segmented::Variant::inset, Segmented::Size::sm };
    std::unique_ptr<juce::ParameterAttachment> waveformAttachment, keyAttachment, octaveAttachment;

    StepGrid grid;
    Select keySelect { keyNames(), Select::Size::sm };
    Segmented octave { octaveOptions(), Segmented::Variant::inset, Segmented::Size::sm };

    Switch syncSwitch { "Host sync", Switch::Size::sm };
    std::unique_ptr<juce::ButtonParameterAttachment> syncAttachment;

    BankList bank;

    Button playButton { "Play", Button::Variant::solid, Tone::brand, Button::Size::lg };
    IconButton stopButton { "Stop and rewind", icons::square(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::lg };
    IconButton prevButton { "Previous pattern", icons::chevronLeft(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::lg };
    IconButton nextButton { "Next pattern", icons::chevronRight(), IconButton::Variant::outline, Tone::neutral, IconButton::Size::lg };

    FocusRingOverlay focusOverlay;

    juce::Rectangle<int> stripArea, headerArea, voicePanel, patternPanel, consoleArea, clockCell, bankCell, transportCell;
    int dividerX = 0;

    AsamProcessor::TransportView transport;
    Pattern pattern {};
    juce::String patternName;
    int presetIndex = 0;
    bool edited = false;
    std::uint32_t lastPatternVersion = 0;
    float meterDb = -60.0f;
    bool blinkOn = true;
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

void AsamEditor::paint (juce::Graphics& g)
{
    g.fillAll (colours().bgCanvas);
}

void AsamEditor::resized()
{
    const auto scale = (float) getWidth() / (float) W;
    view->setBounds (0, 0, W, H);
    view->setTransform (juce::AffineTransform::scale (scale));
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
