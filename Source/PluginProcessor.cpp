#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace asam;

namespace
{
    constexpr float gateLength = 0.5f;          // fraction of a step a plain note is held
    constexpr int   transposeRange = 24;
    constexpr int   baseRootNote = 36;          // C2

    long long floorDiv (long long a, long long b) noexcept
    {
        const auto q = a / b;
        return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
    }
}

//==============================================================================
AsamProcessor::AsamProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AsamState", createParameterLayout())
{
    pCutoff    = parameters.getRawParameterValue (pid::cutoff);
    pResonance = parameters.getRawParameterValue (pid::resonance);
    pEnvMod    = parameters.getRawParameterValue (pid::envMod);
    pDecay     = parameters.getRawParameterValue (pid::decay);
    pAccent    = parameters.getRawParameterValue (pid::accent);
    pDrive     = parameters.getRawParameterValue (pid::drive);
    pTone      = parameters.getRawParameterValue (pid::tone);
    pVolume    = parameters.getRawParameterValue (pid::volume);
    pWaveform  = parameters.getRawParameterValue (pid::waveform);
    pTempo     = parameters.getRawParameterValue (pid::tempo);
    pSwing     = parameters.getRawParameterValue (pid::swing);
    pHostSync  = parameters.getRawParameterValue (pid::hostSync);
    pKey       = parameters.getRawParameterValue (pid::key);
    pOctave    = parameters.getRawParameterValue (pid::octave);

    for (auto& s : steps)
        s.store (0);

    heldNotes.ensureStorageAllocated (128);
    loadPreset (0);
}

bool AsamProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================
void AsamProcessor::prepareToPlay (double sampleRate, int)
{
    baseRate = sampleRate;

    const auto stages = sampleRate <= 50000.0 ? 2 : (sampleRate <= 100000.0 ? 1 : 0);
    osFactor = 1 << stages;
    const auto osRate = sampleRate * osFactor;

    oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
        1, (size_t) stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxChunk);
    oversampler->reset();
    silence.setSize (1, maxChunk);
    silence.clear();

    voice.prepare (osRate);
    drive.prepare (osRate);

    auto initSmoother = [sampleRate] (juce::SmoothedValue<float>& s, float value, double seconds)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };

    initSmoother (smCutoff,    pCutoff->load(),    0.012);
    initSmoother (smResonance, pResonance->load(), 0.020);
    initSmoother (smEnvMod,    pEnvMod->load(),    0.020);
    initSmoother (smDrive,     pDrive->load(),     0.030);
    initSmoother (smTone,      pTone->load(),      0.030);
    initSmoother (smVolume,    pVolume->load(),    0.030);

    drive.setParams (smDrive.getCurrentValue(), smTone.getCurrentValue());

    setLatencySamples (juce::roundToInt (oversampler->getLatencyInSamples()));
    resetSequencer();
}

void AsamProcessor::resetSequencer()
{
    releaseSequencerNote();
    seqRunning  = false;
    lastAbsStep = std::numeric_limits<long long>::min();
    transpose   = 0;
    viewStep.store (-1);
    viewRunning.store (false);
    viewTranspose.store (0);
}

int AsamProcessor::rootNote() const noexcept
{
    const auto key    = juce::jlimit (0, 11, (int) pKey->load());
    const auto octave = juce::jlimit (-1, 2, juce::roundToInt (pOctave->load()));
    return baseRootNote + key + 12 * octave;
}

//==============================================================================
void AsamProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const auto numSamples = buffer.getNumSamples();

    if (oversampler == nullptr)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    // Clock source: the host transport when synced inside a DAW, otherwise our own.
    juce::Optional<juce::AudioPlayHead::PositionInfo> position;

    if (auto* playHead = getPlayHead())
        position = playHead->getPosition();

    const bool syncWanted = ! isStandalone() && pHostSync->load() > 0.5f;
    bool hostPlaying = false;

    if (syncWanted && position.hasValue())
        hostPlaying = position->getIsPlaying() && position->getPpqPosition().hasValue();

    if (rewindRequested.exchange (false))
    {
        resetSequencer();
        internalPos16 = 0.0;
    }

    if (syncWanted != lastWasHostClock)
    {
        resetSequencer();
        lastWasHostClock = syncWanted;
    }

    const bool shouldRun = playArmed.load() && (syncWanted ? hostPlaying : true);

    if (shouldRun && ! seqRunning)
    {
        seqRunning  = true;
        lastAbsStep = std::numeric_limits<long long>::min();
        heldNotes.clearQuick();
    }
    else if (! shouldRun && seqRunning)
    {
        resetSequencer();
    }

    float bpm = pTempo->load();

    if (syncWanted && hostPlaying)
        if (auto hostBpm = position->getBpm())
            bpm = (float) *hostBpm;

    viewBpm.store (bpm);
    viewHostClock.store (syncWanted);
    viewHostAvailable.store (! isStandalone());
    viewRunning.store (seqRunning);

    smCutoff.setTargetValue (pCutoff->load());
    smResonance.setTargetValue (pResonance->load());
    smEnvMod.setTargetValue (pEnvMod->load());
    smDrive.setTargetValue (pDrive->load());
    smTone.setTargetValue (pTone->load());
    smVolume.setTargetValue (pVolume->load());

    auto* out = buffer.getWritePointer (0);
    auto midiIt  = midi.cbegin();
    auto midiEnd = midi.cend();

    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (maxChunk, numSamples - done);

        // Handle MIDI and the sequencer sample-accurately while rendering the oversampled voice.
        juce::dsp::AudioBlock<const float> zeros (silence.getArrayOfReadPointers(), 1, (size_t) n);
        auto osBlock = oversampler->processSamplesUp (zeros);
        auto* os = osBlock.getChannelPointer (0);

        const double pos16PerSample = (double) bpm / 60.0 * 4.0 / baseRate;
        const double hostPpq0 = (syncWanted && hostPlaying) ? *position->getPpqPosition() : 0.0;

        for (int i = 0; i < n; ++i)
        {
            const auto index = done + i;

            while (midiIt != midiEnd && (*midiIt).samplePosition <= index)
            {
                handleMidi ((*midiIt).getMessage(), seqRunning);
                ++midiIt;
            }

            if (seqRunning)
            {
                if (syncWanted)
                {
                    sequencerTick (hostPpq0 * 4.0 + (double) index * pos16PerSample);
                }
                else
                {
                    sequencerTick (internalPos16);
                    internalPos16 += pos16PerSample;
                }
            }

            dsp::VoiceParams vp;
            const auto res = smResonance.getNextValue();
            vp.cutoffHz      = map::cutoffHz (smCutoff.getNextValue());
            vp.resonance01   = res;
            vp.resonanceK    = map::resonanceK (1.0f - (1.0f - res) * (1.0f - res));
            vp.envModOctaves = map::envModOctaves (smEnvMod.getNextValue());
            vp.decaySeconds  = map::decaySeconds (pDecay->load());
            vp.accent01      = pAccent->load();
            vp.square        = pWaveform->load() > 0.5f;
            voice.setParams (vp);

            if (smDrive.isSmoothing() || smTone.isSmoothing() || (index & 31) == 0)
                drive.setParams (smDrive.getNextValue(), smTone.getNextValue());
            else
            {
                smDrive.skip (1);
                smTone.skip (1);
            }

            for (int j = 0; j < osFactor; ++j)
                os[i * osFactor + j] = drive.process (voice.render());
        }

        juce::dsp::AudioBlock<float> outBlock (&out, 1, (size_t) done, (size_t) n);
        oversampler->processSamplesDown (outBlock);

        done += n;
    }

    // Any events left past the end of the block (shouldn't happen) still count.
    for (; midiIt != midiEnd; ++midiIt)
        handleMidi ((*midiIt).getMessage(), seqRunning);

    // Volume, the bottom of the range is silence.
    float peak = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto dB = smVolume.getNextValue();
        const auto gain = dB <= -35.95f ? 0.0f : juce::Decibels::decibelsToGain (dB);
        auto y = out[i] * gain;

        // Safety knee for the speakers: untouched below -3 dBFS, approaches 0 dBFS smoothly.
        if (const auto a = std::abs (y); a > 0.7f)
            y = std::copysign (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f), y);

        out[i] = y;
        peak = std::max (peak, std::abs (y));
    }

    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, buffer, 0, 0, numSamples);

    auto previous = outputPeak.load();
    while (peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}

    midi.clear();
}

//==============================================================================
void AsamProcessor::handleMidi (const juce::MidiMessage& m, bool sequencerOwnsVoice)
{
    if (m.isNoteOn())
    {
        const auto note = m.getNoteNumber();

        if (sequencerOwnsVoice)
        {
            // While the pattern runs, keys transpose it, relative to C3 (MIDI 60).
            transpose = juce::jlimit (-transposeRange, transposeRange, note - 60);
            viewTranspose.store (transpose);
            return;
        }

        const bool accent = m.getVelocity() >= 100;
        const bool legato = ! heldNotes.isEmpty() && voice.isGateOpen();

        heldNotes.removeFirstMatchingValue (note);
        heldNotes.add (note);

        if (legato)
            voice.glideTo (note, accent);
        else
            voice.noteOn (note, accent);

        return;
    }

    if (m.isNoteOff())
    {
        const auto note = m.getNoteNumber();
        const bool wasSounding = ! heldNotes.isEmpty() && heldNotes.getLast() == note;
        heldNotes.removeFirstMatchingValue (note);

        if (sequencerOwnsVoice)
            return;

        if (heldNotes.isEmpty())
            voice.noteOff();
        else if (wasSounding)
            voice.glideTo (heldNotes.getLast(), false);

        return;
    }

    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clearQuick();

        if (! sequencerOwnsVoice)
            voice.noteOff();
    }
}

void AsamProcessor::sequencerTick (double pos16)
{
    const auto swing   = juce::jlimit (0.5, 0.75, (double) pSwing->load() / 100.0);
    const auto pairPos = pos16 * 0.5;
    const auto pair    = std::floor (pairPos);
    const auto f       = pairPos - pair;

    long long absStep;
    double phase;

    if (f < swing)
    {
        absStep = (long long) pair * 2;
        phase   = f / swing;
    }
    else
    {
        absStep = (long long) pair * 2 + 1;
        phase   = (f - swing) / (1.0 - swing);
    }

    if (absStep != lastAbsStep)
    {
        lastAbsStep = absStep;
        triggerStep (absStep);
    }
    else if (seqGateOffPending && phase >= gateLength)
    {
        voice.noteOff();
        seqGateOpen = false;
        seqGateOffPending = false;
    }
}

void AsamProcessor::triggerStep (long long absoluteStep)
{
    const auto index = (int) (absoluteStep - floorDiv (absoluteStep, numSteps) * numSteps);
    const auto step  = Step::unpack (steps[(size_t) index].load (std::memory_order_relaxed));

    viewStep.store (index);
    viewBar.store ((int) floorDiv (absoluteStep, numSteps) + 1);

    if (! step.gate)
    {
        releaseSequencerNote();
        return;
    }

    const auto note = juce::jlimit (0, 127, rootNote() + step.note + transpose);

    if (seqGateOpen && seqHoldThrough)
        voice.glideTo (note, step.accent);
    else
        voice.noteOn (note, step.accent);

    seqGateOpen       = true;
    seqHoldThrough    = step.slide;
    seqGateOffPending = ! step.slide;
}

void AsamProcessor::releaseSequencerNote()
{
    if (seqGateOpen)
        voice.noteOff();

    seqGateOpen = seqHoldThrough = seqGateOffPending = false;
}

AsamProcessor::TransportView AsamProcessor::getTransportView() const noexcept
{
    TransportView v;
    v.running       = viewRunning.load();
    v.hostClock     = viewHostClock.load();
    v.hostAvailable = ! isStandalone();
    v.step          = viewStep.load();
    v.bar           = viewBar.load();
    v.bpm           = viewBpm.load();
    v.transpose     = viewTranspose.load();
    return v;
}

//==============================================================================
Step AsamProcessor::getStep (int index) const noexcept
{
    return Step::unpack (steps[(size_t) juce::jlimit (0, numSteps - 1, index)].load());
}

Pattern AsamProcessor::getPattern() const noexcept
{
    Pattern p;

    for (int i = 0; i < numSteps; ++i)
        p[(size_t) i] = getStep (i);

    return p;
}

void AsamProcessor::setStep (int index, Step step)
{
    if (! juce::isPositiveAndBelow (index, numSteps))
        return;

    steps[(size_t) index].store (step.pack());

    {
        const juce::ScopedLock sl (metaLock);
        patternEdited = true;
    }

    bumpPattern();
}

void AsamProcessor::setPattern (const Pattern& pattern, const juce::String& name, int index)
{
    for (int i = 0; i < numSteps; ++i)
        steps[(size_t) i].store (pattern[(size_t) i].pack());

    {
        const juce::ScopedLock sl (metaLock);
        patternName   = name;
        presetIndex   = index;
        patternEdited = false;
    }

    bumpPattern();
}

void AsamProcessor::loadPreset (int index)
{
    const auto& presets = patternPresets();

    if (juce::isPositiveAndBelow (index, (int) presets.size()))
        setPattern (presets[(size_t) index].steps, presets[(size_t) index].name, index);
}

void AsamProcessor::randomisePattern()
{
    setPattern (randomPattern (random), "Random", -1);
}

void AsamProcessor::clearPattern()
{
    setPattern (Pattern {}, "Empty", -1);
}

juce::String AsamProcessor::getPatternName() const
{
    const juce::ScopedLock sl (metaLock);
    return patternName;
}

int AsamProcessor::getPresetIndex() const
{
    const juce::ScopedLock sl (metaLock);
    return presetIndex;
}

bool AsamProcessor::isPatternEdited() const
{
    const juce::ScopedLock sl (metaLock);
    return patternEdited;
}

void AsamProcessor::bumpPattern()
{
    patternVersion.fetch_add (1);
}

//==============================================================================
void AsamProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());

    if (xml == nullptr)
        return;

    auto* pattern = xml->createNewChildElement ("Pattern");
    pattern->setAttribute ("steps", patternToString (getPattern()));

    {
        const juce::ScopedLock sl (metaLock);
        pattern->setAttribute ("name", patternName);
        pattern->setAttribute ("preset", presetIndex);
        pattern->setAttribute ("edited", patternEdited);
    }

    xml->setAttribute ("theme", darkTheme.load() ? "dark" : "light");
    copyXmlToBinary (*xml, destData);
}

void AsamProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));

    if (xml == nullptr || ! xml->hasTagName (parameters.state.getType()))
        return;

    if (auto* pattern = xml->getChildByName ("Pattern"))
    {
        setPattern (parsePattern (pattern->getStringAttribute ("steps")),
                    pattern->getStringAttribute ("name", "Untitled"),
                    pattern->getIntAttribute ("preset", -1));

        {
            const juce::ScopedLock sl (metaLock);
            patternEdited = pattern->getBoolAttribute ("edited", false);
        }

        xml->removeChildElement (pattern, true);
    }

    darkTheme.store (xml->getStringAttribute ("theme", "dark") != "light");
    xml->removeAttribute ("theme");
    parameters.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessorEditor* AsamProcessor::createEditor()
{
    return new AsamEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AsamProcessor();
}
