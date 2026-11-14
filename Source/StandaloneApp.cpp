// The standalone application. Compiled into the Standalone target only.
//
// Compared with JUCE's stock standalone window this uses the native macOS title bar,
// opens every MIDI input automatically so a keyboard plays straight away, and reaches
// the audio settings from the settings button inside the Rapi interface.

#include "PluginProcessor.h"
#include "ui/RapiTheme.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

namespace
{

/** JUCE runs a top-level window's constrainer on its native frame, title bar included, so a
    3:2 ratio there leaves the editor one title bar short and cuts off its bottom edge. This
    one holds the size limits and the ratio on the content area instead. */
class ContentConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    explicit ContentConstrainer (juce::Component& windowIn) : window (windowIn) {}

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previousBounds,
                      const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft,
                      bool isStretchingBottom, bool isStretchingRight) override
    {
        const auto frame = frameSize();
        auto content = frame.subtractedFrom (bounds);
        ComponentBoundsConstrainer::checkBounds (content, frame.subtractedFrom (previousBounds), frame.subtractedFrom (limits),
                                                 isStretchingTop, isStretchingLeft, isStretchingBottom, isStretchingRight);
        bounds = frame.addedTo (content);
    }

private:
    juce::BorderSize<int> frameSize() const
    {
        if (auto* peer = window.getPeer())
            if (const auto frame = peer->getFrameSizeIfPresent())
                return *frame;

        return {};
    }

    juce::Component& window;
};

//==============================================================================
class AsamWindow final : public juce::DocumentWindow
{
public:
    AsamWindow (const juce::String& title, std::unique_ptr<juce::StandalonePluginHolder> holderIn, juce::PropertySet* propsIn)
        : DocumentWindow (title, rapi::colours().bgCanvas, DocumentWindow::minimiseButton | DocumentWindow::closeButton),
          holder (std::move (holderIn)),
          props (propsIn)
    {
        setUsingNativeTitleBar (true);

        if (auto* asam = dynamic_cast<AsamProcessor*> (holder->processor.get()))
            asam->showAudioSettings = [this] { holder->showAudioSettingsDialog(); };

        if (auto* editor = holder->processor->createEditorAndMakeActive())
        {
            setContentOwned (editor, true);

            constrainer.setFixedAspectRatio ((double) editor->getWidth() / (double) editor->getHeight());
            constrainer.setSizeLimits (editor->getWidth() * 7 / 10, editor->getHeight() * 7 / 10,
                                       editor->getWidth() * 3 / 2, editor->getHeight() * 3 / 2);
            setConstrainer (&constrainer);
            setResizable (true, false);
        }

        restorePosition();
        setVisible (true);
    }

    ~AsamWindow() override
    {
        if (props != nullptr)
        {
            props->setValue ("windowX", getX());
            props->setValue ("windowY", getY());
            props->setValue ("windowW", getWidth());
        }

        holder->stopPlaying();

        // The wrapper must tell the processor before its editor goes away.
        if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
            holder->processor->editorBeingDeleted (editor);

        clearContentComponent();
        holder = nullptr;
    }

    void closeButtonPressed() override
    {
        holder->savePluginState();
        juce::JUCEApplicationBase::quit();
    }

    std::unique_ptr<juce::StandalonePluginHolder> holder;

private:
    void restorePosition()
    {
        const auto natural = getBounds();

        if (props != nullptr && props->containsKey ("windowX"))
        {
            const auto w = juce::jlimit (constrainer.getMinimumWidth(), constrainer.getMaximumWidth(),
                                         props->getIntValue ("windowW", natural.getWidth()));
            const auto h = juce::roundToInt ((double) w * (double) natural.getHeight() / (double) natural.getWidth());
            setBoundsConstrained ({ props->getIntValue ("windowX"), props->getIntValue ("windowY"), w, h });
        }
        else
        {
            centreWithSize (natural.getWidth(), natural.getHeight());
        }
    }

    juce::PropertySet* props = nullptr;
    ContentConstrainer constrainer { *this };
};

//==============================================================================
class AsamStandaloneApp final : public juce::JUCEApplication
{
public:
    AsamStandaloneApp()
    {
        juce::PropertiesFile::Options options;
        options.applicationName     = "Asam";
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support";
        options.folderName          = "";
        appProperties.setStorageParameters (options);
    }

    const juce::String getApplicationName() override        { return "Asam"; }
    const juce::String getApplicationVersion() override     { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }
    void anotherInstanceStarted (const juce::String&) override {}

    void initialise (const juce::String&) override
    {
        auto* settings = appProperties.getUserSettings();

        auto holder = std::make_unique<juce::StandalonePluginHolder> (settings, false, juce::String(), nullptr,
                                                                      juce::Array<juce::StandalonePluginHolder::PluginInOuts>(),
                                                                      true);
        window = std::make_unique<AsamWindow> ("Asam", std::move (holder), settings);
    }

    void shutdown() override
    {
        window = nullptr;
        appProperties.saveIfNeeded();
    }

    void systemRequestedQuit() override
    {
        if (window != nullptr)
            window->holder->savePluginState();

        if (juce::ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            juce::Timer::callAfterDelay (100, []
            {
                if (auto* app = juce::JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

private:
    juce::ApplicationProperties appProperties;
    std::unique_ptr<AsamWindow> window;
};

} // namespace

juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication()
{
    return new AsamStandaloneApp();
}
