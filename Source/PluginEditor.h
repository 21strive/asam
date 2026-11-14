#pragma once

#include "PluginProcessor.h"
#include "ui/RapiLookAndFeel.h"
#include "ui/RapiWidgets.h"
#include "ui/StepGrid.h"

/** One Rapi look and feel per plug-in binary, also installed as the default so stock
    windows (the standalone audio settings) follow it. */
struct SharedRapiLookAndFeel
{
    SharedRapiLookAndFeel()     { juce::LookAndFeel::setDefaultLookAndFeel (&lnf); }
    ~SharedRapiLookAndFeel()    { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }

    rapi::LookAndFeel lnf;
};

//==============================================================================
class AsamEditor final : public juce::AudioProcessorEditor,
                         private juce::Timer
{
public:
    explicit AsamEditor (AsamProcessor&);
    ~AsamEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    bool keyStateChanged (bool isKeyDown) override;
    void focusLost (FocusChangeType) override;

    static constexpr int logicalWidth  = 1200;
    static constexpr int logicalHeight = 800;

    /** For the offline snapshot tool. */
    juce::Component* getView() const noexcept;

    /** Pushes the processor state into the view now, instead of on the next timer tick. */
    void refreshNow();

    /** Opens a page: 0 is Sound (the voices), 1 is Pattern. */
    void showPage (int page);

private:
    class MainView;

    void timerCallback() override;
    void releaseAllKeys();

    AsamProcessor& asamProcessor;
    juce::SharedResourcePointer<rapi::Typefaces> typefaces;
    juce::SharedResourcePointer<SharedRapiLookAndFeel> sharedLookAndFeel;
    std::unique_ptr<MainView> view;
    juce::TooltipWindow tooltips { this, 700 };

    // Playing from the computer keyboard: A W S E D F T G Y H U J K is one octave, Z and X shift it.
    std::array<bool, 13> keyIsDown {};
    std::array<int, 13> keyNote {};
    int keyboardOctave = 0;
    bool grabbedInitialFocus = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AsamEditor)
};
