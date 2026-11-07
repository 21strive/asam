#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "RapiTheme.h"

namespace rapi
{

/**
    Styles the stock JUCE parts we don't draw ourselves — popup menus (the RMenu), tooltips,
    and the controls inside the standalone audio settings dialog — with Rapi tokens:
    square corners, hairline rules, Geist and Geist Mono, cobalt only for selection.
*/
class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    /** Re-points every colour after a theme change. */
    void applyPalette();

    // Fonts
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;

    // Popup menu (RMenu inside an r-panel)
    void drawPopupMenuBackgroundWithOptions (juce::Graphics&, int width, int height, const juce::PopupMenu::Options&) override;
    int getPopupMenuBorderSizeWithOptions (const juce::PopupMenu::Options&) override  { return 4; }
    void drawPopupMenuItem (juce::Graphics&, const juce::Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcutKeyText, const juce::Drawable* icon,
                            const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader (juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                    int& idealWidth, int& idealHeight) override;

    // Buttons and toggles
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool isMouseOverButton, bool isButtonDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isMouseOverButton, bool isButtonDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isMouseOverButton, bool isButtonDown) override;
    void drawTickBox (juce::Graphics&, juce::Component&, float x, float y, float w, float h, bool ticked,
                      bool isEnabled, bool isMouseOverButton, bool isButtonDown) override;

    // Fields
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown, int buttonX, int buttonY,
                       int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
    void fillTextEditorBackground (juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline (juce::Graphics&, int width, int height, juce::TextEditor&) override;

    // Scrollbars, tooltips, meters
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                        int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
    int getDefaultScrollbarWidth() override                                    { return 10; }
    juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                           juce::Rectangle<int> parentArea) override;
    void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;
    void drawLevelMeter (juce::Graphics&, int width, int height, float level) override;

    // Windows
    void drawDocumentWindowTitleBar (juce::DocumentWindow&, juce::Graphics&, int w, int h, int titleSpaceX,
                                     int titleSpaceW, const juce::Image* icon, bool drawTitleTextOnLeft) override;
    void fillResizableWindowBackground (juce::Graphics&, int w, int h, const juce::BorderSize<int>&,
                                        juce::ResizableWindow&) override;

private:
    juce::SharedResourcePointer<Typefaces> typefaces;
};

} // namespace rapi
