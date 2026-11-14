#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/*
    Rapi 0.2, the design language of 21 Strive, mapped onto JUCE.

    Layers follow the system: primitive ramps -> semantic roles -> components.
    Components read only the semantic roles below, never a raw ramp value, so the
    dark and light themes are two blocks that re-point the same names.
*/
namespace rapi
{

//==============================================================================
enum class Tone { neutral, brand, signal, success, warning, danger };

/** The six --tone-* variables a toned component reads. */
struct ToneColours
{
    juce::Colour solid, solidHover, onSolid, soft, softHover, text, border, lamp;
};

struct Palette
{
    // Surfaces
    juce::Colour bgCanvas, bgSurface, bgRaised, bgMuted, bgHover, bgPressed, bgSelected, bgTrack, bgThumb, bgOverlay;
    // Lines
    juce::Colour borderSubtle, border, borderStrong, borderControl;
    // Text
    juce::Colour text, textSecondary, textTertiary, textDisabled;
    // Ink, brand and signal
    juce::Colour ink, inkHover, onInk;
    juce::Colour brand, brandFill, brandFillHover, brandText, onBrand;
    juce::Colour signal, signalText, onSignal;
    juce::Colour focus, marks;
    // Status and charts
    juce::Colour statusGood, statusWarning, statusCritical, statusOff;
    juce::Colour chartTrack, chart1, chartGrid, chartAxis, chartLabel, chartInk;

    ToneColours neutral, brandTone, signalTone, success, warning, danger;

    const ToneColours& tone (Tone t) const noexcept;
};

const Palette& darkPalette();
const Palette& lightPalette();

/** The palette in use. Theme islands are not needed here: the whole plug-in follows one theme. */
const Palette& colours();
bool isDark();
void setDark (bool shouldBeDark);

//==============================================================================
// Type: Geist reads, Geist Mono labels, Geist Pixel is the one instrument readout.
enum class Family { sans, mono, pixel };
enum class Weight { regular, medium, semibold };

/** Holds the embedded typefaces. Keep one alive (SharedResourcePointer) while drawing. */
class Typefaces
{
public:
    Typefaces();
    ~Typefaces();

    juce::Typeface::Ptr get (Family, Weight) const;

private:
    juce::Typeface::Ptr sans[3], mono[3], pixel;
};

/** sizePx is the CSS font-size; trackingEm is CSS letter-spacing in em. */
juce::Font font (Family family, Weight weight, float sizePx, float trackingEm = 0.0f, bool tabular = false);

namespace type
{
    // The Rapi type scale (size / tracking / weight)
    juce::Font h3();            // Sans 18, -1.1%, 600
    juce::Font title();         // Sans 14, -1.1%, 600
    juce::Font headerTitle();   // Sans 18 (text-xl), -2.2%, 600
    juce::Font body();          // Sans 14, 400
    juce::Font bodyMedium();    // Sans 14, 500 (buttons, nav)
    juce::Font bodySm();        // Sans 13, 400
    juce::Font caption();       // Sans 12, 400
    juce::Font eyebrow();       // Mono 11, +10%, 600, caps
    juce::Font label();         // Mono 11, +4%, 500, caps
    juce::Font index();         // Mono 10, +4%, 400, tabular
    juce::Font strip();         // Mono 10, +10%, 600, caps
}

//==============================================================================
// Spacing sits on a 4 px grid, with 2 and 6 for tight spots inside controls.
namespace space
{
    inline constexpr int px = 1, s0_5 = 2, s1 = 4, s1_5 = 6, s2 = 8, s2_5 = 10, s3 = 12, s4 = 16,
                         s5 = 20, s6 = 24, s7 = 28, s8 = 32, s10 = 40, s12 = 48, s16 = 64;
}

namespace control
{
    inline constexpr int xs = 24, sm = 28, md = 32, lg = 40, xl = 48;
}

namespace motion
{
    inline constexpr int instant = 70, fast = 110, base = 160, slow = 240, slower = 420;

    /** cubic-bezier(0.2, 0, 0, 1), the --r-ease-out curve. */
    float easeOut (float t) noexcept;
}

//==============================================================================
// Drawing helpers

/** Text, optionally upper-cased. Rapi mono labels are always capitals. */
void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& f,
               juce::Colour colour, juce::Justification just = juce::Justification::centredLeft, bool ellipsis = true);

float textWidth (const juce::Font& f, const juce::String& text);

/** A 1 px rule drawn inside the rectangle, the way CSS borders sit. */
void drawBorder (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness = 1.0f);

/** The crisp 2 px focus line, offset 2 px outside the control. Never a halo. */
void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> controlBounds, float offset = 2.0f);

/** A status lamp: a circle, because it is a state. */
void drawLamp (juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour c);

/** A value and its unit: the figure in Geist Mono, the unit small and quiet. Returns the width used. */
float drawValueAndUnit (juce::Graphics& g, const juce::String& number, const juce::String& unit,
                        juce::Rectangle<float> area, const juce::Font& numberFont, const juce::Font& unitFont,
                        juce::Colour numberColour, juce::Colour unitColour,
                        juce::Justification just, float gap = 4.0f);

//==============================================================================
/** Animates one float with the Rapi motion tokens and repaints its owner every frame. */
class Animated
{
public:
    explicit Animated (juce::Component& ownerToRepaint, float initial = 0.0f);

    void setTarget (float target, int durationMs);
    void snapTo (float value);
    float get() const noexcept      { return value; }
    float target() const noexcept   { return to; }

private:
    void tick();

    juce::Component& owner;
    float value, from, to;
    double startMs = 0.0;
    int durationMs = 0;
    bool running = false;
    juce::VBlankAttachment vblank;
};

} // namespace rapi
