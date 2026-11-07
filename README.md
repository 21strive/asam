<h1>
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset=".github/assets/asam-logo-white.svg">
    <img src=".github/assets/asam-logo-black.svg" alt="asam" height="64">
  </picture>
</h1>

> *Asam* (Indonesian): sour, and the word for acid. Here, a distorted acid line you can play without opening a DAW.

Asam is a monophonic acid bass synthesizer with a live step sequencer, by 21 Strive. It builds as a **VST3**, an
**Audio Unit** and a **standalone app** for macOS on JUCE 9, and its interface is drawn in
[Rapi](https://github.com/21strive/rapi), the 21 Strive design language.

The voice is the classic bassline machine (a saw or a square into a diode ladder filter, with accent and slide), pushed
through a distortion stage the way distorted acid techno does it. Two knobs do most of the work: **Filter** opens and
closes the low-pass, **Distortion** sets how hard it clips. The sequencer plays inside one octave, because acid lines
rarely need more.

- Plays on its own: pick a pattern, set the tempo, press Space, and play the computer keyboard on top.
- 16 original patterns, a random pattern maker, and a grid to draw your own: 16 steps, 13 notes, accent and slide.
- Inside a DAW it follows the host tempo and transport. MIDI notes play the voice while the sequencer is stopped and
  transpose the pattern while it runs.
- The oscillator, the filter and the clipper run oversampled (4× at 44.1 and 48 kHz), so resonance and distortion stay
  clean.
- Every knob can be automated, and the pattern is saved with the session.
- The interface follows Rapi: semantic tokens, Geist type, the RKnob geometry, the layout of Rapi's step sequencer
  sample, light and dark themes.

In Rapi's terms Asam earns its screen twice over: a filter is tuned faster by hand than in words, and a rhythm is drawn
on a grid faster than it could ever be described.

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="docs/asam-dark.png">
  <img src="docs/asam-light.png" alt="Asam: the voice knobs, the one-octave step grid, and the clock, pattern bank and transport console">
</picture>

## Build (macOS)

Requires Xcode or the Command Line Tools, and CMake 3.22 or newer. JUCE is a submodule.

```bash
git clone --recursive https://github.com/21strive/asam.git
cd asam
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # without Ninja, leave out -G Ninja
cmake --build build --target Asam_VST3 Asam_AU Asam_Standalone
```

If the clone was not recursive, fetch JUCE with `git submodule update --init`. For one binary that runs on Apple Silicon
and Intel, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` when configuring.

| Format     | Built at                                           | Copied to                              |
|------------|----------------------------------------------------|----------------------------------------|
| VST3       | `build/Asam_artefacts/Release/VST3/Asam.vst3`      | `~/Library/Audio/Plug-Ins/VST3`        |
| AU         | `build/Asam_artefacts/Release/AU/Asam.component`   | `~/Library/Audio/Plug-Ins/Components`  |
| Standalone | `build/Asam_artefacts/Release/Standalone/Asam.app` | —                                      |

## What Asam decides

| Layer | Decision |
|---|---|
| Oscillator | Saw or square, band-limited with PolyBLEP |
| Filter | Four-pole diode ladder (RC-ladder topology), solved per sample with zero-delay feedback; the resonance loop runs through a 150 Hz high-pass so the low end survives, and a tanh stage saturates it |
| Envelopes | Filter decay from 200 ms to 2.5 s; a fast VCA with a short release |
| Accent | Louder, the shortest decay, and a sweep capacitor that pushes the cutoff; consecutive accents stack |
| Slide | Fixed-time glide (about 40 ms) with the gate held through the step |
| Distortion | Input high-pass and high-frequency emphasis, an asymmetric tanh stage into a hard-knee clipper, a tone low-pass and makeup gain; from just under unity to +48 dB |
| Oversampling | 4× up to 48 kHz, 2× up to 96 kHz, none above; the voice always runs near 192 kHz |
| Sequencer | 16 steps, sample-accurate, gate at half a step, swing 50–75 %, its own clock at 60–200 BPM or the host's |
| Patterns | One octave: 13 notes, every step a note or a rest, with accent and slide |
| Output | The mono voice on both channels; a soft knee keeps it under 0 dBFS |
| Interface | Rapi 0.2: semantic roles only, square controls, 1 px rules, Geist, Geist Mono and one Geist Pixel readout |
| Formats | VST3, AU and a standalone app on JUCE 9.0.5 |

## Playing it

### Knobs

| Knob | What it does |
|---|---|
| **Filter** | Low-pass cutoff, 40 Hz to 10.2 kHz. The figure is where the resonance peak sits. |
| Resonance | Feedback, from smooth to a squelch on the edge of oscillation. |
| Env mod | How far the envelope opens the filter on every note, up to 4.5 octaves. |
| Decay | Length of the filter sweep. |
| Accent | How much accented steps jump out. |
| **Distortion** | How hard the line is driven, from clean to full fuzz. The level is compensated, so the knob changes character more than loudness. |
| Tone | Low-pass after the distortion, 1.2 to 12 kHz, from dark to screaming. |
| Volume | Output level. |
| Saw / Square | Oscillator waveform. |

### Sequencer

- Click a cell to put a step on that note, click it again for a rest, drag to draw. The root is the bottom row and its
  octave the top row.
- **Accent** and **Slide** have their own rows. Slide on a step holds the gate and glides into the next step.
- The **pattern bank** holds 16 patterns (Warehouse, Squelch, Rave alarm, Rubber, Phrygian, …). **Random** writes a new
  line in minor pentatonic or phrygian; **Clear** empties the grid.
- **Key** and **Octave** move the whole pattern. **Tempo** and **Swing** sit on the clock.
- **Transport**: play or pause, stop and rewind to step 1, previous and next pattern.

### Keys in the standalone app

| Key | Action |
|---|---|
| `Space` | Play or pause |
| `←` `→` | Previous or next pattern |
| `A W S E D F T G Y H U J K` | One octave of notes, C to C; while the pattern runs they transpose it |
| `Shift` + a note | An accented note |
| `Z` `X` | Keyboard octave down or up |

The settings button in the header opens the audio and MIDI settings. Every MIDI input opens on launch, so a keyboard
plays straight away.

### In a DAW

- **Host sync** is on by default: press **Play** to arm the sequencer, and it runs while the host plays. Turn Host sync
  off to run Asam's own clock.
- While the sequencer is stopped, MIDI notes play the voice: overlapping notes slide, velocity 100 and above is an
  accent. While it runs, notes transpose the pattern relative to C3 (MIDI 60).
- Space, the arrows and the letter keys are left to the host.

## Project layout

```
CMakeLists.txt
Source/
  PluginProcessor.*       audio, sequencer, MIDI, state
  PluginEditor.*          the interface: strip, header, Voice and Pattern panels, Clock, Bank and Transport console
  StandaloneApp.cpp       the standalone app: native title bar, MIDI inputs opened on launch
  Params.*                parameters and how their values read
  dsp/                    DiodeLadder, AcidVoice, AcidDrive, small filters
  seq/                    patterns and the preset bank
  ui/                     Rapi for JUCE: tokens, type, Lucide icons, widgets, the step grid, look and feel
Resources/Fonts/          Geist, Geist Mono and Geist Pixel (SIL OFL 1.1)
Tests/AsamCheck.cpp       offline checks
ThirdParty/JUCE/          JUCE 9.0.5 (submodule)
docs/                     screenshots
```

## Checks

`AsamCheck` renders the synth offline and checks it, without a host or an audio device:

```bash
cmake --build build --target AsamCheck
./build/AsamCheck_artefacts/Release/AsamCheck <output-folder>
```

It measures the filter (where the resonance peak lands, stability at full resonance), renders every pattern to WAV and
checks for NaN, clipping and DC, sweeps the Distortion knob for level, runs at 44.1, 96 and 192 kHz, round-trips the
saved state, and writes the interface to PNG in both themes. The Audio Unit also passes `auval -v aumu Asm6 Strv`.

## Credits

The 21 Strive brand book defines the marks, colours and type, and Rapi the language of the interface. Geist, Geist Mono
and Geist Pixel are by Vercel with basement.studio, under the SIL Open Font License. Icons are from Lucide (ISC).
The patterns are original.

JUCE is dual-licensed under the AGPLv3 and the commercial JUCE licence; releasing Asam as closed or commercial software
needs a commercial JUCE licence (see `ThirdParty/JUCE/LICENSE.md`).
