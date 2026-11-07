#include "RapiWidgets.h"

namespace rapi
{

namespace
{
    constexpr float sweepStartDegrees = -135.0f;
    constexpr float sweepDegrees      = 270.0f;

    float signOf (float v) noexcept     { return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f); }
}

//==============================================================================
FocusRingOverlay::FocusRingOverlay()
{
    setInterceptsMouseClicks (false, false);
    setAccessible (false);
}

void FocusRingOverlay::refresh()
{
    juce::Rectangle<float> newRing;

    if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
        if (auto* target = dynamic_cast<FocusVisible*> (focused))
            if (target->isFocusVisible() && focused->isShowing()
                && getParentComponent() != nullptr && getParentComponent()->isParentOf (focused))
            {
                const auto local = target->getFocusRingArea();

                if (! local.isEmpty())
                    newRing = getLocalArea (focused, local.expanded (target->getFocusRingOffset() + 2.0f));
            }

    if (newRing != ring)
    {
        repaint (ring.getSmallestIntegerContainer().expanded (2));
        ring = newRing;
        repaint (ring.getSmallestIntegerContainer().expanded (2));
    }
}

void FocusRingOverlay::paint (juce::Graphics& g)
{
    if (ring.isEmpty())
        return;

    g.setColour (colours().focus);
    g.drawRect (ring, 2.0f);
}

//==============================================================================
class KnobAccessibility : public juce::AccessibilityHandler
{
public:
    explicit KnobAccessibility (Knob& k)
        : AccessibilityHandler (k, juce::AccessibilityRole::slider, juce::AccessibilityActions {},
                                Interfaces { std::make_unique<ValueInterface> (k) }),
          knob (k)
    {
    }

    juce::String getTitle() const override  { return knob.label; }

private:
    class ValueInterface : public juce::AccessibilityRangedNumericValueInterface
    {
    public:
        explicit ValueInterface (Knob& k) : knob (k) {}

        bool isReadOnly() const override                { return false; }
        double getCurrentValue() const override         { return knob.value; }
        void setValue (double v) override               { knob.setFromUser ((float) v, false); }

        AccessibleValueRange getRange() const override
        {
            return { { knob.minValue, knob.maxValue }, knob.step };
        }

    private:
        Knob& knob;
    };

    Knob& knob;
};

Knob::Knob (juce::RangedAudioParameter& p, const juce::String& labelText, Size s, Tone t, int d)
    : param (p),
      attachment (p, [this] (float v) { value = v; repaint(); }),
      label (labelText), size (s), tone (t), detents (juce::jmax (2, d))
{
    const auto& range = p.getNormalisableRange();
    minValue = range.start;
    maxValue = range.end;
    interval = range.interval;
    step     = interval > 0.0f ? interval : (maxValue - minValue) / 100.0f;
    value    = p.convertFrom0to1 (p.getValue());

    formatter = [this] (float v) { return ValueText { param.getText (param.convertTo0to1 (v), 32), {} }; };

    setWantsKeyboardFocus (true);
    setTitle (labelText);
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    attachment.sendInitialUpdate();
}

Knob::~Knob() = default;

int Knob::dialSize (Size s) noexcept
{
    switch (s)
    {
        case Size::sm: return 48;
        case Size::lg: return 88;
        case Size::md: break;
    }

    return 64;
}

int Knob::preferredWidth (Size s) noexcept   { return dialSize (s) + 32; }

int Knob::preferredHeight (Size s) noexcept
{
    const auto gap = s == Size::sm ? 6 : 8;
    return 14 + gap + dialSize (s) + gap + 16;
}

juce::Rectangle<float> Knob::dialBounds() const
{
    const auto d   = (float) dialSize (size);
    const auto gap = size == Size::sm ? 6.0f : 8.0f;
    return { ((float) getWidth() - d) * 0.5f, 14.0f + gap, d, d };
}

float Knob::proportion() const noexcept
{
    return maxValue > minValue ? juce::jlimit (0.0f, 1.0f, (value - minValue) / (maxValue - minValue)) : 0.0f;
}

float Knob::snap (float v) const noexcept
{
    v = juce::jlimit (minValue, maxValue, v);

    if (interval > 0.0f)
        v = juce::jlimit (minValue, maxValue, minValue + std::round ((v - minValue) / interval) * interval);

    return v;
}

ValueText Knob::getValueText() const
{
    return formatter != nullptr ? formatter (value) : ValueText { juce::String (value, 2), {} };
}

void Knob::setFromUser (float newValue, bool asGesture)
{
    const auto v = snap (newValue);

    if (juce::approximatelyEqual (v, value))
        return;

    value = v;

    if (asGesture)
        attachment.setValueAsPartOfGesture (v);
    else
        attachment.setValueAsCompleteGesture (v);

    repaint();
}

void Knob::paint (juce::Graphics& g)
{
    const auto& c   = colours();
    const auto b    = getLocalBounds().toFloat();
    const bool sm   = size == Size::sm;
    const bool lg   = size == Size::lg;
    const auto gap  = sm ? 6.0f : 8.0f;

    // Label, mono capitals
    drawText (g, label.toUpperCase(), b.withHeight (14.0f),
              font (Family::mono, Weight::medium, sm ? 10.0f : 11.0f, 0.08f), c.textSecondary,
              juce::Justification::centred);

    // Dial
    const auto dial       = dialBounds();
    const auto cx         = dial.getCentreX();
    const auto cy         = dial.getCentreY();
    const auto half       = dial.getWidth() * 0.5f;
    const auto tickLength = sm ? 4.0f : (lg ? 7.0f : 5.0f);
    const auto tickWidth  = lg ? 2.0f : 1.5f;
    const auto outer      = half - 1.0f;
    const auto cap        = outer - tickLength - (sm ? 3.0f : (lg ? 6.0f : 4.0f));
    const auto p          = proportion();
    const auto lit        = tone == Tone::signal ? c.signal : (tone == Tone::brand ? c.brand : c.ink);

    for (int i = 0; i < detents; ++i)
    {
        const auto r = (float) i / (float) (detents - 1);
        juce::Path tick;
        tick.addRectangle (cx - tickWidth * 0.5f, cy - outer, tickWidth, tickLength);
        g.setColour (r <= p + 1.0e-6f ? lit : c.borderStrong);
        g.fillPath (tick, juce::AffineTransform::rotation (juce::degreesToRadians (sweepStartDegrees + sweepDegrees * r), cx, cy));
    }

    const auto capCircle = juce::Rectangle<float> (2.0f * (cap - 0.5f), 2.0f * (cap - 0.5f)).withCentre ({ cx, cy });
    g.setColour (c.bgSurface);
    g.fillEllipse (capCircle);
    g.setColour (c.borderStrong.interpolatedWith (c.textTertiary, capHover.get()));
    g.drawEllipse (capCircle, 1.0f);

    juce::Path indicator;
    indicator.startNewSubPath (cx, cy - cap * 0.32f);
    indicator.lineTo (cx, cy - cap + 4.0f);
    g.setColour (lit);
    g.strokePath (indicator,
                  juce::PathStrokeType (lg ? 2.5f : 2.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::square),
                  juce::AffineTransform::rotation (juce::degreesToRadians (sweepStartDegrees + sweepDegrees * p), cx, cy));

    // Value under the knob: the figure in mono, the unit small and quiet
    const auto vt = getValueText();
    drawValueAndUnit (g, vt.number, vt.unit,
                      b.withTop (dial.getBottom() + gap).withHeight (16.0f),
                      font (Family::mono, Weight::medium, sm ? 11.0f : (lg ? 13.0f : 12.0f), 0.0f, true),
                      font (Family::mono, Weight::medium, 10.0f, 0.08f),
                      c.text, c.textTertiary, juce::Justification::centred);
}

void Knob::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || ! e.mods.isLeftButtonDown())
        return;

    if (! dialBounds().expanded (6.0f).contains (e.position))
        return;

    grabKeyboardFocus();
    attachment.beginGesture();
    dragging  = true;
    dragRaw   = value;
    lastDragY = e.position.y;
    capHover.setTarget (1.0f, motion::fast);
}

void Knob::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging)
        return;

    const auto perPixel = (maxValue - minValue) / (e.mods.isShiftDown() ? 1000.0f : 200.0f);
    dragRaw   = juce::jlimit (minValue, maxValue, dragRaw + (lastDragY - e.position.y) * perPixel);
    lastDragY = e.position.y;
    setFromUser (dragRaw, true);
}

void Knob::mouseUp (const juce::MouseEvent&)
{
    if (! dragging)
        return;

    attachment.endGesture();
    dragging = false;
    capHover.setTarget (hovering ? 1.0f : 0.0f, motion::fast);
}

void Knob::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (isEnabled() && dialBounds().expanded (6.0f).contains (e.position))
        setFromUser (param.convertFrom0to1 (param.getDefaultValue()), false);
}

void Knob::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    // Like RKnob: the wheel turns the knob only while it has focus.
    if (! isEnabled() || ! hasKeyboardFocus (false))
    {
        Component::mouseWheelMove (e, wheel);
        return;
    }

    const auto delta = wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 256.0f;
    const auto multiplier = e.mods.isShiftDown() ? 10.0f : 1.0f;

    if (std::abs (delta) >= 40.0f)
    {
        wheelAccumulator = 0.0f;
        setFromUser (value + signOf (delta) * step * multiplier, false);
    }
    else
    {
        wheelAccumulator += delta;

        if (std::abs (wheelAccumulator) >= 40.0f)
        {
            setFromUser (value + signOf (wheelAccumulator) * step * multiplier, false);
            wheelAccumulator = 0.0f;
        }
    }
}

void Knob::mouseEnter (const juce::MouseEvent&)
{
    hovering = true;
    capHover.setTarget (1.0f, motion::fast);
}

void Knob::mouseExit (const juce::MouseEvent&)
{
    hovering = false;

    if (! dragging)
        capHover.setTarget (0.0f, motion::fast);
}

bool Knob::keyPressed (const juce::KeyPress& key)
{
    if (! isEnabled())
        return false;

    const auto code  = key.getKeyCode();
    const auto small = step * (key.getModifiers().isShiftDown() ? 10.0f : 1.0f);
    const auto big   = juce::jmax (step * 10.0f, (maxValue - minValue) / 10.0f);

    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey)      setFromUser (value + small, false);
    else if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) setFromUser (value - small, false);
    else if (code == juce::KeyPress::pageUpKey)                                  setFromUser (value + big, false);
    else if (code == juce::KeyPress::pageDownKey)                                setFromUser (value - big, false);
    else if (code == juce::KeyPress::homeKey)                                    setFromUser (minValue, false);
    else if (code == juce::KeyPress::endKey)                                     setFromUser (maxValue, false);
    else return false;

    return true;
}

void Knob::focusGained (FocusChangeType cause)   { updateFocus (*this, true, cause); }
void Knob::focusLost (FocusChangeType cause)     { updateFocus (*this, false, cause); }
void Knob::enablementChanged()                   { setAlpha (isEnabled() ? 1.0f : 0.45f); }

std::unique_ptr<juce::AccessibilityHandler> Knob::createAccessibilityHandler()
{
    return std::make_unique<KnobAccessibility> (*this);
}

//==============================================================================
Button::Button (const juce::String& text, Variant v, Tone t, Size s)
    : juce::Button (text), variant (v), tone (t), size (s)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Button::heightFor (Size s) noexcept
{
    switch (s)
    {
        case Size::xs: return control::xs;
        case Size::sm: return control::sm;
        case Size::lg: return control::lg;
        case Size::md: break;
    }

    return control::md;
}

juce::Font Button::labelFont() const
{
    switch (size)
    {
        case Size::xs: return font (Family::sans, Weight::medium, 12.0f);
        case Size::sm: return font (Family::sans, Weight::medium, 13.0f, -0.011f);
        case Size::md:
        case Size::lg: break;
    }

    return font (Family::sans, Weight::medium, 14.0f, -0.011f);
}

int Button::paddingX() const noexcept
{
    switch (size)
    {
        case Size::xs: return 8;
        case Size::sm: return 10;
        case Size::lg: return 16;
        case Size::md: break;
    }

    return 12;
}

int Button::iconSize() const noexcept   { return size == Size::xs ? 14 : 16; }

int Button::gap() const noexcept
{
    return size == Size::xs ? 4 : (size == Size::sm ? 6 : 8);
}

int Button::getIdealWidth() const
{
    const auto text = getButtonText();
    auto width = 2 * paddingX() + juce::roundToInt (std::ceil (textWidth (labelFont(), text)));

    if (leadingIcon != nullptr)
        width += iconSize() + (text.isNotEmpty() ? gap() : 0) - 2;

    return width;
}

void Button::buttonStateChanged()
{
    hover.setTarget (isEnabled() && isOver() ? 1.0f : 0.0f, motion::fast);
}

void Button::paintButton (juce::Graphics& g, bool, bool isDown)
{
    const auto& c  = colours();
    const auto& tc = c.tone (tone);
    const auto r   = getLocalBounds().toFloat();
    const auto h   = hover.get();
    const bool neutral = tone == Tone::neutral;

    juce::Colour bg, border, fg;

    switch (variant)
    {
        case Variant::solid:
            bg = tc.solid.interpolatedWith (tc.solidHover, h);
            fg = tc.onSolid;
            break;

        case Variant::soft:
            bg = tc.soft.interpolatedWith (tc.softHover, h);
            fg = neutral ? c.text : tc.text;
            break;

        case Variant::outline:
            border = neutral ? tc.border.interpolatedWith (c.textTertiary, h) : tc.solid;
            bg     = (neutral ? c.bgHover : tc.soft).withMultipliedAlpha (h);
            fg     = neutral ? c.text : tc.text;
            break;

        case Variant::ghost:
            bg = (neutral ? c.bgHover : tc.soft).withMultipliedAlpha (h);
            fg = neutral ? c.textSecondary.interpolatedWith (c.text, h) : tc.text;
            break;
    }

    if (pressedLook && variant != Variant::solid)
    {
        border = tc.solid;
        bg     = tc.solid;
        fg     = tc.onSolid;
    }

    if (! isEnabled())
    {
        fg = c.textDisabled;
        bg = (variant == Variant::outline || variant == Variant::ghost) ? juce::Colour() : c.bgPressed;
        border = variant == Variant::outline ? c.border : juce::Colour();
    }

    if (! bg.isTransparent())
    {
        g.setColour (bg);
        g.fillRect (r);
    }

    if (! border.isTransparent())
        drawBorder (g, r, border);

    // Content: optional leading icon, then the label, centred. Pressed moves it down 1 px.
    const auto text      = getButtonText();
    const auto f         = labelFont();
    const auto textW     = textWidth (f, text);
    const auto iconW     = leadingIcon != nullptr ? (float) iconSize() : 0.0f;
    const auto innerGap  = (leadingIcon != nullptr && text.isNotEmpty()) ? (float) gap() : 0.0f;
    const auto contentW  = iconW + innerGap + textW;
    auto x               = r.getCentreX() - contentW * 0.5f;
    const auto dy        = (isDown && isEnabled()) ? 1.0f : 0.0f;

    if (leadingIcon != nullptr)
    {
        icons::draw (g, *leadingIcon, { x, r.getCentreY() - iconW * 0.5f + dy, iconW, iconW }, fg);
        x += iconW + innerGap;
    }

    if (text.isNotEmpty())
        drawText (g, text, { x, r.getY() + dy, textW + 1.0f, r.getHeight() }, f, fg, juce::Justification::centredLeft, false);
}

void Button::focusGained (FocusChangeType cause)   { updateFocus (*this, true, cause); }
void Button::focusLost (FocusChangeType cause)     { updateFocus (*this, false, cause); }

//==============================================================================
IconButton::IconButton (const juce::String& accessibleName, const juce::Path& iconPath, Variant v, Tone t, Size s)
    : juce::Button (accessibleName), icon (&iconPath), variant (v), tone (t), size (s)
{
    setWantsKeyboardFocus (true);
    setTooltip (accessibleName);
    setTitle (accessibleName);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int IconButton::sizeFor (Size s) noexcept
{
    switch (s)
    {
        case Size::xs: return control::xs;
        case Size::sm: return control::sm;
        case Size::lg: return control::lg;
        case Size::md: break;
    }

    return control::md;
}

void IconButton::buttonStateChanged()
{
    hover.setTarget (isEnabled() && isOver() ? 1.0f : 0.0f, motion::fast);
}

void IconButton::paintButton (juce::Graphics& g, bool, bool isDown)
{
    const auto& c  = colours();
    const auto& tc = c.tone (tone);
    const auto r   = getLocalBounds().toFloat();
    const auto h   = hover.get();
    const bool neutral = tone == Tone::neutral;

    juce::Colour bg, border, fg = c.text;

    switch (variant)
    {
        case Variant::outline:
            border = c.borderStrong.interpolatedWith (c.textTertiary, h);
            bg     = c.bgHover.withMultipliedAlpha (h);
            break;

        case Variant::ghost:
            bg = tc.soft.withMultipliedAlpha (h);
            fg = (neutral ? c.textSecondary : tc.text).interpolatedWith (c.text, h);
            break;

        case Variant::soft:
            bg = tc.soft.interpolatedWith (tc.softHover, h);
            fg = neutral ? c.textSecondary.interpolatedWith (c.text, h) : tc.text;
            break;

        case Variant::solid:
            bg = tc.solid.interpolatedWith (tc.solidHover, h);
            fg = tc.onSolid;
            break;
    }

    if (pressedLook)
    {
        border = c.borderStrong;
        bg     = c.bgPressed;
        fg     = c.text;
    }

    if (! isEnabled())
    {
        fg = c.textDisabled;

        if (variant == Variant::outline) border = c.border;
        if (variant == Variant::solid)   bg = c.bgPressed;
        else                             bg = juce::Colour();
    }

    if (! bg.isTransparent())
    {
        g.setColour (bg);
        g.fillRect (r);
    }

    if (! border.isTransparent())
        drawBorder (g, r, border);

    const auto iconPx = size == Size::xs ? 14.0f : (size == Size::lg ? 18.0f : 16.0f);
    const auto dy = (isDown && isEnabled()) ? 1.0f : 0.0f;
    icons::draw (g, *icon, juce::Rectangle<float> (iconPx, iconPx).withCentre (r.getCentre().translated (0.0f, dy)), fg);
}

void IconButton::focusGained (FocusChangeType cause)   { updateFocus (*this, true, cause); }
void IconButton::focusLost (FocusChangeType cause)     { updateFocus (*this, false, cause); }

//==============================================================================
Segmented::Segmented (juce::StringArray opts, Variant v, Size s)
    : options (std::move (opts)), variant (v), size (s)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Segmented::heightFor (Size s) noexcept
{
    switch (s)
    {
        case Size::sm: return control::sm;
        case Size::lg: return control::lg;
        case Size::md: break;
    }

    return control::md;
}

int Segmented::paddingX() const noexcept
{
    return size == Size::sm ? 10 : (size == Size::lg ? 16 : 12);
}

juce::Font Segmented::optionFont() const
{
    return font (Family::sans, Weight::medium, size == Size::sm ? 12.0f : (size == Size::lg ? 14.0f : 13.0f));
}

int Segmented::getIdealWidth() const
{
    float total = 0.0f;

    for (auto& o : options)
        total += std::ceil (textWidth (optionFont(), o)) + 2.0f * (float) paddingX();

    if (variant == Variant::inset)
        return juce::roundToInt (total + 2.0f * (float) (options.size() - 1) + 6.0f);

    return juce::roundToInt (total - (float) (options.size() - 1) + 1.0f);
}

juce::Rectangle<float> Segmented::optionBounds (int index) const
{
    if (! juce::isPositiveAndBelow (index, options.size()))
        return {};

    const auto b = getLocalBounds().toFloat();
    const auto n = options.size();

    // Natural widths, stretched to fill the component (block layout).
    juce::Array<float> widths;
    float natural = 0.0f;

    for (auto& o : options)
    {
        widths.add (std::ceil (textWidth (optionFont(), o)) + 2.0f * (float) paddingX());
        natural += widths.getLast();
    }

    if (variant == Variant::inset)
    {
        const auto inner = b.reduced (3.0f);
        const auto avail = inner.getWidth() - 2.0f * (float) (n - 1);
        const auto scale = natural > 0.0f ? avail / natural : 1.0f;
        float x = inner.getX();

        for (int i = 0; i < index; ++i)
            x += widths[i] * scale + 2.0f;

        return { x, inner.getY(), widths[index] * scale, inner.getHeight() };
    }

    const auto avail = b.getWidth() + (float) (n - 1);
    const auto scale = natural > 0.0f ? avail / natural : 1.0f;
    float x = b.getX();

    for (int i = 0; i < index; ++i)
        x += widths[i] * scale - 1.0f;

    return { x, b.getY(), widths[index] * scale, b.getHeight() };
}

int Segmented::optionAt (juce::Point<float> p) const
{
    for (int i = 0; i < options.size(); ++i)
        if (optionBounds (i).contains (p))
            return i;

    return -1;
}

void Segmented::resized()
{
    const auto r = optionBounds (selected);
    thumbX.snapTo (r.getX());
    thumbW.snapTo (r.getWidth());
}

void Segmented::setSelected (int index, bool notify)
{
    index = juce::jlimit (0, juce::jmax (0, options.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    const auto r = optionBounds (selected);
    thumbX.setTarget (r.getX(), motion::base);
    thumbW.setTarget (r.getWidth(), motion::base);
    repaint();

    if (notify && onChange != nullptr)
        onChange (selected);

    if (auto* handler = getAccessibilityHandler())
        handler->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
}

void Segmented::paint (juce::Graphics& g)
{
    const auto& c = colours();
    const auto b  = getLocalBounds().toFloat();
    const auto f  = optionFont();

    if (variant == Variant::inset)
    {
        g.setColour (c.bgSurface);
        g.fillRect (b);
        drawBorder (g, b, c.border);

        for (int i = 0; i < options.size(); ++i)
            if (i == hovered && i != selected && isEnabled())
            {
                g.setColour (c.bgHover);
                g.fillRect (optionBounds (i));
            }

        // The selected option is a surface key with a strong rule, sliding into place.
        const auto sel = optionBounds (selected);
        const juce::Rectangle<float> thumb (thumbX.get(), sel.getY(), thumbW.get(), sel.getHeight());
        g.setColour (c.bgSurface);
        g.fillRect (thumb);
        drawBorder (g, thumb, c.borderStrong);

        for (int i = 0; i < options.size(); ++i)
        {
            const auto colour = ! isEnabled() ? c.textDisabled
                              : (i == selected || i == hovered) ? c.text : c.textSecondary;
            drawText (g, options[i], optionBounds (i), f, colour, juce::Justification::centred, false);
        }

        return;
    }

    // Solid strip: keys on shared rules, the selected one filled with ink.
    for (int i = 0; i < options.size(); ++i)
    {
        if (i == selected)
            continue;

        const auto r = optionBounds (i);

        if (i == hovered && isEnabled())
        {
            g.setColour (c.bgHover);
            g.fillRect (r);
        }

        drawBorder (g, r, c.borderStrong);
        drawText (g, options[i], r, f, isEnabled() ? (i == hovered ? c.text : c.textSecondary) : c.textDisabled,
                  juce::Justification::centred, false);
    }

    const auto r = optionBounds (selected);
    g.setColour (c.ink);
    g.fillRect (r);
    drawText (g, options[selected], r, f, c.onInk, juce::Justification::centred, false);
}

void Segmented::mouseMove (const juce::MouseEvent& e)
{
    const auto h = optionAt (e.position);

    if (h != hovered)
    {
        hovered = h;
        repaint();
    }
}

void Segmented::mouseExit (const juce::MouseEvent&)
{
    hovered = -1;
    repaint();
}

void Segmented::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled())
        return;

    const auto i = optionAt (e.position);

    if (i >= 0)
        setSelected (i, true);
}

bool Segmented::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey)
    {
        setSelected (selected - 1, true);
        return true;
    }

    if (code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey)
    {
        setSelected (selected + 1, true);
        return true;
    }

    return false;
}

void Segmented::focusGained (FocusChangeType cause)    { updateFocus (*this, true, cause); }
void Segmented::focusLost (FocusChangeType cause)      { updateFocus (*this, false, cause); }

std::unique_ptr<juce::AccessibilityHandler> Segmented::createAccessibilityHandler()
{
    class ValueInterface : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ValueInterface (Segmented& s) : seg (s) {}
        bool isReadOnly() const override                        { return false; }
        juce::String getCurrentValueAsString() const override   { return seg.options[seg.selected]; }
        void setValueAsString (const juce::String& v) override  { seg.setSelected (seg.options.indexOf (v), true); }

    private:
        Segmented& seg;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::group,
                                                         juce::AccessibilityActions {},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
}

//==============================================================================
Switch::Switch (const juce::String& labelText, Size s)
    : juce::Button (labelText), size (s)
{
    setClickingTogglesState (true);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int Switch::heightFor (Size s) noexcept     { return s == Size::sm ? 18 : 20; }

int Switch::getIdealWidth() const
{
    const auto trackW = size == Size::sm ? 26 : 32;
    const auto gap    = size == Size::sm ? 8 : 10;
    const auto f      = font (Family::sans, Weight::regular, size == Size::sm ? 13.0f : 14.0f);
    return trackW + gap + juce::roundToInt (std::ceil (textWidth (f, getButtonText())));
}

juce::Rectangle<float> Switch::getFocusRingArea() const
{
    const auto trackW = size == Size::sm ? 26.0f : 32.0f;
    const auto trackH = size == Size::sm ? 14.0f : 18.0f;
    return { 0.0f, ((float) getHeight() - trackH) * 0.5f, trackW, trackH };
}

void Switch::buttonStateChanged()
{
    knob.setTarget (getToggleState() ? 1.0f : 0.0f, motion::fast);
    repaint();
}

void Switch::paintButton (juce::Graphics& g, bool isMouseOver, bool)
{
    const auto& c    = colours();
    const bool sm    = size == Size::sm;
    const auto w     = sm ? 26.0f : 32.0f;
    const auto h     = sm ? 14.0f : 18.0f;
    const auto thumb = sm ? 8.0f : 12.0f;
    const auto on    = knob.get();
    const auto track = juce::Rectangle<float> (0.0f, ((float) getHeight() - h) * 0.5f, w, h);
    const bool hot   = isMouseOver && isEnabled();

    auto offBorder = hot ? c.textSecondary : c.borderControl;
    auto offThumb  = hot ? c.textSecondary : c.textTertiary;
    auto onFill    = hot ? c.brandFillHover : c.brandFill;

    juce::Colour trackFill   = c.bgSurface.interpolatedWith (onFill, on);
    juce::Colour trackBorder = offBorder.interpolatedWith (onFill, on);
    juce::Colour thumbFill   = offThumb.interpolatedWith (c.onBrand, on);

    if (! isEnabled())
    {
        const bool isOn = getToggleState();
        trackFill   = isOn ? c.textDisabled : c.bgMuted;
        trackBorder = isOn ? c.textDisabled : c.borderStrong;
        thumbFill   = isOn ? c.bgSurface : c.borderStrong;
    }

    g.setColour (trackFill);
    g.fillRect (track);
    drawBorder (g, track, trackBorder);

    const auto inset = (h - 2.0f - thumb) * 0.5f;
    g.setColour (thumbFill);
    g.fillRect (track.getX() + 1.0f + inset + on * (w - h), track.getY() + 1.0f + inset, thumb, thumb);

    const auto gap = sm ? 8.0f : 10.0f;
    drawText (g, getButtonText(), getLocalBounds().toFloat().withTrimmedLeft (w + gap),
              font (Family::sans, Weight::regular, sm ? 13.0f : 14.0f),
              isEnabled() ? c.text : c.textDisabled, juce::Justification::centredLeft);
}

void Switch::focusGained (FocusChangeType cause)   { updateFocus (*this, true, cause); }
void Switch::focusLost (FocusChangeType cause)     { updateFocus (*this, false, cause); }

//==============================================================================
Select::Select (juce::StringArray itemList, Size s)
    : items (std::move (itemList)), size (s)
{
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Select::setSelected (int index, bool notify)
{
    index = juce::jlimit (0, juce::jmax (0, items.size() - 1), index);

    if (index == selected)
        return;

    selected = index;
    repaint();

    if (notify && onChange != nullptr)
        onChange (selected);
}

void Select::paint (juce::Graphics& g)
{
    const auto& c  = colours();
    const auto b   = getLocalBounds().toFloat();
    const bool sm  = size == Size::sm;
    const bool hot = (isMouseOver (true) || menuOpen) && isEnabled();

    g.setColour (isEnabled() ? c.bgSurface : c.bgMuted);
    g.fillRect (b);
    drawBorder (g, b, ! isEnabled() ? c.border : (hot ? c.textSecondary : c.borderControl));

    const auto padX = sm ? 8.0f : 10.0f;
    drawText (g, items[selected], b.withTrimmedLeft (padX).withTrimmedRight (32.0f),
              font (Family::sans, Weight::regular, sm ? 13.0f : 14.0f),
              isEnabled() ? c.text : c.textDisabled, juce::Justification::centredLeft);

    const auto chevron = sm ? 14.0f : 16.0f;
    const auto right   = sm ? 6.0f : 8.0f;
    icons::draw (g, icons::chevronDown(),
                 { b.getRight() - right - chevron, b.getCentreY() - chevron * 0.5f, chevron, chevron },
                 isEnabled() ? c.textSecondary : c.textDisabled);
}

void Select::showMenu()
{
    juce::PopupMenu menu;
    menu.setLookAndFeel (&getLookAndFeel());

    for (int i = 0; i < items.size(); ++i)
        menu.addItem (i + 1, items[i], true, i == selected);

    menuOpen = true;
    repaint();

    juce::Component::SafePointer<Select> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (this)
                            .withMinimumWidth (getWidth())
                            .withStandardItemHeight (control::sm),
                        [safe] (int result)
                        {
                            if (safe == nullptr)
                                return;

                            safe->menuOpen = false;
                            safe->repaint();

                            if (result > 0)
                                safe->setSelected (result - 1, true);
                        });
}

void Select::mouseDown (const juce::MouseEvent&)
{
    if (isEnabled())
        showMenu();
}

bool Select::keyPressed (const juce::KeyPress& key)
{
    const auto code = key.getKeyCode();

    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::spaceKey)
    {
        showMenu();
        return true;
    }

    if (code == juce::KeyPress::upKey)   { setSelected (selected - 1, true); return true; }
    if (code == juce::KeyPress::downKey) { setSelected (selected + 1, true); return true; }

    return false;
}

void Select::focusGained (FocusChangeType cause)   { updateFocus (*this, true, cause); }
void Select::focusLost (FocusChangeType cause)     { updateFocus (*this, false, cause); }

std::unique_ptr<juce::AccessibilityHandler> Select::createAccessibilityHandler()
{
    class ValueInterface : public juce::AccessibilityTextValueInterface
    {
    public:
        explicit ValueInterface (Select& s) : sel (s) {}
        bool isReadOnly() const override                        { return false; }
        juce::String getCurrentValueAsString() const override   { return sel.items[sel.selected]; }
        void setValueAsString (const juce::String& v) override  { sel.setSelected (sel.items.indexOf (v), true); }

    private:
        Select& sel;
    };

    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::comboBox,
                                                         juce::AccessibilityActions {}.addAction (juce::AccessibilityActionType::press,
                                                                                                  [this] { showMenu(); }),
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<ValueInterface> (*this) });
}

//==============================================================================
void drawPanelHead (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& index,
                    const juce::String& eyebrow, const juce::String& meta)
{
    const auto& c = colours();

    g.setColour (c.border);
    g.fillRect (area.getX(), area.getBottom() - 1.0f, area.getWidth(), 1.0f);

    // padding 14 16 12; one 15 px line of mono text
    const auto line = juce::Rectangle<float> (area.getX() + 16.0f, area.getY() + 14.0f, area.getWidth() - 32.0f, 15.0f);
    const auto indexFont   = type::index();
    const auto eyebrowFont = type::eyebrow();

    auto x = line.getX();

    if (index.isNotEmpty())
    {
        drawText (g, index, line.withX (x), indexFont, c.textTertiary);
        x += textWidth (indexFont, index) + 10.0f;
    }

    drawText (g, eyebrow.toUpperCase(), line.withLeft (x), eyebrowFont, c.textSecondary);

    if (meta.isNotEmpty())
        drawText (g, meta.toUpperCase(), line, font (Family::mono, Weight::medium, 11.0f, 0.06f), c.textTertiary,
                  juce::Justification::centredRight);
}

float drawKbd (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text)
{
    const auto& c = colours();
    const auto f  = font (Family::mono, Weight::medium, 11.0f);
    const auto w  = juce::jmax (20.0f, std::ceil (textWidth (f, text)) + 10.0f);
    const auto r  = juce::Rectangle<float> (area.getX(), area.getCentreY() - 10.0f, w, 20.0f);

    g.setColour (c.bgSurface);
    g.fillRect (r);
    drawBorder (g, r, c.border);
    g.setColour (c.borderStrong);
    g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
    drawText (g, text, r, f, c.textSecondary, juce::Justification::centred, false);
    return w;
}

float drawStatus (juce::Graphics& g, juce::Point<float> leftCentre, const juce::String& text,
                  juce::Colour lamp, juce::Colour textColour, bool bar, bool small)
{
    const auto lampSize = small ? 5.0f : 6.0f;
    const auto gap      = small ? 6.0f : 8.0f;
    float lampW;

    if (bar)
    {
        lampW = 2.0f;
        const auto barH = small ? 10.0f : 12.0f;
        g.setColour (lamp);
        g.fillRect (leftCentre.x, leftCentre.y - barH * 0.5f, 2.0f, barH);
    }
    else
    {
        lampW = lampSize;
        drawLamp (g, { leftCentre.x + lampSize * 0.5f, leftCentre.y }, lampSize, lamp);
    }

    if (text.isEmpty())
        return lampW;

    const auto f = font (Family::mono, Weight::medium, small ? 10.0f : 11.0f, 0.08f);
    const auto label = text.toUpperCase();
    const auto w = textWidth (f, label);
    drawText (g, label, { leftCentre.x + lampW + gap, leftCentre.y - 8.0f, w + 2.0f, 16.0f }, f, textColour,
              juce::Justification::centredLeft, false);
    return lampW + gap + w;
}

void drawLevelMeter (juce::Graphics& g, juce::Rectangle<float> area, float levelDb)
{
    struct Group { float from; int count; juce::Colour lit; };

    const auto& c = colours();
    const Group groups[] { { -60.0f, 16, c.brand }, { -12.0f, 3, c.statusWarning }, { -3.0f, 1, c.statusCritical } };

    constexpr int total = 20;
    constexpr float gap = 2.0f;
    const auto blockW = (area.getWidth() - gap * (float) (total - 1)) / (float) total;
    auto x = area.getX();

    for (const auto& group : groups)
    {
        const auto litCount = juce::jlimit (0, group.count, (int) std::floor ((levelDb - group.from) / 3.0f + 1.0e-6f));

        for (int i = 0; i < group.count; ++i)
        {
            g.setColour (i < litCount ? group.lit : c.chartTrack);
            g.fillRect (x, area.getY(), blockW, area.getHeight());
            x += blockW + gap;
        }
    }
}

void drawRegistrationMarks (juce::Graphics& g, juce::Rectangle<float> a)
{
    constexpr float m = 9.0f;
    g.setColour (colours().marks);

    g.fillRect (a.getX(), a.getY(), m, 1.0f);
    g.fillRect (a.getX(), a.getY(), 1.0f, m);
    g.fillRect (a.getRight() - m, a.getY(), m, 1.0f);
    g.fillRect (a.getRight() - 1.0f, a.getY(), 1.0f, m);
    g.fillRect (a.getX(), a.getBottom() - 1.0f, m, 1.0f);
    g.fillRect (a.getX(), a.getBottom() - m, 1.0f, m);
    g.fillRect (a.getRight() - m, a.getBottom() - 1.0f, m, 1.0f);
    g.fillRect (a.getRight() - 1.0f, a.getBottom() - m, 1.0f, m);
}

} // namespace rapi
