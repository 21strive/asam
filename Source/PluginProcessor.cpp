#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "SoundPreset.h"

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

    /** A voice's Volume knob, in dB, to gain: the bottom of the range is silence. */
    float levelGain (float dB) noexcept
    {
        return dB <= -35.95f ? 0.0f : juce::Decibels::decibelsToGain (dB);
    }
}

//==============================================================================
AsamProcessor::AsamProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "AsamState", createParameterLayout())
{
    for (int v = 0; v < numVoices; ++v)
    {
        auto raw = [this, v] (const char* base) { return parameters.getRawParameterValue (pid::forVoice (v, base)); };
        auto& p = voiceParams[(size_t) v];

        p.cutoff     = raw (pid::cutoff);
        p.resonance  = raw (pid::resonance);
        p.envMod     = raw (pid::envMod);
        p.decay      = raw (pid::decay);
        p.release    = raw (pid::release);
        p.accent     = raw (pid::accent);
        p.waveform   = raw (pid::waveform);
        p.unison     = raw (pid::unison);
        p.detune     = raw (pid::detune);
        p.width      = raw (pid::width);
        p.level      = raw (pid::level);
        p.filterMode = raw (pid::filterMode);
        p.enabled    = raw (pid::enabled);
        p.drive      = raw (pid::drive);
        p.tone       = raw (pid::tone);
    }

    pVolume    = parameters.getRawParameterValue (pid::volume);
    pTempo     = parameters.getRawParameterValue (pid::tempo);
    pSwing     = parameters.getRawParameterValue (pid::swing);
    pHostSync  = parameters.getRawParameterValue (pid::hostSync);
    pKey       = parameters.getRawParameterValue (pid::key);
    pOctave    = parameters.getRawParameterValue (pid::octave);

    for (auto& s : steps)
        s.store (0);

    for (auto& id : soundParameterIds())
        soundRaw.push_back (parameters.getRawParameterValue (id));

    heldNotes.ensureStorageAllocated (128);
    loadPreset (0);
    takeSoundSnapshot ("Init", {});
}

double AsamProcessor::getTailLengthSeconds() const
{
    // The longest release among the voices that are on, plus the latency of the oversampler.
    float longest = 0.0f;

    for (const auto& p : voiceParams)
        if (p.enabled->load() > 0.5f)
            longest = std::max (longest, map::releaseSeconds (p.release->load()));

    return (double) longest + 0.1;
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
        2, (size_t) stages, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
    oversampler->initProcessing ((size_t) maxChunk);
    oversampler->reset();
    silence.setSize (2, maxChunk);
    silence.clear();
    scratch.setSize (2, maxChunk);

    for (auto& v : voices)
        v.prepare (osRate);

    for (auto& d : drivesL)
        d.prepare (osRate);

    for (auto& d : drivesR)
        d.prepare (osRate);

    auto initSmoother = [sampleRate] (juce::SmoothedValue<float>& s, float value, double seconds)
    {
        s.reset (sampleRate, seconds);
        s.setCurrentAndTargetValue (value);
    };

    for (int v = 0; v < numVoices; ++v)
    {
        const auto& p = voiceParams[(size_t) v];
        auto& sm = voiceSmoothing[(size_t) v];

        initSmoother (sm.cutoff,    p.cutoff->load(),    0.012);
        initSmoother (sm.resonance, p.resonance->load(), 0.020);
        initSmoother (sm.envMod,    p.envMod->load(),    0.020);
        initSmoother (sm.detune,    p.detune->load(),    0.050);
        initSmoother (sm.width,     p.width->load(),     0.050);
        initSmoother (sm.level,     levelGain (p.level->load()), 0.030);
        initSmoother (sm.drive,     p.drive->load(),     0.030);
        initSmoother (sm.tone,      p.tone->load(),      0.030);

        sm.active = p.enabled->load() > 0.5f;
        sm.onGain = sm.active ? 1.0f : 0.0f;

        drivesL[(size_t) v].setParams (sm.drive.getCurrentValue(), sm.tone.getCurrentValue());
        drivesR[(size_t) v] = drivesL[(size_t) v];
    }

    initSmoother (smVolume, pVolume->load(), 0.030);
    onGainCoef = asam::dsp::smoothingCoef (0.004f, (float) sampleRate);

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

    // Per voice: smoothed knobs follow their targets, the rest is read once a block.
    struct VoiceBlock
    {
        float decaySeconds, releaseSeconds, accent;
        int unison, filterMode;
        bool square, on;
    };

    std::array<VoiceBlock, numVoices> vb;

    for (int v = 0; v < numVoices; ++v)
    {
        const auto& p = voiceParams[(size_t) v];
        auto& sm = voiceSmoothing[(size_t) v];

        sm.cutoff.setTargetValue (p.cutoff->load());
        sm.resonance.setTargetValue (p.resonance->load());
        sm.envMod.setTargetValue (p.envMod->load());
        sm.detune.setTargetValue (p.detune->load());
        sm.width.setTargetValue (p.width->load());
        sm.level.setTargetValue (levelGain (p.level->load()));
        sm.drive.setTargetValue (p.drive->load());
        sm.tone.setTargetValue (p.tone->load());

        vb[(size_t) v] = { map::decaySeconds (p.decay->load()), map::releaseSeconds (p.release->load()), p.accent->load(),
                           map::unisonVoices (juce::roundToInt (p.unison->load())),
                           juce::jlimit (0, 3, juce::roundToInt (p.filterMode->load())),
                           p.waveform->load() > 0.5f, p.enabled->load() > 0.5f };
    }

    smVolume.setTargetValue (pVolume->load());

    const auto numOut = buffer.getNumChannels();
    auto* outL = buffer.getWritePointer (0);
    auto* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    float peak = 0.0f;

    auto midiIt  = midi.cbegin();
    auto midiEnd = midi.cend();

    for (int done = 0; done < numSamples;)
    {
        const auto n = std::min (maxChunk, numSamples - done);

        // Handle MIDI and the sequencer sample-accurately while rendering the oversampled voices.
        juce::dsp::AudioBlock<const float> zeros (silence.getArrayOfReadPointers(), 2, (size_t) n);
        auto osBlock = oversampler->processSamplesUp (zeros);
        auto* osL = osBlock.getChannelPointer (0);
        auto* osR = osBlock.getChannelPointer (1);

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

            for (int v = 0; v < numVoices; ++v)
            {
                auto& sm = voiceSmoothing[(size_t) v];
                const auto& b = vb[(size_t) v];

                if (b.on && ! sm.active)
                {
                    // Switched on: start from the knobs as they are now, not where they were, and
                    // in phase with a voice that is already sounding.
                    sm.active = true;

                    for (auto* s : { &sm.cutoff, &sm.resonance, &sm.envMod, &sm.detune, &sm.width, &sm.level, &sm.drive, &sm.tone })
                        s->setCurrentAndTargetValue (s->getTargetValue());

                    drivesL[(size_t) v].setParams (sm.drive.getCurrentValue(), sm.tone.getCurrentValue());

                    for (int other = 0; other < numVoices; ++other)
                    {
                        if (other != v && voiceSmoothing[(size_t) other].active)
                        {
                            voices[(size_t) v].followPhase (voices[(size_t) other]);
                            break;
                        }
                    }
                }

                if (! sm.active)
                    continue;

                sm.onGain += ((b.on ? 1.0f : 0.0f) - sm.onGain) * onGainCoef;

                if (! b.on && sm.onGain < 1.0e-4f)
                {
                    // Faded out: stop rendering, and start from rest when it comes back.
                    sm.onGain = 0.0f;
                    sm.active = false;
                    voices[(size_t) v].reset();
                    continue;
                }

                dsp::VoiceParams vp;
                const auto res = sm.resonance.getNextValue();
                vp.cutoffHz       = map::cutoffHz (sm.cutoff.getNextValue());
                vp.resonance01    = res;
                vp.resonanceK     = map::resonanceK (1.0f - (1.0f - res) * (1.0f - res));
                vp.svfQ           = map::svfQ (res);
                vp.envModOctaves  = map::envModOctaves (sm.envMod.getNextValue());
                vp.decaySeconds   = b.decaySeconds;
                vp.releaseSeconds = b.releaseSeconds;
                vp.accent01       = b.accent;
                vp.square         = b.square;
                vp.unison         = b.unison;
                vp.detuneCents    = map::detuneCents (sm.detune.getNextValue());
                vp.width          = sm.width.getNextValue();
                vp.filterMode     = b.filterMode;
                voices[(size_t) v].setParams (vp);

                // Level after the voice's own distortion, so turning a voice down never cleans it up.
                sm.gain = sm.level.getNextValue() * sm.onGain;

                if (sm.drive.isSmoothing() || sm.tone.isSmoothing() || (index & 31) == 0)
                {
                    const auto d = sm.drive.getNextValue(), t = sm.tone.getNextValue();
                    drivesL[(size_t) v].setParams (d, t);
                    drivesR[(size_t) v].setParams (d, t);
                }
                else
                {
                    sm.drive.skip (1);
                    sm.tone.skip (1);
                }
            }

            for (int j = 0; j < osFactor; ++j)
            {
                float mixL = 0.0f, mixR = 0.0f;

                for (int v = 0; v < numVoices; ++v)
                {
                    const auto& sm = voiceSmoothing[(size_t) v];

                    if (! sm.active)
                        continue;

                    // Voice, its distortion and tone, its level.
                    const auto s = voices[(size_t) v].render();
                    auto& dl = drivesL[(size_t) v];
                    auto& dr = drivesR[(size_t) v];
                    const auto l = dl.process (s.left);
                    float r;

                    if (s.stereo)
                    {
                        r = dr.process (s.right);
                    }
                    else
                    {
                        dr = dl;   // keep the right side's state in step
                        r = l;
                    }

                    mixL += l * sm.gain;
                    mixR += r * sm.gain;
                }

                osL[i * osFactor + j] = mixL;
                osR[i * osFactor + j] = mixR;
            }
        }

        juce::dsp::AudioBlock<float> chunk (scratch.getArrayOfWritePointers(), 2, 0, (size_t) n);
        oversampler->processSamplesDown (chunk);

        // Volume (the bottom of the range is silence), then a safety knee for the speakers:
        // untouched below -3 dBFS, approaching 0 dBFS smoothly.
        const auto* chunkL = scratch.getReadPointer (0);
        const auto* chunkR = scratch.getReadPointer (1);

        auto safe = [] (float y)
        {
            if (const auto a = std::abs (y); a > 0.7f)
                return std::copysign (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f), y);

            return y;
        };

        for (int i = 0; i < n; ++i)
        {
            const auto dB = smVolume.getNextValue();
            const auto gain = dB <= -35.95f ? 0.0f : juce::Decibels::decibelsToGain (dB);
            const auto l = safe (chunkL[i] * gain);
            const auto r = safe (chunkR[i] * gain);

            if (outR != nullptr)
            {
                outL[done + i] = l;
                outR[done + i] = r;
            }
            else
            {
                outL[done + i] = 0.5f * (l + r);
            }

            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }

        done += n;
    }

    // Any events left past the end of the block (shouldn't happen) still count.
    for (; midiIt != midiEnd; ++midiIt)
        handleMidi ((*midiIt).getMessage(), seqRunning);

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    auto previous = outputPeak.load();
    while (peak > previous && ! outputPeak.compare_exchange_weak (previous, peak)) {}

    for (int v = 0; v < numVoices; ++v)
    {
        const auto& sm = voiceSmoothing[(size_t) v];
        liveCutoff[(size_t) v].store (voices[(size_t) v].getLiveCutoff());
        liveLevel[(size_t) v].store (sm.active ? voices[(size_t) v].getLiveAmplitude() * sm.onGain : 0.0f);
    }

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
        const bool legato = ! heldNotes.isEmpty() && gateOpen;

        heldNotes.removeFirstMatchingValue (note);
        heldNotes.add (note);

        if (legato)
            voicesGlideTo (note, accent);
        else
            voicesNoteOn (note, accent);

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
            voicesNoteOff();
        else if (wasSounding)
            voicesGlideTo (heldNotes.getLast(), false);

        return;
    }

    if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        heldNotes.clearQuick();

        if (! sequencerOwnsVoice)
            voicesNoteOff();
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
        voicesNoteOff();
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
        voicesGlideTo (note, step.accent);
    else
        voicesNoteOn (note, step.accent);

    seqGateOpen       = true;
    seqHoldThrough    = step.slide;
    seqGateOffPending = ! step.slide;
}

void AsamProcessor::releaseSequencerNote()
{
    if (seqGateOpen)
        voicesNoteOff();

    seqGateOpen = seqHoldThrough = seqGateOffPending = false;
}

void AsamProcessor::voicesNoteOn (int note, bool accent) noexcept
{
    for (auto& v : voices)
        v.noteOn (note, accent);

    gateOpen = true;
}

void AsamProcessor::voicesGlideTo (int note, bool accent) noexcept
{
    // A glide with the gate closed starts a new note; AcidVoice does the same.
    for (auto& v : voices)
        v.glideTo (note, accent);

    gateOpen = true;
}

void AsamProcessor::voicesNoteOff() noexcept
{
    for (auto& v : voices)
        v.noteOff();

    gateOpen = false;
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
juce::Result AsamProcessor::loadSoundPreset (const juce::File& file)
{
    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return juce::Result::fail ("Could not read " + file.getFileName() + ".");

    const auto result = preset::apply (parameters, *xml);

    if (result.wasOk())
        takeSoundSnapshot (preset::nameOf (*xml, file), file);

    return result;
}

juce::Result AsamProcessor::saveSoundPreset (const juce::File& target)
{
    const auto file = target.hasFileExtension (preset::extension) ? target : target.withFileExtension (preset::extension);
    const auto name = file.getFileNameWithoutExtension();

    if (! file.getParentDirectory().createDirectory() || ! preset::toXml (parameters, name)->writeTo (file))
        return juce::Result::fail ("Could not write " + file.getFullPathName() + ".");

    takeSoundSnapshot (name, file);
    return juce::Result::ok();
}

void AsamProcessor::initSound()
{
    preset::applyDefaults (parameters);
    takeSoundSnapshot ("Init", {});
}

void AsamProcessor::takeSoundSnapshot (const juce::String& name, const juce::File& file)
{
    const juce::ScopedLock sl (metaLock);
    soundPresetName = name;
    soundPresetFile = file;
    soundSnapshot.clearQuick();

    for (auto* raw : soundRaw)
        soundSnapshot.add (raw->load());
}

juce::String AsamProcessor::getSoundPresetName() const
{
    const juce::ScopedLock sl (metaLock);
    return soundPresetName;
}

juce::File AsamProcessor::getSoundPresetFile() const
{
    const juce::ScopedLock sl (metaLock);
    return soundPresetFile;
}

bool AsamProcessor::isSoundEdited() const
{
    const juce::ScopedLock sl (metaLock);

    for (size_t i = 0; i < soundRaw.size(); ++i)
        if (std::abs (soundRaw[i]->load() - soundSnapshot[(int) i]) > 1.0e-4f)
            return true;

    return false;
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

    {
        // The sound preset in use, with the sound as it was when loaded or saved
        const juce::ScopedLock sl (metaLock);
        auto* sound = xml->createNewChildElement ("SoundPreset");
        sound->setAttribute ("name", soundPresetName);
        sound->setAttribute ("file", soundPresetFile.getFullPathName());

        for (int i = 0; i < soundSnapshot.size(); ++i)
        {
            auto* param = sound->createNewChildElement ("PARAM");
            param->setAttribute ("id", soundParameterIds()[i]);
            param->setAttribute ("value", (double) soundSnapshot[i]);
        }
    }

    xml->setAttribute ("theme", darkTheme.load() ? "dark" : "light");
    xml->setAttribute ("page", editorPage.load() == 1 ? "pattern" : "sound");
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

    std::unique_ptr<juce::XmlElement> soundPreset;

    if (auto* sound = xml->getChildByName ("SoundPreset"))
    {
        soundPreset = std::make_unique<juce::XmlElement> (*sound);
        xml->removeChildElement (sound, true);
    }

    darkTheme.store (xml->getStringAttribute ("theme", "dark") != "light");
    editorPage.store (xml->getStringAttribute ("page", "sound") == "pattern" ? 1 : 0);
    xml->removeAttribute ("theme");
    xml->removeAttribute ("page");
    parameters.replaceState (juce::ValueTree::fromXml (*xml));

    // The preset that was in use. A session from before presets keeps its sound as "Untitled".
    if (soundPreset == nullptr)
    {
        takeSoundSnapshot ("Untitled", {});
        return;
    }

    const juce::String path = soundPreset->getStringAttribute ("file");
    takeSoundSnapshot (soundPreset->getStringAttribute ("name", "Untitled"),
                       juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File());

    const juce::ScopedLock sl (metaLock);

    for (auto* param : soundPreset->getChildWithTagNameIterator ("PARAM"))
    {
        const auto index = soundParameterIds().indexOf (param->getStringAttribute ("id"));

        if (juce::isPositiveAndBelow (index, soundSnapshot.size()))
            soundSnapshot.set (index, (float) param->getDoubleAttribute ("value"));
    }
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
