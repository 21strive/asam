#include "RapiLookAndFeel.h"
#include "RapiIcons.h"
#include "RapiWidgets.h"

namespace rapi
{

LookAndFeel::LookAndFeel()
{
    applyPalette();
}

void LookAndFeel::applyPalette()
{
    const auto& c = rapi::colours();

    setColourScheme ({ c.bgCanvas,        // windowBackground
                       c.bgSurface,       // widgetBackground
                       c.bgRaised,        // menuBackground
                       c.borderStrong,    // outline
                       c.text,            // defaultText
                       c.bgHover,         // defaultFill
                       c.onBrand,         // highlightedText
                       c.brandFill,       // highlightedFill
                       c.text });         // menuText

    setColour (juce::ResizableWindow::backgroundColourId, c.bgCanvas);
    setColour (juce::DocumentWindow::textColourId, c.text);
    setColour (juce::Label::textColourId, c.textSecondary);
    setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::ComboBox::backgroundColourId, c.bgSurface);
    setColour (juce::ComboBox::textColourId, c.text);
    setColour (juce::ComboBox::outlineColourId, c.borderControl);
    setColour (juce::ComboBox::arrowColourId, c.textSecondary);
    setColour (juce::ListBox::backgroundColourId, c.bgSurface);
    setColour (juce::ListBox::outlineColourId, c.border);
    setColour (juce::ListBox::textColourId, c.text);
    setColour (juce::TextButton::buttonColourId, c.bgSurface);
    setColour (juce::TextButton::buttonOnColourId, c.brandFill);
    setColour (juce::TextButton::textColourOffId, c.text);
    setColour (juce::TextButton::textColourOnId, c.onBrand);
    setColour (juce::ToggleButton::textColourId, c.text);
    setColour (juce::ToggleButton::tickColourId, c.onBrand);
    setColour (juce::ToggleButton::tickDisabledColourId, c.textDisabled);
    setColour (juce::TextEditor::backgroundColourId, c.bgSurface);
    setColour (juce::TextEditor::textColourId, c.text);
    setColour (juce::TextEditor::outlineColourId, c.borderControl);
    setColour (juce::TextEditor::focusedOutlineColourId, c.focus);
    setColour (juce::TextEditor::highlightColourId, c.bgSelected);
    setColour (juce::PopupMenu::backgroundColourId, c.bgRaised);
    setColour (juce::PopupMenu::textColourId, c.text);
    setColour (juce::PopupMenu::headerTextColourId, c.textTertiary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, c.bgHover);
    setColour (juce::PopupMenu::highlightedTextColourId, c.text);
    setColour (juce::ScrollBar::thumbColourId, c.bgThumb);
    setColour (juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, c.bgRaised);
    setColour (juce::TooltipWindow::textColourId, c.text);
    setColour (juce::TooltipWindow::outlineColourId, c.borderStrong);
    setColour (juce::AlertWindow::backgroundColourId, c.bgRaised);
    setColour (juce::AlertWindow::textColourId, c.text);
    setColour (juce::AlertWindow::outlineColourId, c.borderStrong);
    setColour (juce::CaretComponent::caretColourId, c.text);
}

//==============================================================================
juce::Typeface::Ptr LookAndFeel::getTypefaceForFont (const juce::Font& f)
{
    // Stock components ask for the default sans: give them Geist.
    const auto name = f.getTypefaceName();

    if (name.isEmpty() || name == juce::Font::getDefaultSansSerifFontName())
        return typefaces->get (Family::sans, f.isBold() ? Weight::semibold : Weight::regular);

    if (f.getTypefaceName() == juce::Font::getDefaultMonospacedFontName())
        return typefaces->get (Family::mono, Weight::regular);

    return LookAndFeel_V4::getTypefaceForFont (f);
}

juce::Font LookAndFeel::getPopupMenuFont()                      { return font (Family::sans, Weight::regular, 13.0f); }
juce::Font LookAndFeel::getComboBoxFont (juce::ComboBox&)       { return font (Family::sans, Weight::regular, 13.0f); }
juce::Font LookAndFeel::getTextButtonFont (juce::TextButton&, int) { return font (Family::sans, Weight::medium, 13.0f, -0.011f); }
juce::Font LookAndFeel::getLabelFont (juce::Label&)             { return font (Family::sans, Weight::regular, 13.0f); }

//==============================================================================
void LookAndFeel::drawPopupMenuBackgroundWithOptions (juce::Graphics& g, int width, int height, const juce::PopupMenu::Options&)
{
    // An r-panel: raised surface, strong 1 px rule. The window itself carries the short shadow.
    const auto& c = rapi::colours();
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (c.bgRaised);
    drawBorder (g, r, c.borderStrong);
}

void LookAndFeel::drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                                     bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                                     const juce::String& shortcutKeyText, const juce::Drawable*, const juce::Colour* textColour)
{
    const auto& c = rapi::colours();
    auto r = area.toFloat();

    if (isSeparator)
    {
        g.setColour (c.borderSubtle);
        g.fillRect (r.getX() - 4.0f, r.getCentreY(), r.getWidth() + 8.0f, 1.0f);
        return;
    }

    if (isHighlighted && isActive)
    {
        g.setColour (c.bgHover);
        g.fillRect (r);
    }

    auto content = r.reduced (8.0f, 4.0f);

    if (isTicked)
    {
        icons::draw (g, icons::check(), content.removeFromRight (14.0f).withSizeKeepingCentre (14.0f, 14.0f), c.brandText);
        content.removeFromRight (8.0f);
    }
    else if (hasSubMenu)
    {
        icons::draw (g, icons::chevronRight(), content.removeFromRight (14.0f).withSizeKeepingCentre (14.0f, 14.0f), c.textTertiary);
        content.removeFromRight (8.0f);
    }

    if (shortcutKeyText.isNotEmpty())
    {
        const auto kf = font (Family::mono, Weight::medium, 11.0f);
        const auto w = textWidth (kf, shortcutKeyText) + 10.0f;
        drawKbd (g, content.removeFromRight (w), shortcutKeyText);
        content.removeFromRight (16.0f);
    }

    const auto colour = ! isActive ? c.textDisabled : (textColour != nullptr ? *textColour : c.text);
    drawText (g, text, content, getPopupMenuFont(), colour);
}

void LookAndFeel::drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& name)
{
    drawText (g, name.toUpperCase(), area.toFloat().reduced (8.0f, 0.0f).withTrimmedTop (4.0f),
              font (Family::mono, Weight::semibold, 10.0f, 0.10f), rapi::colours().textTertiary);
}

void LookAndFeel::getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                             int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth  = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = juce::jmax (control::sm, standardMenuItemHeight);
    idealWidth  = juce::roundToInt (textWidth (getPopupMenuFont(), text)) + 16 + 22;
}

//==============================================================================
void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool isMouseOver, bool)
{
    // Stock text buttons become outline neutral RButtons.
    const auto& c = rapi::colours();
    const auto r = b.getLocalBounds().toFloat();

    if (! b.isEnabled())
    {
        drawBorder (g, r, c.border);
        return;
    }

    if (b.getToggleState())
    {
        g.setColour (c.brandFill);
        g.fillRect (r);
        return;
    }

    if (isMouseOver)
    {
        g.setColour (c.bgHover);
        g.fillRect (r);
    }

    drawBorder (g, r, isMouseOver ? c.textTertiary : c.borderStrong);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool isDown)
{
    const auto& c = rapi::colours();
    const auto colour = ! b.isEnabled() ? c.textDisabled : (b.getToggleState() ? c.onBrand : c.text);
    drawText (g, b.getButtonText(), b.getLocalBounds().toFloat().translated (0.0f, isDown ? 1.0f : 0.0f),
              getTextButtonFont (b, b.getHeight()), colour, juce::Justification::centred);
}

void LookAndFeel::drawTickBox (juce::Graphics& g, juce::Component& comp, float x, float y, float w, float h, bool ticked,
                               bool isEnabled, bool isMouseOver, bool)
{
    // RCheckbox: a 16 px square, cobalt when checked, with a white check.
    const auto& c = rapi::colours();
    const auto size = juce::jmin (16.0f, w, h);
    const auto box = juce::Rectangle<float> (x, y, w, h).withSizeKeepingCentre (size, size);
    juce::ignoreUnused (comp);

    if (! isEnabled)
    {
        g.setColour (ticked ? c.textDisabled : c.bgMuted);
        g.fillRect (box);
        drawBorder (g, box, ticked ? c.textDisabled : c.borderStrong);
    }
    else if (ticked)
    {
        g.setColour (c.brandFill);
        g.fillRect (box);
    }
    else
    {
        g.setColour (c.bgSurface);
        g.fillRect (box);
        drawBorder (g, box, isMouseOver ? c.textSecondary : c.borderControl);
    }

    if (ticked)
    {
        const auto iconBox = box.reduced (2.0f);
        const auto scale = iconBox.getWidth() / 24.0f;
        g.setColour (isEnabled ? c.onBrand : c.bgSurface);
        g.strokePath (icons::check(),
                      juce::PathStrokeType (3.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                      juce::AffineTransform::scale (scale).translated (iconBox.getX(), iconBox.getY()));
    }
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool isMouseOver, bool isDown)
{
    const auto& c = rapi::colours();
    const auto h = (float) b.getHeight();
    drawTickBox (g, b, 0.0f, (h - 16.0f) * 0.5f, 16.0f, 16.0f, b.getToggleState(), b.isEnabled(), isMouseOver, isDown);
    drawText (g, b.getButtonText(), b.getLocalBounds().toFloat().withTrimmedLeft (26.0f),
              font (Family::sans, Weight::regular, 13.0f), b.isEnabled() ? c.text : c.textDisabled);
}

//==============================================================================
void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    // The RSelect field: square, 1 px control rule, chevron at the right.
    const auto& c = rapi::colours();
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    const bool hot = box.isMouseOver (true) && box.isEnabled();

    g.setColour (box.isEnabled() ? c.bgSurface : c.bgMuted);
    g.fillRect (r);
    drawBorder (g, r, ! box.isEnabled() ? c.border : (hot || box.hasKeyboardFocus (true) ? c.textSecondary : c.borderControl));

    const auto chevron = height <= control::sm ? 14.0f : 16.0f;
    icons::draw (g, icons::chevronDown(), { r.getRight() - 8.0f - chevron, r.getCentreY() - chevron * 0.5f, chevron, chevron },
                 box.isEnabled() ? c.textSecondary : c.textDisabled);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (10, 1, box.getWidth() - 42, box.getHeight() - 2);
    label.setFont (getComboBoxFont (box));
}

void LookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;

    const auto& c = rapi::colours();
    const auto colour = label.isEnabled() ? label.findColour (juce::Label::textColourId) : c.textDisabled;
    drawText (g, label.getText(), label.getBorderSize().subtractedFrom (label.getLocalBounds()).toFloat(),
              getLabelFont (label), colour, label.getJustificationType());
}

void LookAndFeel::fillTextEditorBackground (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    g.setColour (editor.isEnabled() ? rapi::colours().bgSurface : rapi::colours().bgMuted);
    g.fillRect (0, 0, width, height);
}

void LookAndFeel::drawTextEditorOutline (juce::Graphics& g, int width, int height, juce::TextEditor& editor)
{
    const auto& c = rapi::colours();
    const auto r = juce::Rectangle<int> (width, height).toFloat();

    if (editor.hasKeyboardFocus (true) && ! editor.isReadOnly())
        drawBorder (g, r, c.focus, 2.0f);
    else
        drawBorder (g, r, c.borderControl);
}

//==============================================================================
void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool vertical,
                                 int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown)
{
    const auto& c = rapi::colours();
    juce::Rectangle<float> thumb;

    if (vertical)
        thumb = { (float) x + 3.0f, (float) thumbStart, (float) width - 6.0f, (float) thumbSize };
    else
        thumb = { (float) thumbStart, (float) y + 3.0f, (float) thumbSize, (float) height - 6.0f };

    g.setColour (isMouseDown ? c.textTertiary : (isMouseOver ? c.borderControl : c.bgThumb));
    g.fillRect (thumb);
}

juce::Rectangle<int> LookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                    juce::Rectangle<int> parentArea)
{
    // RTooltip: 12 px medium text, 5 x 8 padding.
    const auto f = font (Family::sans, Weight::medium, 12.0f);
    const auto w = juce::jmin (280, juce::roundToInt (textWidth (f, tipText)) + 18);
    const auto h = 26;

    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 12,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 8) : screenPos.y + 8,
                                 w, h).constrainedWithin (parentArea);
}

void LookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto& c = rapi::colours();
    const auto r = juce::Rectangle<int> (width, height).toFloat();
    g.fillAll (c.bgRaised);
    drawBorder (g, r, c.borderStrong);
    drawText (g, text, r.reduced (8.0f, 5.0f), font (Family::sans, Weight::medium, 12.0f), c.text);
}

void LookAndFeel::drawLevelMeter (juce::Graphics& g, int width, int height, float level)
{
    // The device input meter, drawn as the Rapi segment meter.
    const auto db = juce::Decibels::gainToDecibels (level, -60.0f);
    rapi::drawLevelMeter (g, juce::Rectangle<int> (width, height).toFloat().withSizeKeepingCentre ((float) width, 6.0f), db);
}

//==============================================================================
void LookAndFeel::drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g, int w, int h, int, int,
                                              const juce::Image*, bool)
{
    // Only used if a window has no native title bar: the 32 px mono strip.
    const auto& c = rapi::colours();
    g.fillAll (c.bgSurface);
    g.setColour (c.border);
    g.fillRect (0, h - 1, w, 1);
    drawText (g, window.getName().toUpperCase(), juce::Rectangle<int> (w, h).toFloat().reduced (16.0f, 0.0f),
              type::strip(), c.text, juce::Justification::centred);
}

void LookAndFeel::fillResizableWindowBackground (juce::Graphics& g, int, int, const juce::BorderSize<int>&,
                                                 juce::ResizableWindow&)
{
    g.fillAll (rapi::colours().bgCanvas);
}

} // namespace rapi
