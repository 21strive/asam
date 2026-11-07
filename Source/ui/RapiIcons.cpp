#include "RapiIcons.h"

namespace rapi::icons
{

namespace
{
    juce::Path fromSvg (std::initializer_list<const char*> paths)
    {
        juce::Path result;

        for (auto* d : paths)
            result.addPath (juce::Drawable::parseSVGPath (d));

        return result;
    }
}

const juce::Path& play()
{
    static const auto p = fromSvg ({ "M5 5a2 2 0 0 1 3.008-1.728l11.997 6.998a2 2 0 0 1 .003 3.458l-12 7A2 2 0 0 1 5 19z" });
    return p;
}

const juce::Path& pause()
{
    static const auto p = []
    {
        juce::Path path;
        path.addRoundedRectangle (14.0f, 3.0f, 5.0f, 18.0f, 1.0f);
        path.addRoundedRectangle (5.0f, 3.0f, 5.0f, 18.0f, 1.0f);
        return path;
    }();
    return p;
}

const juce::Path& square()
{
    static const auto p = []
    {
        juce::Path path;
        path.addRoundedRectangle (3.0f, 3.0f, 18.0f, 18.0f, 2.0f);
        return path;
    }();
    return p;
}

const juce::Path& chevronLeft()     { static const auto p = fromSvg ({ "m15 18-6-6 6-6" }); return p; }
const juce::Path& chevronRight()    { static const auto p = fromSvg ({ "m9 18 6-6-6-6" });  return p; }
const juce::Path& chevronDown()     { static const auto p = fromSvg ({ "m6 9 6 6 6-6" });   return p; }
const juce::Path& check()           { static const auto p = fromSvg ({ "M20 6 9 17l-5-5" }); return p; }

const juce::Path& eraser()
{
    static const auto p = fromSvg ({ "M21 21H8a2 2 0 0 1-1.42-.587l-3.994-3.999a2 2 0 0 1 0-2.828l10-10a2 2 0 0 1 2.829 0l5.999 6a2 2 0 0 1 0 2.828L12.834 21",
                                     "m5.082 11.09 8.828 8.828" });
    return p;
}

const juce::Path& shuffle()
{
    static const auto p = fromSvg ({ "m18 14 4 4-4 4", "m18 2 4 4-4 4",
                                     "M2 18h1.973a4 4 0 0 0 3.3-1.7l5.454-8.6a4 4 0 0 1 3.3-1.7H22",
                                     "M2 6h1.972a4 4 0 0 1 3.6 2.2",
                                     "M22 18h-6.041a4 4 0 0 1-3.3-1.8l-.359-.45" });
    return p;
}

const juce::Path& settings()
{
    static const auto p = []
    {
        auto path = fromSvg ({ "M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915" });
        path.addEllipse (9.0f, 9.0f, 6.0f, 6.0f);
        return path;
    }();
    return p;
}

const juce::Path& sun()
{
    static const auto p = []
    {
        auto path = fromSvg ({ "M12 2v2", "M12 20v2", "m4.93 4.93 1.41 1.41", "m17.66 17.66 1.41 1.41",
                               "M2 12h2", "M20 12h2", "m6.34 17.66-1.41 1.41", "m19.07 4.93-1.41 1.41" });
        path.addEllipse (8.0f, 8.0f, 8.0f, 8.0f);
        return path;
    }();
    return p;
}

const juce::Path& moon()
{
    static const auto p = fromSvg ({ "M20.985 12.486a9 9 0 1 1-9.473-9.472c.405-.022.617.46.402.803a6 6 0 0 0 8.268 8.268c.344-.215.825-.004.803.401" });
    return p;
}

void draw (juce::Graphics& g, const juce::Path& icon, juce::Rectangle<float> area, juce::Colour colour)
{
    const auto size  = juce::jmin (area.getWidth(), area.getHeight());
    const auto scale = size / 24.0f;
    const auto box   = juce::Rectangle<float> (size, size).withCentre (area.getCentre());

    g.setColour (colour);
    g.strokePath (icon,
                  juce::PathStrokeType (2.0f * scale, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                  juce::AffineTransform::scale (scale).translated (box.getX(), box.getY()));
}

} // namespace rapi::icons
