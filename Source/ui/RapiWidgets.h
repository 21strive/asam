#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "RapiIcons.h"
#include "RapiTheme.h"

namespace rapi
{

/** A figure and its unit, e.g. { "4.8", "kHz" }. */
struct ValueText
{
    juce::String number, unit;
};

//==============================================================================
/** Base for controls that show the 2 px focus line only when reached from the keyboard
    (:focus-visible). The ring is drawn outside the control by a FocusRingOverlay. */
class FocusVisible
{
public:
    virtual ~FocusVisible() = default;

    bool isFocusVisible() const noexcept                        { return focusVisible; }

    /** Area to outline, in the control's own coordinates. */
    virtual juce::Rectangle<float> getFocusRingArea() const = 0;

    /** CSS outline-offset. */
    virtual float getFocusRingOffset() const                    { return 2.0f; }

protected:
    bool focusVisible = false;

    void updateFocus (juce::Component& c, bool gained, juce::Component::FocusChangeType cause)
    {
        focusVisible = gained && cause == juce::Component::focusChangedByTabKey;
        c.repaint();

        if (auto* parent = c.getTopLevelComponent())
            parent->repaint();
    }
};

/** Sits on top of a view and draws the focus line around whichever control has visible focus. */
class FocusRingOverlay : public juce::Component
{
public:
    FocusRingOverlay();
    void paint (juce::Graphics&) override;
    void refresh();

private:
    juce::Rectangle<float> ring;
};

//==============================================================================
/**
    RKnob: a rotary control for values tuned by feel. Exact Rapi geometry: a 270 degree
    sweep of detent ticks that light up to the value, a flat cap with a 1 px rule and a
    straight indicator. Label above in mono capitals, value underneath.

    Drag up to turn clockwise ((max - min) / 200 per pixel, Shift for 1/1000), arrow keys
    step (Shift for ten), Page Up / Page Down, Home / End, and the wheel while focused.
    Double-click returns to the default.
*/
class Knob : public juce::Component,
             public FocusVisible
{
public:
    enum class Size { sm, md, lg };

    Knob (juce::RangedAudioParameter& parameter, const juce::String& label, Size size,
          Tone tone = Tone::neutral, int detents = 21);
    ~Knob() override;

    void setFormatter (std::function<ValueText (float)> f)   { formatter = std::move (f); repaint(); }
    void setStep (float newStep)                              { step = newStep; }

    static int dialSize (Size s) noexcept;
    static int preferredWidth (Size s) noexcept;
    static int preferredHeight (Size s) noexcept;

    float getValue() const noexcept                           { return value; }
    ValueText getValueText() const;
    juce::String getLabel() const                             { return label; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    void enablementChanged() override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::Rectangle<float> getFocusRingArea() const override   { return dialBounds(); }

private:
    friend class KnobAccessibility;

    juce::Rectangle<float> dialBounds() const;
    float proportion() const noexcept;
    float snap (float v) const noexcept;
    void setFromUser (float newValue, bool asGesture);

    juce::RangedAudioParameter& param;
    juce::ParameterAttachment attachment;
    juce::String label;
    Size size;
    Tone tone;
    int detents;
    float value = 0.0f, minValue = 0.0f, maxValue = 1.0f, interval = 0.0f, step = 0.01f;
    std::function<ValueText (float)> formatter;

    bool dragging = false, hovering = false;
    float dragRaw = 0.0f, lastDragY = 0.0f, wheelAccumulator = 0.0f;
    Animated capHover { *this };
};

//==============================================================================
/** RButton: square, 1 px rule, Geist Medium label, optional leading Lucide icon. */
class Button : public juce::Button,
               public FocusVisible
{
public:
    enum class Variant { solid, soft, outline, ghost };
    enum class Size { xs, sm, md, lg };

    Button (const juce::String& text, Variant variant = Variant::solid, Tone tone = Tone::neutral, Size size = Size::md);

    void setIcon (const juce::Path* icon)               { leadingIcon = icon; repaint(); }
    void setVariant (Variant v)                         { variant = v; repaint(); }
    void setTone (Tone t)                               { tone = t; repaint(); }
    void setPressedLook (bool shouldLookPressed)        { pressedLook = shouldLookPressed; repaint(); }

    int getIdealWidth() const;
    static int heightFor (Size s) noexcept;

    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
    void buttonStateChanged() override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    juce::Rectangle<float> getFocusRingArea() const override   { return getLocalBounds().toFloat(); }

private:
    juce::Font labelFont() const;
    int paddingX() const noexcept;
    int iconSize() const noexcept;
    int gap() const noexcept;

    Variant variant;
    Tone tone;
    Size size;
    const juce::Path* leadingIcon = nullptr;
    bool pressedLook = false;
    Animated hover { *this };
};

//==============================================================================
/** RIconButton: a square button holding one Lucide icon. */
class IconButton : public juce::Button,
                   public FocusVisible
{
public:
    enum class Variant { outline, ghost, soft, solid };
    enum class Size { xs, sm, md, lg };

    IconButton (const juce::String& accessibleName, const juce::Path& icon,
                Variant variant = Variant::outline, Tone tone = Tone::neutral, Size size = Size::md);

    void setIcon (const juce::Path& newIcon)            { icon = &newIcon; repaint(); }
    void setPressedLook (bool shouldLookPressed)        { pressedLook = shouldLookPressed; repaint(); }
    static int sizeFor (Size s) noexcept;

    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
    void buttonStateChanged() override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    juce::Rectangle<float> getFocusRingArea() const override   { return getLocalBounds().toFloat(); }

private:
    const juce::Path* icon;
    Variant variant;
    Tone tone;
    Size size;
    bool pressedLook = false;
    Animated hover { *this };
};

//==============================================================================
/** RSegmented: two to five options, one at a time. Inset (a ruled track with a sliding key)
    or the solid strip, where the selected key is filled with ink. */
class Segmented : public juce::Component,
                  public FocusVisible
{
public:
    enum class Variant { inset, pill };
    enum class Size { sm, md, lg };

    Segmented (juce::StringArray options, Variant variant = Variant::inset, Size size = Size::md);

    void setSelected (int index, bool notify);
    int getSelected() const noexcept                    { return selected; }
    int getIdealWidth() const;
    static int heightFor (Size s) noexcept;

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::Rectangle<float> getFocusRingArea() const override   { return optionBounds (selected); }
    float getFocusRingOffset() const override                   { return 0.0f; }

private:
    juce::Rectangle<float> optionBounds (int index) const;
    int optionAt (juce::Point<float> p) const;
    juce::Font optionFont() const;
    int paddingX() const noexcept;

    juce::StringArray options;
    Variant variant;
    Size size;
    int selected = 0, hovered = -1;
    Animated thumbX { *this }, thumbW { *this };
};

//==============================================================================
/** RSwitch: a square slot with a square key that slides across and turns the slot cobalt.
    For settings that take effect the moment you flip them. */
class Switch : public juce::Button,
               public FocusVisible
{
public:
    enum class Size { sm, md };

    explicit Switch (const juce::String& label, Size size = Size::md);

    int getIdealWidth() const;
    static int heightFor (Size s) noexcept;

    void paintButton (juce::Graphics&, bool isMouseOver, bool isDown) override;
    void buttonStateChanged() override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;
    juce::Rectangle<float> getFocusRingArea() const override;

private:
    Size size;
    Animated knob { *this };
};

//==============================================================================
/** RSelect: the square field with a chevron, opening a Rapi menu. */
class Select : public juce::Component,
               public FocusVisible
{
public:
    enum class Size { sm, md };

    Select (juce::StringArray items, Size size = Size::sm);

    void setSelected (int index, bool notify);
    int getSelected() const noexcept                    { return selected; }
    void setItems (juce::StringArray newItems)          { items = std::move (newItems); repaint(); }

    std::function<void (int)> onChange;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override  { repaint(); }
    void mouseExit (const juce::MouseEvent&) override   { repaint(); }
    bool keyPressed (const juce::KeyPress&) override;
    void focusGained (FocusChangeType) override;
    void focusLost (FocusChangeType) override;

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
    juce::Rectangle<float> getFocusRingArea() const override   { return getLocalBounds().toFloat(); }
    float getFocusRingOffset() const override                   { return 1.0f; }

private:
    void showMenu();

    juce::StringArray items;
    Size size;
    int selected = 0;
    bool menuOpen = false;
};

//==============================================================================
// Paint-only pieces

/** RRule-style panel head: running number, mono eyebrow, quiet metadata at the right. */
void drawPanelHead (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& index,
                    const juce::String& eyebrow, const juce::String& meta = {});

/** A key cap hint, e.g. "Space". Returns its width. */
float drawKbd (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text);

/** Status lamp with its word, mono capitals. Returns the width used. */
float drawStatus (juce::Graphics& g, juce::Point<float> leftCentre, const juce::String& text,
                  juce::Colour lamp, juce::Colour textColour, bool bar = false, bool small = false);

/** The level meter of the Rapi field recorder: 16 cobalt, 3 amber, 1 red segments, -60..0 dBFS. */
void drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> area, float levelDb);

/** Four corner ticks for an instrument panel (.r-marks). */
void drawRegistrationMarks (juce::Graphics& g, juce::Rectangle<float> area);

} // namespace rapi
