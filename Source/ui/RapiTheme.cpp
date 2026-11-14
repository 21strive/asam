#include "RapiTheme.h"

#include <AsamAssets.h>

namespace rapi
{

namespace
{
    juce::Colour hex (juce::uint32 rgb)                 { return juce::Colour (0xff000000 | rgb); }
    juce::Colour hexA (juce::uint32 rgb, float alpha)   { return hex (rgb).withAlpha (alpha); }

    Palette makeDark()
    {
        Palette p;
        p.bgCanvas      = hex (0x0e0e0e);
        p.bgSurface     = hex (0x151515);
        p.bgRaised      = hex (0x1b1b1b);
        p.bgMuted       = hex (0x1b1b1b);
        p.bgHover       = hex (0x222222);
        p.bgPressed     = hex (0x2a2a2a);
        p.bgSelected    = hex (0x1d2437);   // cobalt-500 16% into #151515
        p.bgTrack       = hex (0x222222);
        p.bgThumb       = hex (0x3a3a3a);
        p.bgOverlay     = hexA (0x000000, 0.62f);

        p.borderSubtle  = hex (0x212121);
        p.border        = hex (0x2a2a2a);
        p.borderStrong  = hex (0x424242);
        p.borderControl = hex (0x707070);

        p.text          = hex (0xededed);
        p.textSecondary = hex (0xb4b4b4);
        p.textTertiary  = hex (0x8f8f8f);
        p.textDisabled  = hex (0x5e5e5e);

        p.ink = hex (0xededed);  p.inkHover = hex (0xffffff);  p.onInk = hex (0x151515);

        p.brand          = hex (0x4970eb);  // cobalt-500
        p.brandFill      = hex (0x3254cc);  // cobalt-600
        p.brandFillHover = hex (0x1b3aab);  // cobalt-700
        p.brandText      = hex (0xa4befe);  // cobalt-300
        p.onBrand        = hex (0xffffff);

        p.signal     = hex (0xff4f00);
        p.signalText = hex (0xfda68b);
        p.onSignal   = hex (0x151515);

        p.focus = hex (0x7e94e2);           // cobalt-400
        p.marks = p.text;

        p.statusGood     = hex (0x0fa44d);
        p.statusWarning  = hex (0xf2b037);
        p.statusCritical = hex (0xcb2430);
        p.statusOff      = hex (0xa3a3a3);
        p.chartTrack     = hex (0x222222);
        p.chart1         = hex (0x4970eb);  // cobalt-500
        p.chartGrid      = hex (0x222222);
        p.chartAxis      = hex (0x424242);
        p.chartLabel     = hex (0x8f8f8f);
        p.chartInk       = hex (0xededed);

        p.neutral    = { p.ink, p.inkHover, p.onInk, p.bgHover, p.bgPressed, p.textSecondary, p.borderStrong, p.statusOff };
        p.brandTone  = { p.brandFill, p.brandFillHover, p.onBrand, hexA (0x4970eb, 0.15f), hexA (0x4970eb, 0.24f),
                         p.brandText, hexA (0x4970eb, 0.42f), p.brand };
        p.signalTone = { p.signal, hex (0xd44303), p.onSignal, hexA (0xff4f00, 0.14f), hexA (0xff4f00, 0.278f),
                         p.signalText, hexA (0xff4f00, 0.40f), p.signal };
        p.success    = { hex (0x0fa44d), hex (0x52bd6f), hex (0x151515), hexA (0x0fa44d, 0.14f), hexA (0x0fa44d, 0.26f),
                         hex (0x87d497), hexA (0x0fa44d, 0.36f), p.statusGood };
        p.warning    = { hex (0xf2b037), hex (0xe39f07), hex (0x151515), hexA (0xf2b037, 0.13f), hexA (0xf2b037, 0.27f),
                         hex (0xfbc876), hexA (0xf2b037, 0.36f), p.statusWarning };
        p.danger     = { hex (0xcb2430), hex (0xe94649), hex (0xffffff), hexA (0xe94649, 0.15f), hexA (0xe94649, 0.27f),
                         hex (0xfea39d), hexA (0xe94649, 0.40f), p.statusCritical };
        return p;
    }

    Palette makeLight()
    {
        Palette p;
        p.bgCanvas      = hex (0xfafafa);
        p.bgSurface     = hex (0xffffff);
        p.bgRaised      = hex (0xffffff);
        p.bgMuted       = hex (0xf4f4f4);
        p.bgHover       = hex (0xededed);
        p.bgPressed     = hex (0xe5e5e5);
        p.bgSelected    = hex (0xf3f7ff);
        p.bgTrack       = hex (0xededed);
        p.bgThumb       = hex (0xffffff);
        p.bgOverlay     = hexA (0x0e0e0e, 0.42f);

        p.borderSubtle  = hex (0xededed);
        p.border        = hex (0xe5e5e5);
        p.borderStrong  = hex (0xc2c2c2);
        p.borderControl = hex (0x8a8a8a);

        p.text          = hex (0x151515);
        p.textSecondary = hex (0x525252);
        p.textTertiary  = hex (0x6b6b6b);
        p.textDisabled  = hex (0xa3a3a3);

        p.ink = hex (0x151515);  p.inkHover = hex (0x3d3d3d);  p.onInk = hex (0xffffff);

        p.brand          = hex (0x1b3aab);
        p.brandFill      = hex (0x1b3aab);
        p.brandFillHover = hex (0x172c86);
        p.brandText      = hex (0x1b3aab);
        p.onBrand        = hex (0xffffff);

        p.signal     = hex (0xff4f00);
        p.signalText = hex (0xaa3607);
        p.onSignal   = hex (0x151515);

        p.focus = hex (0x1b3aab);
        p.marks = p.text;

        p.statusGood     = hex (0x0fa44d);
        p.statusWarning  = hex (0xf2b037);
        p.statusCritical = hex (0xcb2430);
        p.statusOff      = hex (0xa3a3a3);
        p.chartTrack     = hex (0xededed);
        p.chart1         = hex (0x3254cc);  // cobalt-600
        p.chartGrid      = hex (0xededed);  // neutral-100
        p.chartAxis      = hex (0xc2c2c2);  // neutral-300
        p.chartLabel     = hex (0x6b6b6b);  // neutral-500
        p.chartInk       = hex (0x151515);  // neutral-900

        p.neutral    = { p.ink, p.inkHover, p.onInk, p.bgHover, p.bgPressed, p.textSecondary, p.borderStrong, p.statusOff };
        p.brandTone  = { p.brandFill, p.brandFillHover, p.onBrand, hex (0xf3f7ff), hex (0xe5edff), p.brandText, hex (0xc9d9fe), p.brand };
        p.signalTone = { p.signal, hex (0xd44303), p.onSignal, hex (0xfff4f1), hex (0xffdaca), p.signalText, hex (0xfeccbc), p.signal };
        p.success    = { hex (0x0d873e), hex (0x046c30), hex (0xffffff), hex (0xeefbf0), hex (0xcfebd7), hex (0x046c30), hex (0xb6e8bf), p.statusGood };
        p.warning    = { hex (0xf2b037), hex (0xe39f07), hex (0x151515), hex (0xfff5e8), hex (0xfdeacc), hex (0x8e6206), hex (0xf5d4a0), p.statusWarning };
        p.danger     = { hex (0xcb2430), hex (0xa8081f), hex (0xffffff), hex (0xfff4f3), hex (0xf8d7d8), hex (0xa8081f), hex (0xfecbc6), p.statusCritical };
        return p;
    }

    std::atomic<bool> darkMode { true };
    Typefaces* currentTypefaces = nullptr;

    juce::Typeface::Ptr loadTypeface (const char* originalFilename)
    {
        for (int i = 0; i < AsamAssets::namedResourceListSize; ++i)
        {
            if (juce::String (AsamAssets::originalFilenames[i]) == originalFilename)
            {
                int size = 0;

                if (auto* data = AsamAssets::getNamedResource (AsamAssets::namedResourceList[i], size))
                    return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
            }
        }

        jassertfalse;
        return {};
    }
}

const ToneColours& Palette::tone (Tone t) const noexcept
{
    switch (t)
    {
        case Tone::brand:   return brandTone;
        case Tone::signal:  return signalTone;
        case Tone::success: return success;
        case Tone::warning: return warning;
        case Tone::danger:  return danger;
        case Tone::neutral: break;
    }

    return neutral;
}

const Palette& darkPalette()    { static const Palette p = makeDark();  return p; }
const Palette& lightPalette()   { static const Palette p = makeLight(); return p; }
const Palette& colours()        { return darkMode.load() ? darkPalette() : lightPalette(); }
bool isDark()                   { return darkMode.load(); }
void setDark (bool shouldBeDark){ darkMode.store (shouldBeDark); }

//==============================================================================
Typefaces::Typefaces()
{
    sans[0] = loadTypeface ("Geist-Regular.ttf");
    sans[1] = loadTypeface ("Geist-Medium.ttf");
    sans[2] = loadTypeface ("Geist-SemiBold.ttf");
    mono[0] = loadTypeface ("GeistMono-Regular.ttf");
    mono[1] = loadTypeface ("GeistMono-Medium.ttf");
    mono[2] = loadTypeface ("GeistMono-SemiBold.ttf");
    pixel   = loadTypeface ("GeistPixel-Circle.ttf");
    currentTypefaces = this;
}

Typefaces::~Typefaces()
{
    if (currentTypefaces == this)
        currentTypefaces = nullptr;
}

juce::Typeface::Ptr Typefaces::get (Family family, Weight weight) const
{
    const auto w = (int) weight;

    switch (family)
    {
        case Family::mono:  return mono[w];
        case Family::pixel: return pixel;
        case Family::sans:  break;
    }

    return sans[w];
}

juce::Font font (Family family, Weight weight, float sizePx, float trackingEm, bool tabular)
{
    juce::FontOptions options;

    if (currentTypefaces != nullptr)
        if (auto face = currentTypefaces->get (family, weight))
            options = juce::FontOptions (face);

    options = options.withPointHeight (sizePx);

    if (tabular)
        options = options.withFeatureEnabled (juce::FontFeatureTag ("tnum"));

    juce::Font f (options);

    if (trackingEm != 0.0f)
        f = f.withExtraKerningFactor (trackingEm * f.getHeightToPointsFactor());

    return f;
}

namespace type
{
    juce::Font h3()           { return font (Family::sans, Weight::semibold, 18.0f, -0.011f); }
    juce::Font title()        { return font (Family::sans, Weight::semibold, 14.0f, -0.011f); }
    juce::Font headerTitle()  { return font (Family::sans, Weight::semibold, 18.0f, -0.022f); }
    juce::Font body()         { return font (Family::sans, Weight::regular, 14.0f); }
    juce::Font bodyMedium()   { return font (Family::sans, Weight::medium, 14.0f, -0.011f); }
    juce::Font bodySm()       { return font (Family::sans, Weight::regular, 13.0f); }
    juce::Font caption()      { return font (Family::sans, Weight::regular, 12.0f); }
    juce::Font eyebrow()      { return font (Family::mono, Weight::semibold, 11.0f, 0.10f); }
    juce::Font label()        { return font (Family::mono, Weight::medium, 11.0f, 0.04f); }
    juce::Font index()        { return font (Family::mono, Weight::regular, 10.0f, 0.04f, true); }
    juce::Font strip()        { return font (Family::mono, Weight::semibold, 10.0f, 0.10f); }
}

//==============================================================================
float motion::easeOut (float t) noexcept
{
    // Solve x(u) = t for the curve cubic-bezier(0.2, 0, 0, 1), then return y(u).
    t = juce::jlimit (0.0f, 1.0f, t);
    float lo = 0.0f, hi = 1.0f, u = t;

    for (int i = 0; i < 24; ++i)
    {
        u = 0.5f * (lo + hi);
        const auto inv = 1.0f - u;
        const auto x = 3.0f * inv * inv * u * 0.2f + u * u * u;

        if (x < t) lo = u; else hi = u;
    }

    const auto inv = 1.0f - u;
    return 3.0f * inv * u * u + u * u * u;
}

//==============================================================================
void drawText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, const juce::Font& f,
               juce::Colour colour, juce::Justification just, bool ellipsis)
{
    g.setFont (f);
    g.setColour (colour);
    g.drawText (text, area, just, ellipsis);
}

float textWidth (const juce::Font& f, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (f, text);
}

void drawBorder (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness)
{
    g.setColour (c);
    g.drawRect (r, thickness);
}

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> controlBounds, float offset)
{
    g.setColour (colours().focus);
    g.drawRect (controlBounds.expanded (offset + 2.0f), 2.0f);
}

void drawLamp (juce::Graphics& g, juce::Point<float> centre, float diameter, juce::Colour c)
{
    g.setColour (c);
    g.fillEllipse (juce::Rectangle<float> (diameter, diameter).withCentre (centre));
}

float drawValueAndUnit (juce::Graphics& g, const juce::String& number, const juce::String& unit,
                        juce::Rectangle<float> area, const juce::Font& numberFont, const juce::Font& unitFont,
                        juce::Colour numberColour, juce::Colour unitColour,
                        juce::Justification just, float gap)
{
    const auto wNumber = textWidth (numberFont, number);
    const auto wUnit   = unit.isEmpty() ? 0.0f : textWidth (unitFont, unit);
    const auto total   = wNumber + (unit.isEmpty() ? 0.0f : gap + wUnit);

    float x = area.getX();

    if (just.testFlags (juce::Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (just.testFlags (juce::Justification::right))
        x = area.getRight() - total;

    const auto baseline = area.getCentreY() + (numberFont.getAscent() - numberFont.getDescent()) * 0.5f;

    juce::GlyphArrangement numberGlyphs;
    numberGlyphs.addLineOfText (numberFont, number, x, baseline);
    g.setColour (numberColour);
    numberGlyphs.draw (g);

    if (unit.isNotEmpty())
    {
        juce::GlyphArrangement unitGlyphs;
        unitGlyphs.addLineOfText (unitFont, unit, x + wNumber + gap, baseline);
        g.setColour (unitColour);
        unitGlyphs.draw (g);
    }

    return total;
}

//==============================================================================
Animated::Animated (juce::Component& ownerToRepaint, float initial)
    : owner (ownerToRepaint), value (initial), from (initial), to (initial),
      vblank (&ownerToRepaint, [this] { tick(); })
{
}

void Animated::setTarget (float target, int ms)
{
    if (juce::approximatelyEqual (target, to))
        return;

    from = value;
    to = target;
    durationMs = ms;
    startMs = juce::Time::getMillisecondCounterHiRes();
    running = ms > 0;

    if (! running)
        value = to;

    owner.repaint();
}

void Animated::snapTo (float v)
{
    value = from = to = v;
    running = false;
    owner.repaint();
}

void Animated::tick()
{
    if (! running)
        return;

    const auto t = (float) ((juce::Time::getMillisecondCounterHiRes() - startMs) / (double) durationMs);

    if (t >= 1.0f)
    {
        value = to;
        running = false;
    }
    else
    {
        value = from + (to - from) * motion::easeOut (t);
    }

    owner.repaint();
}

} // namespace rapi
