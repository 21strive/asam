// AsamCheck: offline checks for the synth and its interface. Not shipped.
//
//   AsamCheck <outputDir>
//
// 1. Measures the diode ladder: low-pass shape, where the resonance peak lands, stability.
// 2. Renders the sequencer through the full processor to WAV and reports level and DC.
// 3. Sweeps the Distortion knob and reports loudness, to tune the makeup gain.
// 4. Renders every preset and checks for clipping or silence.
// 5. Snapshots the editor to PNG in both themes.

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "dsp/DiodeLadder.h"

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

    void setParam (AsamProcessor& p, const char* id, float realValue)
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
    void snapshot (AsamProcessor& proc, bool dark, float scale, const juce::File& file)
    {
        proc.darkTheme.store (dark);
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

    void uiChecks (const juce::File& outDir)
    {
        std::cout << "\n[7] Interface snapshots\n";
        auto proc = makeProcessor();
        proc->setPlaying (true);
        render (*proc, 1.3);   // let the sequencer run so the playing step shows

        snapshot (*proc, true,  1.0f, outDir.getChildFile ("asam_ui_dark.png"));
        snapshot (*proc, true,  2.0f, outDir.getChildFile ("asam_ui_dark@2x.png"));
        snapshot (*proc, false, 1.0f, outDir.getChildFile ("asam_ui_light.png"));
        proc->darkTheme.store (true);
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
    uiChecks (outDir);

    std::cout << "\n" << (failures == 0 ? "All checks passed." : juce::String (failures) + " check(s) failed.") << "\n";
    return failures == 0 ? 0 : 1;
}
