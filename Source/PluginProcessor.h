#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "Params.h"
#include "dsp/AcidDrive.h"
#include "dsp/AcidVoice.h"
#include "seq/Pattern.h"

#include <atomic>
#include <functional>

//==============================================================================
class AsamProcessor final : public juce::AudioProcessor
{
public:
    AsamProcessor();
    ~AsamProcessor() override = default;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                         { return true; }

    const juce::String getName() const override             { return "Asam"; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    bool isMidiEffect() const override                      { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==============================================================================
    // Pattern editing, called from the message thread.
    asam::Step getStep (int index) const noexcept;
    void setStep (int index, asam::Step step);
    asam::Pattern getPattern() const noexcept;
    void setPattern (const asam::Pattern& pattern, const juce::String& name, int presetIndex);
    void loadPreset (int presetIndex);
    void randomisePattern();
    void clearPattern();

    juce::String getPatternName() const;
    int getPresetIndex() const;
    bool isPatternEdited() const;

    /** Bumped whenever the pattern changes, so the editor knows to refresh. */
    std::uint32_t getPatternVersion() const noexcept        { return patternVersion.load(); }

    //==============================================================================
    // Sound presets: the three voices and the master volume as a .asam file (SoundPreset.h).
    // Called from the message thread. Loading never touches the pattern, the clock or the key.
    juce::Result loadSoundPreset (const juce::File& file);
    juce::Result saveSoundPreset (const juce::File& file);
    void initSound();

    juce::String getSoundPresetName() const;
    juce::File getSoundPresetFile() const;

    /** True once the sound differs from the preset last loaded, saved or initialised. */
    bool isSoundEdited() const;

    //==============================================================================
    // Transport
    void setPlaying (bool shouldPlay) noexcept              { playArmed.store (shouldPlay); }
    bool isPlayArmed() const noexcept                       { return playArmed.load(); }
    void stopAndRewind() noexcept                           { playArmed.store (false); rewindRequested.store (true); }

    struct TransportView
    {
        bool  running        = false;   // notes are being produced
        bool  hostClock      = false;   // following the host transport
        bool  hostAvailable  = false;   // running inside a DAW
        int   step           = -1;      // 0..15, -1 when stopped
        int   bar            = 0;
        float bpm            = 130.0f;
        int   transpose      = 0;
    };

    TransportView getTransportView() const noexcept;

    /** Peak since the last call, linear. */
    float takeOutputPeak() noexcept                         { return outputPeak.exchange (0.0f); }

    /** Where a voice's filter is right now (envelope and accent included), and how loud the voice
        is, 0 when silent or off: for the filter display. */
    float getLiveCutoff (int voice) const noexcept          { return liveCutoff[(size_t) voice].load(); }
    float getLiveLevel (int voice) const noexcept           { return liveLevel[(size_t) voice].load(); }

    bool isStandalone() const noexcept                      { return wrapperType == wrapperType_Standalone; }

    juce::AudioProcessorValueTreeState& getState() noexcept { return parameters; }

    /** Keys and on-screen notes are fed through here into the MIDI stream. */
    juce::MidiKeyboardState keyboardState;

    /** Set by the standalone app: opens the audio and MIDI device settings. */
    std::function<void()> showAudioSettings;

    /** Interface theme, saved with the state: true is dark. */
    std::atomic<bool> darkTheme { true };

    /** The editor's page, saved with the state: 0 is Sound (the voices), 1 is Pattern. */
    std::atomic<int> editorPage { 0 };

private:
    //==============================================================================
    void handleMidi (const juce::MidiMessage& m, bool sequencerOwnsVoice);
    void sequencerTick (double pos16);
    void triggerStep (long long absoluteStep);
    void releaseSequencerNote();
    void resetSequencer();
    int rootNote() const noexcept;
    void bumpPattern();

    // Every note goes to all three voices, sounding or not, so one switched on mid-line
    // picks up the next note in step.
    void takeSoundSnapshot (const juce::String& name, const juce::File& file);

    void voicesNoteOn (int note, bool accent) noexcept;
    void voicesGlideTo (int note, bool accent) noexcept;
    void voicesNoteOff() noexcept;

    juce::AudioProcessorValueTreeState parameters;

    struct VoiceParamPointers
    {
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* resonance = nullptr;
        std::atomic<float>* envMod = nullptr;
        std::atomic<float>* decay = nullptr;
        std::atomic<float>* release = nullptr;
        std::atomic<float>* accent = nullptr;
        std::atomic<float>* waveform = nullptr;
        std::atomic<float>* unison = nullptr;
        std::atomic<float>* detune = nullptr;
        std::atomic<float>* width = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* filterMode = nullptr;
        std::atomic<float>* enabled = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* tone = nullptr;
    };

    std::array<VoiceParamPointers, asam::numVoices> voiceParams;

    std::atomic<float>* pVolume    = nullptr;
    std::atomic<float>* pTempo     = nullptr;
    std::atomic<float>* pSwing     = nullptr;
    std::atomic<float>* pHostSync  = nullptr;
    std::atomic<float>* pKey       = nullptr;
    std::atomic<float>* pOctave    = nullptr;

    // DSP runs oversampled so the saw, the ladder and the clipper stay clean:
    // 4x up to 48 kHz, 2x up to 96 kHz, none above that (the voices always run near 192 kHz).
    static constexpr int maxChunk = 256;
    int osFactor = 4;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    juce::AudioBuffer<float> silence, scratch;
    std::array<asam::dsp::AcidVoice, asam::numVoices> voices;

    // Each voice has its own distortion and tone after its filter, then its level.
    std::array<asam::dsp::AcidDrive, asam::numVoices> drivesL, drivesR;

    struct VoiceSmoothing
    {
        juce::SmoothedValue<float> cutoff, resonance, envMod, detune, width, level, drive, tone;
        float onGain = 0.0f;    // fades the voice in and out when it is switched
        float gain = 0.0f;      // level times onGain for the sample being rendered
        bool  active = false;   // rendering: switched on, or still fading out
    };

    std::array<VoiceSmoothing, asam::numVoices> voiceSmoothing;
    juce::SmoothedValue<float> smVolume;
    float onGainCoef = 0.01f;
    bool gateOpen = false;
    double baseRate = 44100.0;

    // Pattern, shared lock-free with the audio thread
    std::array<std::atomic<std::uint32_t>, asam::numSteps> steps;
    std::atomic<std::uint32_t> patternVersion { 1 };
    mutable juce::CriticalSection metaLock;
    juce::String patternName;
    int presetIndex = 0;
    bool patternEdited = false;

    // The sound preset in use, and the sound as it was loaded or saved, to tell when it is edited.
    juce::String soundPresetName { "Init" };
    juce::File soundPresetFile;
    juce::Array<float> soundSnapshot;                  // in soundParameterIds() order
    std::vector<std::atomic<float>*> soundRaw;         // the same order
    juce::Random random;

    // Sequencer state (audio thread)
    std::atomic<bool> playArmed { false };
    std::atomic<bool> rewindRequested { false };
    bool seqRunning = false;
    bool lastWasHostClock = false;
    double internalPos16 = 0.0;
    long long lastAbsStep = std::numeric_limits<long long>::min();
    bool seqGateOpen = false, seqHoldThrough = false, seqGateOffPending = false;
    int transpose = 0;

    // Direct MIDI play when the sequencer is stopped
    juce::Array<int> heldNotes;

    // Shared with the editor
    std::atomic<bool>  viewRunning { false }, viewHostClock { false }, viewHostAvailable { false };
    std::atomic<int>   viewStep { -1 }, viewBar { 0 }, viewTranspose { 0 };
    std::atomic<float> viewBpm { 130.0f };
    std::atomic<float> outputPeak { 0.0f };
    std::array<std::atomic<float>, asam::numVoices> liveCutoff {}, liveLevel {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AsamProcessor)
};
