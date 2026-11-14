// AsamCheck: offline checks for the synth and its interface. Not shipped.
//
//   AsamCheck <outputDir>
//
// 1. Measures the diode ladder: low-pass shape, where the resonance peak lands, stability.
// 2. Renders the sequencer through the full processor to WAV and reports level and DC.
// 3. Sweeps the Distortion knob and reports loudness, to tune the makeup gain.
// 4. Renders every preset and checks for clipping or silence.
// 5. Checks the three voices: switching, the filter modes, Release, old sessions.
// 6. Snapshots the editor to PNG, both pages in both themes.
// 7. Checks the editor fits any window shape without cropping an edge.
// 8. Switches a voice with the editor open and checks its panel follows.
// 9. Saves and loads sound presets: the file, sound only, defaults, edited, the session.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/DiodeLadder.h"
#include "dsp/FilterResponse.h"
#include "SoundPreset.h"

using namespace asam;

namespace
{
    int failures = 0;

    void check (bool ok, const juce::String& what)
    {
        std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n";
        if (! ok) ++failures;
    }

    double sineGain (dsp::DiodeLadder& f, double fs, double hz, float amplitude = 0.05f)
    {
        f.reset();
        const int warm = (int) (fs * 0.25), measure = (int) (fs * 0.25);
        double sumIn = 0.0, sumOut = 0.0;

        for (int n = 0; n < warm + measure; ++n)
        {
            const auto x = amplitude * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * n / fs);
            const auto y = f.process (x);

            if (n >= warm)
            {
                sumIn += (double) x * x;
                sumOut += (double) y * y;
            }
        }

        return std::sqrt (sumOut / juce::jmax (1.0e-20, sumIn));
    }

    void filterChecks()
    {
        std::cout << "\n[1] Diode ladder\n";
        const double fs = 192000.0;
        dsp::DiodeLadder f;
        f.prepare (fs);

        // Low-pass shape with no resonance
        f.setCutoff (1000.0f);
        f.setResonance (0.0f);
        const auto g200  = sineGain (f, fs, 200.0);
        const auto g1k   = sineGain (f, fs, 1000.0);
        const auto g8k   = sineGain (f, fs, 8000.0);
        std::cout << "  k=0, fc=1 kHz: 200 Hz " << juce::Decibels::gainToDecibels (g200) << " dB, 1 kHz "
                  << juce::Decibels::gainToDecibels (g1k) << " dB, 8 kHz " << juce::Decibels::gainToDecibels (g8k) << " dB\n";
        check (g200 > g1k && g1k > g8k && g8k < 0.001, "low-pass: falls with frequency, 8 kHz down > 60 dB");

        // Resonance: the peak should sit near the set cutoff
        for (auto fc : { 300.0f, 1000.0f, 3000.0f })
        {
            f.setCutoff (fc);
            f.setResonance (16.5f);
            double bestHz = 0.0, bestGain = 0.0;

            for (double hz = fc * 0.5; hz < fc * 2.0; hz *= 1.02)
            {
                const auto gain = sineGain (f, fs, hz, 0.01f);
                if (gain > bestGain) { bestGain = gain; bestHz = hz; }
            }

            const auto passGain = sineGain (f, fs, fc < 500.0f ? fc * 0.5 : fc * 0.25, 0.01f);
            std::cout << "  k=16.5, fc=" << fc << " Hz: peak at " << juce::roundToInt (bestHz) << " Hz, "
                      << juce::Decibels::gainToDecibels (bestGain / passGain) << " dB over the band below\n";
            check (std::abs (std::log2 (bestHz / fc)) < 0.2, "resonance peak within 0.2 octave of " + juce::String (fc) + " Hz");
            const auto minRatio = fc < 500.0f ? 1.26 : 4.0;   // the feedback high-pass softens low cutoffs
            check (bestGain / passGain > minRatio, "resonance peak stands > " + juce::String (juce::Decibels::gainToDecibels (minRatio), 0)
                                                    + " dB above the band below");
        }

        // Stability: hammer it with a loud saw at max resonance and extreme cutoffs.
        bool finite = true;
        float peak = 0.0f;

        for (auto fc : { 20.0f, 200.0f, 5000.0f, 40000.0f, 86000.0f })
        {
            f.reset();
            f.setCutoff (fc);
            f.setResonance (17.2f);
            double phase = 0.0;

            for (int n = 0; n < 192000; ++n)
            {
                phase += 55.0 / fs;
                if (phase >= 1.0) phase -= 1.0;
                const auto y = f.process ((float) (2.0 * phase - 1.0));
                finite = finite && std::isfinite (y);
                peak = std::max (peak, std::abs (y));
            }
        }

        std::cout << "  max |y| at k=17.2 over all cutoffs: " << peak << "\n";
        check (finite && peak < 4.0f, "stable and bounded at maximum resonance");

        // Self-oscillation must stay musical: no runaway when the input stops.
        f.reset();
        f.setCutoff (800.0f);
        f.setResonance (17.2f);
        for (int n = 0; n < 2000; ++n) f.process (n < 10 ? 1.0f : 0.0f);
        float tail = 0.0f;
        for (int n = 0; n < 192000; ++n) tail = std::max (tail, std::abs (f.process (0.0f)));
        std::cout << "  ring-out after an impulse, 1 s later: " << tail << "\n";
        check (tail < 0.5f, "rings but does not run away at maximum resonance");
    }

    //==============================================================================
    struct Render
    {
        juce::AudioBuffer<float> audio;
        double rms = 0.0, peak = 0.0, dc = 0.0;
        bool finite = true;
    };

    void setParam (AsamProcessor& p, const juce::String& id, float realValue)
    {
        auto* param = p.getState().getParameter (id);
        param->setValueNotifyingHost (param->convertTo0to1 (realValue));
    }

    Render render (AsamProcessor& proc, double seconds, double fs = 48000.0, int block = 480)
    {
        Render r;
        const auto total = (int) (seconds * fs);
        r.audio.setSize (2, total);
        r.audio.clear();

        juce::AudioBuffer<float> buffer (2, block);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total; pos += block)
        {
            const auto n = std::min (block, total - pos);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            view.clear();
            midi.clear();
            proc.processBlock (view, midi);

            for (int ch = 0; ch < 2; ++ch)
                r.audio.copyFrom (ch, pos, view, ch, 0, n);
        }

        double sum = 0.0, sumSq = 0.0;
        const auto* d = r.audio.getReadPointer (0);

        for (int i = 0; i < total; ++i)
        {
            r.finite = r.finite && std::isfinite (d[i]);
            sum += d[i];
            sumSq += (double) d[i] * d[i];
            r.peak = std::max (r.peak, (double) std::abs (d[i]));
        }

        r.rms = std::sqrt (sumSq / total);
        r.dc = sum / total;
        return r;
    }

    std::unique_ptr<AsamProcessor> makeProcessor (double fs = 48000.0, int block = 480)
    {
        auto proc = std::make_unique<AsamProcessor>();
        proc->setRateAndBufferSizeDetails (fs, block);
        proc->prepareToPlay (fs, block);
        setParam (*proc, pid::hostSync, 0.0f);   // no host here: run the internal clock
        return proc;
    }

    void writeWav (const juce::AudioBuffer<float>& audio, double fs, const juce::File& file)
    {
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());

        if (stream == nullptr)
            return;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions()
                                                       .withSampleRate (fs)
                                                       .withNumChannels (audio.getNumChannels())
                                                       .withBitsPerSample (24));
        if (writer != nullptr)
            writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    }

    void audioChecks (const juce::File& outDir)
    {
        std::cout << "\n[2] Full processor, default pattern\n";
        {
            auto proc = makeProcessor();
            proc->setPlaying (true);
            const auto r = render (*proc, 8.0);
            writeWav (r.audio, 48000.0, outDir.getChildFile ("asam_default.wav"));
            std::cout << "  rms " << juce::Decibels::gainToDecibels (r.rms) << " dBFS, peak "
                      << juce::Decibels::gainToDecibels (r.peak) << " dBFS, dc " << r.dc << "\n";
            check (r.finite, "no NaN or infinity");
            check (r.rms > 0.02, "audible");
            check (r.peak < 1.0, "no clipping at default settings");
            check (std::abs (r.dc) < 0.01, "no DC offset");

            const auto view = proc->getTransportView();
            check (view.running && view.step >= 0, "sequencer running and stepping");
        }

        std::cout << "\n[2b] Unison and width\n";
        {
            struct Image { double mid = 0.0, side = 0.0, midHigh = 0.0, sideHigh = 0.0, maxDiff = 0.0, rms = 0.0; bool finite = true; };

            auto measure = [] (int unisonIndex, float detune, float width)
            {
                auto proc = makeProcessor();
                setParam (*proc, pid::unison, (float) unisonIndex);
                setParam (*proc, pid::detune, detune);
                setParam (*proc, pid::width, width);
                proc->prepareToPlay (48000.0, 480);   // start from the settings, not a glide towards them
                proc->setPlaying (true);
                const auto r = render (*proc, 4.0);

                Image im;
                const auto* l = r.audio.getReadPointer (0);
                const auto* rr = r.audio.getReadPointer (1);
                double mid = 0.0, side = 0.0;

                for (int i = 0; i < r.audio.getNumSamples(); ++i)
                {
                    im.finite = im.finite && std::isfinite (l[i]) && std::isfinite (rr[i]);
                    mid  += 0.25 * (double) (l[i] + rr[i]) * (l[i] + rr[i]);
                    side += 0.25 * (double) (l[i] - rr[i]) * (l[i] - rr[i]);
                    im.maxDiff = std::max (im.maxDiff, (double) std::abs (l[i] - rr[i]));
                }

                const auto n = (double) r.audio.getNumSamples();
                im.mid = std::sqrt (mid / n);
                im.side = std::sqrt (side / n);

                // The same, above 400 Hz: where the squelch lives, away from the bass.
                juce::IIRFilter hpL, hpR;
                hpL.setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, 400.0));
                hpR.setCoefficients (juce::IIRCoefficients::makeHighPass (48000.0, 400.0));
                double midHigh = 0.0, sideHigh = 0.0;

                for (int i = 0; i < r.audio.getNumSamples(); ++i)
                {
                    const auto hl = hpL.processSingleSampleRaw (l[i]);
                    const auto hr = hpR.processSingleSampleRaw (rr[i]);
                    midHigh  += 0.25 * (double) (hl + hr) * (hl + hr);
                    sideHigh += 0.25 * (double) (hl - hr) * (hl - hr);
                }

                im.midHigh = std::sqrt (midHigh / n);
                im.sideHigh = std::sqrt (sideHigh / n);
                im.rms = r.rms;
                return std::make_pair (im, r);
            };

            const auto [one, oneRender] = measure (0, 0.45f, 0.8f);
            check (one.finite && one.maxDiff < 1.0e-6, "1 voice: left and right identical (classic mono line)");

            const auto [fiveMono, fiveMonoRender] = measure (2, 0.45f, 0.0f);
            check (fiveMono.maxDiff < 1.0e-6, "5 voices at 0 % width: still mono");

            for (int index : { 1, 2, 3 })
            {
                const auto [im, r] = measure (index, 0.45f, 1.0f);
                const auto voices = map::unisonVoices (index);
                const auto ratio = im.side / juce::jmax (1.0e-9, im.mid);
                const auto levelDelta = juce::Decibels::gainToDecibels (im.rms / one.rms);
                const auto ratioHigh = im.sideHigh / juce::jmax (1.0e-9, im.midHigh);
                check (im.finite && ratio > 0.25 && ratioHigh > 0.5,
                       juce::String (voices) + " voices, 100 % width: side/mid " + juce::String (ratio, 2)
                           + " overall, " + juce::String (ratioHigh, 2) + " above 400 Hz");
                check (std::abs (levelDelta) < 3.0,
                       juce::String (voices) + " voices: level " + (levelDelta >= 0 ? "+" : "") + juce::String (levelDelta, 1)
                           + " dB against 1 voice");

                if (index == 2)
                    writeWav (r.audio, 48000.0, outDir.getChildFile ("asam_unison5.wav"));
            }
        }

        std::cout << "\n[2c] Unison is consistent\n";
        {
            // Switch unison on at two different moments. Once both have played a few notes,
            // the audio must be the same: the result may not depend on when it was switched on.
            auto renderSwitchingOn = [] (double switchAt)
            {
                auto proc = makeProcessor();
                setParam (*proc, pid::unison, 0.0f);
                setParam (*proc, pid::width, 1.0f);
                proc->prepareToPlay (48000.0, 480);
                proc->setPlaying (true);

                juce::AudioBuffer<float> out (2, 48000 * 3);
                juce::AudioBuffer<float> block (2, 480);
                juce::MidiBuffer midi;
                bool switched = false;

                for (int pos = 0; pos < out.getNumSamples(); pos += 480)
                {
                    if (! switched && pos >= (int) (switchAt * 48000.0))
                    {
                        setParam (*proc, pid::unison, 2.0f);   // 5 voices
                        switched = true;
                    }

                    block.clear();
                    midi.clear();
                    proc->processBlock (block, midi);

                    for (int ch = 0; ch < 2; ++ch)
                        out.copyFrom (ch, pos, block, ch, 0, 480);
                }

                return out;
            };

            const auto a = renderSwitchingOn (0.53);
            const auto b = renderSwitchingOn (0.91);
            double maxDiff = 0.0, peak = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 48000 * 3 / 2; i < a.getNumSamples(); ++i)
                {
                    maxDiff = std::max (maxDiff, (double) std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
                    peak = std::max (peak, (double) std::abs (a.getSample (ch, i)));
                }

            std::cout << "  switched on at 0.53 s and 0.91 s, difference from 1.5 s on: "
                      << juce::Decibels::gainToDecibels (maxDiff / juce::jmax (1.0e-9, peak), -200.0) << " dB below the peak\n";
            check (maxDiff < 1.0e-4 * juce::jmax (1.0, peak), "same sound whenever unison is switched on");
        }

        std::cout << "\n[3] Distortion sweep (default pattern)\n";
        {
            for (int i = 0; i <= 8; ++i)
            {
                const auto d = (float) i / 8.0f;
                auto proc = makeProcessor();
                setParam (*proc, pid::drive, d);
                proc->setPlaying (true);
                const auto r = render (*proc, 4.0);
                std::cout << "  drive " << juce::String (d, 3) << ": rms " << juce::String (juce::Decibels::gainToDecibels (r.rms), 1)
                          << " dBFS, peak " << juce::String (juce::Decibels::gainToDecibels (r.peak), 1) << " dBFS\n";

                if (i == 8)
                    writeWav (r.audio, 48000.0, outDir.getChildFile ("asam_drive_full.wav"));
                if (i == 0)
                    writeWav (r.audio, 48000.0, outDir.getChildFile ("asam_drive_zero.wav"));
            }
        }

        std::cout << "\n[4] Every preset\n";
        {
            const auto& presets = patternPresets();

            for (int i = 0; i < (int) presets.size(); ++i)
            {
                auto proc = makeProcessor();
                proc->loadPreset (i);
                proc->setPlaying (true);
                const auto r = render (*proc, 3.7);
                const bool ok = r.finite && r.rms > 0.01 && r.peak < 1.0;
                check (ok, juce::String (i + 1).paddedLeft ('0', 2) + " " + presets[(size_t) i].name + ": rms "
                               + juce::String (juce::Decibels::gainToDecibels (r.rms), 1) + " dBFS, peak "
                               + juce::String (juce::Decibels::gainToDecibels (r.peak), 1) + " dBFS");
            }
        }

        std::cout << "\n[5] Extremes\n";
        {
            auto proc = makeProcessor (44100.0, 512);
            setParam (*proc, pid::resonance, 1.0f);
            setParam (*proc, pid::cutoff, 1.0f);
            setParam (*proc, pid::envMod, 1.0f);
            setParam (*proc, pid::accent, 1.0f);
            setParam (*proc, pid::drive, 1.0f);
            setParam (*proc, pid::volume, 6.0f);
            setParam (*proc, pid::tempo, 200.0f);
            proc->loadPreset (1);
            proc->setPlaying (true);
            const auto r = render (*proc, 4.0, 44100.0, 512);
            std::cout << "  everything at max: peak " << juce::Decibels::gainToDecibels (r.peak) << " dBFS\n";
            check (r.finite, "no NaN or infinity with every knob at maximum");
            check (r.peak <= 1.0, "output never passes 0 dBFS");

            // Other sample rates use other oversampling factors; the sound should hold.
            for (auto rate : { 44100.0, 96000.0, 192000.0 })
            {
                auto rp = makeProcessor (rate, 512);
                rp->setPlaying (true);
                const auto rr = render (*rp, 3.0, rate, 512);
                check (rr.finite && rr.rms > 0.05 && rr.peak <= 1.0,
                       juce::String (rate / 1000.0, 1) + " kHz: rms " + juce::String (juce::Decibels::gainToDecibels (rr.rms), 1) + " dBFS");
            }

            // Direct MIDI playing with the sequencer stopped
            auto midiProc = makeProcessor();
            juce::AudioBuffer<float> buffer (2, 480);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 110), 10);
            double energy = 0.0;

            for (int b = 0; b < 50; ++b)
            {
                buffer.clear();
                midiProc->processBlock (buffer, midi);
                midi.clear();
                energy += buffer.getRMSLevel (0, 0, 480);
            }

            check (energy > 0.1, "a MIDI note plays the voice when the sequencer is stopped");
        }

        std::cout << "\n[6] State round trip\n";
        {
            auto a = makeProcessor();
            a->loadPreset (4);
            a->setStep (3, { 7, true, true, true });
            setParam (*a, pid::cutoff, 0.77f);
            juce::MemoryBlock block;
            a->getStateInformation (block);

            auto b = makeProcessor();
            b->setStateInformation (block.getData(), (int) block.getSize());
            const auto step = b->getStep (3);
            check (b->getPattern() == a->getPattern(), "pattern restored");
            check (step.note == 7 && step.gate && step.accent && step.slide, "edited step restored");
            check (std::abs (b->getState().getRawParameterValue (pid::cutoff)->load() - 0.77f) < 1.0e-4f, "parameter restored");
            check (b->getPatternName() == a->getPatternName() && b->isPatternEdited(), "pattern name and edited flag restored");
        }
    }

    //==============================================================================
    float paramValue (AsamProcessor& p, const juce::String& id)
    {
        return p.getState().getRawParameterValue (id)->load();
    }

    /** Holds one MIDI note on voice 1's line (sequencer stopped) and returns the left channel. */
    juce::AudioBuffer<float> renderNote (AsamProcessor& proc, int note, double holdSeconds, double totalSeconds,
                                         double fs = 48000.0, int block = 480)
    {
        const auto total = (int) (totalSeconds * fs), offAt = (int) (holdSeconds * fs);
        juce::AudioBuffer<float> out (1, total), buffer (2, block);
        juce::MidiBuffer midi;

        for (int pos = 0; pos < total; pos += block)
        {
            const auto n = std::min (block, total - pos);
            juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
            view.clear();
            midi.clear();

            if (pos == 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 80), 0);

            if (offAt >= pos && offAt < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note), offAt - pos);

            proc.processBlock (view, midi);
            out.copyFrom (0, pos, view, 0, 0, n);
        }

        return out;
    }

    double rmsOf (const juce::AudioBuffer<float>& b, double fromSeconds, double toSeconds, double fs = 48000.0)
    {
        const auto from = juce::jlimit (0, b.getNumSamples(), (int) (fromSeconds * fs));
        const auto to   = juce::jlimit (from, b.getNumSamples(), (int) (toSeconds * fs));
        return to > from ? (double) b.getRMSLevel (0, from, to - from) : 0.0;
    }

    /** Share of the signal's energy below about 200 Hz: high for low-pass, low for high-pass. */
    double lowShare (const juce::AudioBuffer<float>& b, double fromSeconds, double toSeconds, double fs = 48000.0)
    {
        dsp::SvfLowpass lp;
        lp.setCutoff (200.0f, (float) fs);
        double low = 0.0, all = 0.0;

        for (int i = 0; i < (int) (toSeconds * fs) && i < b.getNumSamples(); ++i)
        {
            const auto x = b.getSample (0, i);
            const auto y = lp.process (x);

            if (i >= (int) (fromSeconds * fs))
            {
                low += (double) y * y;
                all += (double) x * x;
            }
        }

        return all > 0.0 ? low / all : 0.0;
    }

    void voiceChecks()
    {
        std::cout << "\n[7] Three voices\n";
        {
            auto proc = makeProcessor();
            check (paramValue (*proc, pid::forVoice (0, pid::enabled)) > 0.5f
                       && paramValue (*proc, pid::forVoice (1, pid::enabled)) < 0.5f
                       && paramValue (*proc, pid::forVoice (2, pid::enabled)) < 0.5f,
                   "only voice 1 is on at first");

            proc->setPlaying (true);
            const auto one = render (*proc, 3.0);

            auto withTwo = makeProcessor();
            setParam (*withTwo, pid::forVoice (1, pid::enabled), 1.0f);
            setParam (*withTwo, pid::drive, 0.0f);
            withTwo->setPlaying (true);
            const auto two = render (*withTwo, 3.0);

            auto oneClean = makeProcessor();
            setParam (*oneClean, pid::drive, 0.0f);
            oneClean->setPlaying (true);
            const auto clean = render (*oneClean, 3.0);

            std::cout << "  drive at 0: voice 1 alone rms " << juce::String (juce::Decibels::gainToDecibels (clean.rms), 1)
                      << " dBFS, voices 1 and 2 " << juce::String (juce::Decibels::gainToDecibels (two.rms), 1) << " dBFS\n";
            check (one.finite && two.finite && two.peak < 1.0, "voices 1 and 2 together: finite, under 0 dBFS");
            check (two.rms > clean.rms * 1.05, "switching voice 2 on adds to the line");

            auto silent = makeProcessor();
            setParam (*silent, pid::forVoice (0, pid::enabled), 0.0f);
            silent->setPlaying (true);
            const auto none = render (*silent, 1.0);
            double tailPeak = 0.0;

            for (int i = 24000; i < none.audio.getNumSamples(); ++i)   // after the fade and the tails
                tailPeak = std::max (tailPeak, (double) std::abs (none.audio.getSample (0, i)));

            check (none.finite && tailPeak < 1.0e-6, "every voice off: silence");

            auto quiet = makeProcessor();
            setParam (*quiet, pid::forVoice (0, pid::level), -36.0f);
            quiet->setPlaying (true);
            render (*quiet, 0.2);   // let the level glide down
            check (render (*quiet, 1.0).peak < 1.0e-6, "voice Volume at the bottom: silence");

            // Switching a voice on and off while the line plays never clicks: the step from one
            // sample to the next stays in the range the line itself uses.
            auto toggled = makeProcessor();
            setParam (*toggled, pid::drive, 0.0f);
            toggled->setPlaying (true);
            render (*toggled, 0.61);
            setParam (*toggled, pid::forVoice (1, pid::enabled), 1.0f);
            const auto on = render (*toggled, 0.3);
            setParam (*toggled, pid::forVoice (1, pid::enabled), 0.0f);
            const auto off = render (*toggled, 0.3);
            check (on.finite && off.finite, "switching a voice on and off mid-line stays finite");

            // Each voice has its own distortion: voice 2's knob moves the sound only while voice 2
            // plays, and voice 1's own knob still works on its own.
            auto withDrive = [] (bool voice2, float drive2, float drive1)
            {
                auto layered = makeProcessor();
                setParam (*layered, pid::forVoice (1, pid::enabled), voice2 ? 1.0f : 0.0f);
                setParam (*layered, pid::forVoice (1, pid::drive), drive2);
                setParam (*layered, pid::drive, drive1);
                layered->setPlaying (true);
                return render (*layered, 2.0);
            };

            auto diff = [] (const Render& a, const Render& b)
            {
                double d = 0.0;

                for (int i = 0; i < a.audio.getNumSamples(); ++i)
                    d = std::max (d, (double) std::abs (a.audio.getSample (0, i) - b.audio.getSample (0, i)));

                return d;
            };

            check (diff (withDrive (false, 0.0f, 0.62f), withDrive (false, 1.0f, 0.62f)) < 1.0e-6,
                   "voice 2's Distortion does nothing while voice 2 is off");
            check (diff (withDrive (true, 0.0f, 0.62f), withDrive (true, 1.0f, 0.62f)) > 0.01,
                   "voice 2's Distortion changes the sound while voice 2 plays");
            check (diff (withDrive (true, 0.5f, 0.0f), withDrive (true, 0.5f, 1.0f)) > 0.01,
                   "voice 1's Distortion works on its own");

            // The heaviest setting: every voice on with seven oscillators each.
            auto heavy = makeProcessor();

            for (int v = 0; v < numVoices; ++v)
            {
                setParam (*heavy, pid::forVoice (v, pid::enabled), 1.0f);
                setParam (*heavy, pid::forVoice (v, pid::unison), 3.0f);
                setParam (*heavy, pid::forVoice (v, pid::width), 1.0f);
            }

            heavy->setPlaying (true);
            const auto started = juce::Time::getMillisecondCounterHiRes();
            const auto loud = render (*heavy, 10.0);
            const auto seconds = (juce::Time::getMillisecondCounterHiRes() - started) / 1000.0;
            std::cout << "  three voices of seven oscillators: 10 s rendered in " << juce::String (seconds, 2) << " s ("
                      << juce::String (100.0 * seconds / 10.0, 1) << " % of one core), peak "
                      << juce::String (juce::Decibels::gainToDecibels (loud.peak), 1) << " dBFS\n";
            check (loud.finite && loud.peak <= 1.0 && seconds < 5.0, "three voices of seven oscillators: finite, under 0 dBFS, well inside real time");
        }

        std::cout << "\n[8] Filter modes\n";
        {
            const char* names[] = { "LP", "BP", "HP", "Notch" };
            double lpRms = 0.0;
            double shares[4] {};

            for (int mode = 0; mode < 4; ++mode)
            {
                // A held low note through a still filter: where the energy sits shows the mode.
                auto held = makeProcessor();
                setParam (*held, pid::filterMode, (float) mode);
                setParam (*held, pid::envMod, 0.0f);
                setParam (*held, pid::cutoff, 0.5f);       // 640 Hz
                setParam (*held, pid::resonance, 0.3f);
                setParam (*held, pid::drive, 0.0f);
                const auto note = renderNote (*held, 33, 1.0, 1.0);   // A1, 55 Hz
                shares[mode] = lowShare (note, 0.3, 1.0);

                // Level with the default pattern, before the distortion
                auto line = makeProcessor();
                setParam (*line, pid::filterMode, (float) mode);
                setParam (*line, pid::drive, 0.0f);
                line->setPlaying (true);
                const auto r = render (*line, 4.0);

                if (mode == 0)
                    lpRms = r.rms;

                const auto delta = juce::Decibels::gainToDecibels (r.rms / lpRms);
                std::cout << "  " << names[mode] << ": energy below 200 Hz " << juce::String (100.0 * shares[mode], 1)
                          << " %, default line rms " << juce::String (juce::Decibels::gainToDecibels (r.rms), 1)
                          << " dBFS (" << juce::String (delta, 1) << " dB from LP), peak "
                          << juce::String (juce::Decibels::gainToDecibels (r.peak), 1) << " dBFS\n";
                check (r.finite && r.peak < 1.0 && std::abs (delta) < 3.0,
                       juce::String (names[mode]) + ": level within 3 dB of the low-pass, no clipping");
            }

            check (shares[0] > 0.5 && shares[2] < 0.1 && shares[1] < shares[0],
                   "LP keeps the low end, HP removes it, BP sits between");

            // Every mode at full resonance and full env mod stays finite and under 0 dBFS.
            for (int mode = 1; mode < 4; ++mode)
            {
                auto hot = makeProcessor (44100.0, 512);
                setParam (*hot, pid::filterMode, (float) mode);
                setParam (*hot, pid::resonance, 1.0f);
                setParam (*hot, pid::envMod, 1.0f);
                setParam (*hot, pid::accent, 1.0f);
                setParam (*hot, pid::drive, 1.0f);
                hot->setPlaying (true);
                const auto r = render (*hot, 3.0, 44100.0, 512);
                check (r.finite && r.peak <= 1.0, juce::String (names[mode]) + " at full resonance: finite, under 0 dBFS");
            }

            // Changing the mode mid-note crossfades: nothing non-finite, no runaway level.
            auto switching = makeProcessor();
            setParam (*switching, pid::drive, 0.0f);
            switching->setPlaying (true);
            render (*switching, 0.5);
            double worst = 0.0;

            for (int mode : { 2, 1, 0, 3, 2, 0 })
            {
                setParam (*switching, pid::filterMode, (float) mode);
                const auto r = render (*switching, 0.13);
                worst = std::max (worst, r.peak);
                check (r.finite, juce::String ("switch to ") + names[mode] + " mid-line: finite");
            }

            check (worst < 1.0, "mode switches mid-line stay under 0 dBFS");

            // The graph draws what the filter does: its curve has the peak on the cutoff, low-pass
            // keeps the lows, high-pass removes them, notch digs out the cutoff.
            const auto res  = 0.9f;
            const auto k    = map::resonanceK (1.0f - (1.0f - res) * (1.0f - res));
            const auto q    = map::svfQ (res);
            const auto fc   = 1000.0f;
            auto mag = [&] (int mode, float hz) { return dsp::filterMagnitude (mode, fc, k, q, hz); };

            float peakHz = 0.0f, peakMag = 0.0f;

            for (float hz = 300.0f; hz < 3000.0f; hz *= 1.005f)
                if (mag (0, hz) > peakMag) { peakMag = mag (0, hz); peakHz = hz; }

            std::cout << "  graph curve, ladder at 90 % resonance: peak at " << juce::String (peakHz, 0) << " Hz for a 1 kHz cutoff\n";
            check (std::abs (peakHz / fc - 1.0f) < 0.03f, "the drawn low-pass peaks on its cutoff");
            check (mag (0, 30.0f) > 10.0f * mag (0, 8000.0f), "the drawn low-pass keeps the lows");
            check (mag (2, 8000.0f) > 10.0f * mag (2, 50.0f), "the drawn high-pass removes the lows");
            check (mag (3, fc) < 0.05f * mag (3, 100.0f), "the drawn notch digs out the cutoff");

            // The drawn ladder peak matches the measured one (the filter check at the top).
            dsp::DiodeLadder ladder;
            ladder.prepare (192000.0);
            ladder.setCutoff (fc);
            ladder.setResonance (k);
            double bestHz = 0.0, bestGain = 0.0;

            for (double hz = 500.0; hz < 2000.0; hz *= 1.01)
            {
                const auto gain = sineGain (ladder, 192000.0, hz, 0.01f);
                if (gain > bestGain) { bestGain = gain; bestHz = hz; }
            }

            check (std::abs (bestHz / peakHz - 1.0) < 0.05, "the drawn peak lands where the ladder really peaks ("
                                                                + juce::String (bestHz, 0) + " Hz measured)");
        }

        std::cout << "\n[9] Release and old sessions\n";
        {
            auto tailFor = [] (float release)
            {
                auto proc = makeProcessor();
                setParam (*proc, pid::release, release);
                setParam (*proc, pid::drive, 0.0f);
                const auto note = renderNote (*proc, 45, 0.3, 1.2);
                return rmsOf (note, 0.4, 0.7) / juce::jmax (1.0e-9, rmsOf (note, 0.1, 0.3));
            };

            const auto shortTail = tailFor (0.0f), longTail = tailFor (0.7f);
            std::cout << "  level 0.1 to 0.4 s after the note ends, against the held note: Release 0 "
                      << juce::String (juce::Decibels::gainToDecibels (shortTail, -200.0), 1) << " dB, Release 0.7 "
                      << juce::String (juce::Decibels::gainToDecibels (longTail, -200.0), 1) << " dB\n";
            check (shortTail < 1.0e-3 && longTail > 0.1, "Release: the bottom is the snappy VCA, higher settings ring on");

            // A session saved before there were three voices: its knobs land on voice 1, and the
            // new voices stay off, so it sounds as it did.
            auto old = makeProcessor();
            auto xml = std::make_unique<juce::XmlElement> ("AsamState");

            for (auto [paramId, value] : { std::pair<const char*, float> { pid::cutoff, 0.61f }, { pid::resonance, 0.4f },
                                           { pid::decay, 0.8f }, { pid::waveform, 1.0f }, { pid::drive, 0.3f } })
            {
                auto* child = xml->createNewChildElement ("PARAM");
                child->setAttribute ("id", paramId);
                child->setAttribute ("value", value);
            }

            juce::MemoryBlock block;
            juce::AudioProcessor::copyXmlToBinary (*xml, block);
            old->setStateInformation (block.getData(), (int) block.getSize());

            check (std::abs (paramValue (*old, pid::cutoff) - 0.61f) < 1.0e-4f && paramValue (*old, pid::waveform) > 0.5f
                       && std::abs (paramValue (*old, pid::drive) - 0.3f) < 1.0e-4f,
                   "old session: its knobs, the distortion too, land on voice 1");
            check (paramValue (*old, pid::forVoice (0, pid::enabled)) > 0.5f && paramValue (*old, pid::forVoice (1, pid::enabled)) < 0.5f
                       && paramValue (*old, pid::forVoice (2, pid::enabled)) < 0.5f,
                   "old session: voice 1 on, voices 2 and 3 off");
            check (paramValue (*old, pid::release) < 1.0e-6f && std::abs (paramValue (*old, pid::level)) < 1.0e-4f
                       && paramValue (*old, pid::filterMode) < 0.5f,
                   "old session: snappy release, unity level, low-pass, as before");

            // Per-voice settings and the page survive a round trip.
            auto a = makeProcessor();
            setParam (*a, pid::forVoice (1, pid::enabled), 1.0f);
            setParam (*a, pid::forVoice (2, pid::filterMode), 2.0f);
            setParam (*a, pid::forVoice (2, pid::release), 0.42f);
            a->editorPage.store (1);
            juce::MemoryBlock saved;
            a->getStateInformation (saved);

            auto b = makeProcessor();
            b->setStateInformation (saved.getData(), (int) saved.getSize());
            check (paramValue (*b, pid::forVoice (1, pid::enabled)) > 0.5f && std::abs (paramValue (*b, pid::forVoice (2, pid::filterMode)) - 2.0f) < 1.0e-4f
                       && std::abs (paramValue (*b, pid::forVoice (2, pid::release)) - 0.42f) < 1.0e-4f,
                   "per-voice settings restored");
            check (b->editorPage.load() == 1, "the open page is restored");
        }
    }

    //==============================================================================
    void snapshot (AsamProcessor& proc, bool dark, int page, float scale, const juce::File& file)
    {
        proc.darkTheme.store (dark);
        proc.editorPage.store (page);
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
        auto* asamEditor = dynamic_cast<AsamEditor*> (editor.get());

        if (asamEditor == nullptr)
        {
            check (false, "editor created");
            return;
        }

        asamEditor->refreshNow();
        auto* view = asamEditor->getView();
        const auto image = view->createComponentSnapshot (view->getLocalBounds(), true, scale);

        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        check (out.openedOk() && png.writeImageToStream (image, out), "snapshot " + file.getFileName());
        proc.editorBeingDeleted (editor.get());
        editor = nullptr;
    }

    /** The window or the host may hand the editor any shape: the whole view has to stay inside
        it, filling one side, with no edge cropped. */
    void fitChecks (AsamProcessor& proc)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
        auto* asamEditor = dynamic_cast<AsamEditor*> (editor.get());

        if (asamEditor == nullptr)
        {
            check (false, "editor created");
            return;
        }

        auto* view = asamEditor->getView();

        // 1246 x 803 is what a 3:2 window frame leaves once a 28 px title bar is taken out.
        for (const auto size : { juce::Point<int> (1200, 800), juce::Point<int> (1246, 803), juce::Point<int> (1246, 900),
                                 juce::Point<int> (1600, 820) })
        {
            editor->setSize (size.x, size.y);
            const auto shown = editor->getLocalArea (view, view->getLocalBounds().toFloat());
            const auto inside = editor->getLocalBounds().toFloat().expanded (0.01f).contains (shown);
            const auto filled = std::abs (shown.getWidth() - (float) size.x) < 0.5f || std::abs (shown.getHeight() - (float) size.y) < 0.5f;
            check (inside && filled && shown.getY() == 0.0f,
                   "view fits " + juce::String (size.x) + " x " + juce::String (size.y) + " without cropping");
        }

        proc.editorBeingDeleted (editor.get());
    }

    juce::Component* findByTitle (juce::Component& root, const juce::String& title)
    {
        if (root.getTitle() == title)
            return &root;

        for (auto* child : root.getChildren())
            if (auto* found = findByTitle (*child, title))
                return found;

        return nullptr;
    }

    rapi::Knob* findKnob (juce::Component& root, const juce::String& label)
    {
        for (auto* child : root.getChildren())
            if (auto* k = dynamic_cast<rapi::Knob*> (child); k != nullptr && k->getLabel() == label)
                return k;

        return nullptr;
    }

    /** A voice is switched from its panel, the Voices list or the host, always through the
        parameter, with the editor open. Its panel must follow the new value at once: knobs
        alive when it is on, greyed when it is off. */
    void voicePanelChecks (AsamProcessor& proc)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditorAndMakeActive());
        auto* asamEditor = dynamic_cast<AsamEditor*> (editor.get());
        auto* view   = asamEditor != nullptr ? asamEditor->getView() : nullptr;
        auto* panel  = view != nullptr ? findByTitle (*view, "Voice 2") : nullptr;
        auto* filter = panel != nullptr ? findKnob (*panel, "Decay") : nullptr;

        if (filter == nullptr)
        {
            check (false, "voice 2 panel and its Decay knob found");
            return;
        }

        const auto id = pid::forVoice (1, pid::enabled);
        setParam (proc, id, 0.0f);
        const bool offGreyed = ! filter->isEnabled();
        setParam (proc, id, 1.0f);
        const bool onAlive = filter->isEnabled();
        setParam (proc, id, 0.0f);
        const bool offAgain = ! filter->isEnabled();

        std::cout << "  voice 2 Decay knob: off " << (offGreyed ? "greyed" : "ALIVE") << ", on " << (onAlive ? "alive" : "GREYED")
                  << ", off again " << (offAgain ? "greyed" : "ALIVE") << "\n";
        check (offGreyed && onAlive && offAgain, "switching voice 2 on wakes its knobs at once, switching it off greys them");

        // The filter graph: a click at the 1 kHz line puts the cutoff at 1 kHz, and dragging up
        // raises the resonance. Voice 1, which is on.
        auto* panel1 = findByTitle (*view, "Voice 1");
        auto* graph  = panel1 != nullptr ? findByTitle (*panel1, "Voice 1 filter") : nullptr;

        if (graph == nullptr)
        {
            check (false, "voice 1 filter graph found");
            proc.editorBeingDeleted (editor.get());
            return;
        }

        const auto plot = graph->getLocalBounds().toFloat().reduced (1.0f);
        const auto xAt1k = plot.getX() + plot.getWidth() * std::log (1000.0f / 20.0f) / std::log (1000.0f);
        auto mouse = juce::Desktop::getInstance().getMainMouseSource();
        const auto now = juce::Time::getCurrentTime();

        auto event = [&] (juce::Point<float> pos, juce::Point<float> down)
        {
            return juce::MouseEvent (mouse, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                     graph, graph, now, down, now, 1, false);
        };

        const auto resBefore = paramValue (proc, pid::resonance);
        const juce::Point<float> at1k (xAt1k, plot.getCentreY());
        graph->mouseDown (event (at1k, at1k));
        const auto clickedHz = map::cutoffHz (paramValue (proc, pid::cutoff));
        graph->mouseDrag (event (at1k.translated (0.0f, -20.0f), at1k));
        graph->mouseUp (event (at1k.translated (0.0f, -20.0f), at1k));
        const auto resAfter = paramValue (proc, pid::resonance);

        std::cout << "  click at the 1k line: cutoff " << juce::String (clickedHz, 1) << " Hz; drag up 20 px: resonance "
                  << juce::String (100.0f * resBefore, 1) << " % to " << juce::String (100.0f * resAfter, 1) << " %\n";
        check (std::abs (clickedHz - 1000.0f) < 15.0f, "a click at the graph's 1 kHz line sets the cutoff to 1 kHz");
        check (resAfter > resBefore + 0.05f || resBefore > 0.99f, "dragging up in the graph raises the resonance");
        proc.editorBeingDeleted (editor.get());
    }

    //==============================================================================
    void presetChecks()
    {
        std::cout << "\n[13] Sound presets\n";

        auto near = [] (float a, float b) { return std::abs (a - b) < 1.0e-4f; };
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AsamCheck presets");
        dir.deleteRecursively();
        dir.createDirectory();
        const auto file = dir.getChildFile ("Rubber Squelch.asam");

        auto a = makeProcessor();
        check (a->getSoundPresetName() == "Init" && ! a->isSoundEdited(), "a fresh Asam starts on Init, unedited");

        setParam (*a, pid::forVoice (1, pid::enabled), 1.0f);
        setParam (*a, pid::forVoice (2, pid::filterMode), 3.0f);
        setParam (*a, pid::cutoff, 0.66f);
        setParam (*a, pid::volume, -12.0f);
        check (a->isSoundEdited(), "turning a knob marks the sound edited");

        const auto saved = a->saveSoundPreset (dir.getChildFile ("Rubber Squelch"));   // the extension is added
        check (saved.wasOk() && file.existsAsFile(), "Save writes Rubber Squelch.asam");
        check (a->getSoundPresetName() == "Rubber Squelch" && ! a->isSoundEdited(), "after saving: named after the file, unedited");

        // The file: plain XML, the format version, every sound parameter and nothing else
        const auto xml = juce::XmlDocument::parse (file);
        auto* sound = xml != nullptr ? xml->getChildByName ("Sound") : nullptr;
        int count = 0;
        bool onlySound = true;

        if (sound != nullptr)
        {
            for (auto* param : sound->getChildWithTagNameIterator ("PARAM"))
            {
                ++count;
                onlySound = onlySound && soundParameterIds().contains (param->getStringAttribute ("id"));
            }
        }

        check (xml != nullptr && xml->hasTagName ("AsamPreset") && xml->getIntAttribute ("format") == 1
                   && count == soundParameterIds().size() && onlySound,
               "the file is XML, format 1, with the " + juce::String (count) + " sound parameters and nothing else");

        // Into another Asam with its own pattern, tempo and key: the sound arrives, the rest stays.
        auto b = makeProcessor();
        b->loadPreset (6);
        setParam (*b, pid::tempo, 142.0f);
        setParam (*b, pid::key, 5.0f);
        const auto pattern = b->getPattern();
        const auto loaded = b->loadSoundPreset (file);

        check (loaded.wasOk() && b->getSoundPresetName() == "Rubber Squelch" && ! b->isSoundEdited(), "Load: named, unedited");
        check (paramValue (*b, pid::forVoice (1, pid::enabled)) > 0.5f && near (paramValue (*b, pid::forVoice (2, pid::filterMode)), 3.0f)
                   && near (paramValue (*b, pid::cutoff), 0.66f) && near (paramValue (*b, pid::volume), -12.0f),
               "Load brings back the voices and the master volume");
        check (b->getPattern() == pattern && near (paramValue (*b, pid::tempo), 142.0f) && near (paramValue (*b, pid::key), 5.0f),
               "Load leaves the pattern, the tempo and the key alone (sound only)");

        setParam (*b, pid::cutoff, 0.3f);
        const auto movedAway = b->isSoundEdited();
        setParam (*b, pid::cutoff, 0.66f);
        check (movedAway && ! b->isSoundEdited(), "Edited follows the knobs: back where the preset had them, it clears");

        // A preset that names only part of the sound: the rest goes back to the defaults.
        const auto partial = dir.getChildFile ("Partial.asam");
        partial.replaceWithText ("<AsamPreset name=\"Partial\" format=\"1\"><Sound><PARAM id=\"cutoff\" value=\"0.2\"/></Sound></AsamPreset>");
        check (b->loadSoundPreset (partial).wasOk() && near (paramValue (*b, pid::cutoff), 0.2f)
                   && paramValue (*b, pid::forVoice (1, pid::enabled)) < 0.5f && near (paramValue (*b, pid::volume), -6.0f),
               "a partial preset: what it names, defaults for the rest");

        // Not a preset, or from a newer Asam: refused, nothing changes.
        const auto stranger = dir.getChildFile ("Stranger.asam"), newer = dir.getChildFile ("Newer.asam");
        stranger.replaceWithText ("<Something/>");
        newer.replaceWithText ("<AsamPreset format=\"9\"><Sound><PARAM id=\"cutoff\" value=\"0.9\"/></Sound></AsamPreset>");
        check (b->loadSoundPreset (stranger).failed() && b->loadSoundPreset (newer).failed() && near (paramValue (*b, pid::cutoff), 0.2f)
                   && b->getSoundPresetName() == "Partial",
               "a file that is not a preset, or one from a newer Asam, is refused and changes nothing");

        b->initSound();
        check (b->getSoundPresetName() == "Init" && ! b->isSoundEdited() && near (paramValue (*b, pid::cutoff), 0.40f),
               "Init sound: every sound knob at its default");

        // The preset in use, and whether it was edited, survive the session.
        setParam (*a, pid::resonance, 0.2f);
        juce::MemoryBlock block;
        a->getStateInformation (block);
        auto c = makeProcessor();
        c->setStateInformation (block.getData(), (int) block.getSize());
        check (c->getSoundPresetName() == "Rubber Squelch" && c->getSoundPresetFile() == file && c->isSoundEdited(),
               "a saved session remembers the preset and that it was edited");

        const auto folder = preset::folder().getFullPathName();
        std::cout << "  presets folder: " << folder << "\n";
        check (folder.endsWith ("Library/Audio/Presets/21 Strive/Asam"), "presets live in ~/Library/Audio/Presets/21 Strive/Asam");

        dir.deleteRecursively();
    }

    void uiChecks (const juce::File& outDir)
    {
        std::cout << "\n[10] Interface snapshots\n";
        auto proc = makeProcessor();
        proc->setPlaying (true);
        render (*proc, 1.3);   // let the sequencer run so the playing step shows

        // A little more audio before each one, so the meters show the line rather than silence.
        auto shoot = [&] (bool dark, int page, float scale, const char* name)
        {
            render (*proc, 0.1);
            snapshot (*proc, dark, page, scale, outDir.getChildFile (name));
        };

        shoot (true,  0, 1.0f, "asam_ui_sound_dark.png");
        shoot (true,  0, 2.0f, "asam_ui_sound_dark@2x.png");
        shoot (true,  1, 1.0f, "asam_ui_pattern_dark.png");
        shoot (false, 0, 1.0f, "asam_ui_sound_light.png");
        shoot (false, 1, 1.0f, "asam_ui_pattern_light.png");

        // Voice 2 on, voice 3 off: how the stack reads once a second voice joins
        setParam (*proc, pid::forVoice (1, pid::enabled), 1.0f);
        render (*proc, 0.2);
        snapshot (*proc, true,  0, 1.0f, outDir.getChildFile ("asam_ui_sound_two_voices_dark.png"));
        setParam (*proc, pid::forVoice (1, pid::enabled), 0.0f);
        proc->darkTheme.store (true);
        proc->editorPage.store (0);

        std::cout << "\n[11] Editor sizes\n";
        fitChecks (*proc);

        std::cout << "\n[12] Switching voices with the editor open\n";
        voicePanelChecks (*proc);
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    const auto outDir = argc > 1 ? juce::File (juce::String (argv[1]))
                                 : juce::File::getCurrentWorkingDirectory();
    outDir.createDirectory();

    filterChecks();
    audioChecks (outDir);
    voiceChecks();
    presetChecks();
    uiChecks (outDir);

    std::cout << "\n" << (failures == 0 ? "All checks passed." : juce::String (failures) + " check(s) failed.") << "\n";
    return failures == 0 ? 0 : 1;
}
