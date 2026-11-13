<p align="center"><img src="docs/logo.png" alt="Nam KNoB" width="560"></p>

# Nam KNoB — NAM amp head

*(formerly VFXAamps)*

A professional Neural Amp Modeler amp head with a **10-slot morphing gain knob** and a **dual IR loader**.
VST3, AU (macOS) and Standalone; macOS universal (Apple Silicon + Intel) and Windows x64.


![Nam KNoB](docs/vfxaamps-ui.png)

## What it does

**Signal chain:** input trim → noise gate → NAM pedal 1 → NAM pedal 2 → [compressor: PRE AMP] → 10-slot NAM morph → bass / middle / treble + presence → dual IR → [compressor: POST CAB] → graphic EQ → delay → reverb → [compressor: END] → master → limiter

- **RESET** (amp head header): after a confirmation, removes the captures from all 10 slots (their level and IR
  switches back to default) and sets BASS, MIDDLE, TREBLE and PRESENCE to 5.0. Pedals, IRs and the rack are untouched.
- **Pedalboard.** Two optional NAM pedal loaders in front of the amp (drop a .nam capture of a boost, overdrive, …): footswitch,
  level, LED and name display. Off or empty, a pedal passes the guitar straight through (bit-exact). Pedal captures keep their
  own gain (no loudness matching), so a boost pushes the amp like the real thing. A pedal only runs while it holds a capture,
  so empty pedals add no latency at any sample rate. Show or hide the board with **PEDALS**.
- **The 10-slot NAM VF-X knob.** Load a `.nam` capture into each of slots 1–10 (WaveNet, LSTM, A2 / slimmable, all sizes). The big knob
  runs continuously from 1.0 to 10.0 and morphs between neighbouring slots like a real amp's gain: at 1.5 you hear an
  equal-power blend of slots 1 and 2. Empty slots are skipped (with slots 2 and 5 loaded, the knob blends 2 → 5 across that range).
  No steps, gaps or clicks, and full DAW automation.
- **Seamless by design.** Models load on a background thread and are swapped in lock-free. Only the 2–4 models that matter run:
  the two being blended plus one loaded neighbour on each side, kept "warm" so they're ready before they fade in. A model that
  becomes warm is first caught up on the input history covering its receptive field, so its state is exactly what it would be had
  it been running all along (tested sample-for-sample against always-running references: −127 dB deviation).
  Loading, clearing or big knob jumps crossfade over 40 ms.
- **Per-slot IR switch.** Each slot has an **IR** button: on, the slot plays through the dual IR (cab); off, it bypasses it — for
  captures that already include a cab (full rig). Morphing between an IR-on and an IR-off slot blends the cab in or out with
  the knob, without clicks. Loading a capture whose metadata says *amp + cab* / *full rig* turns its slot's IR off for you
  (an *amp-only* capture turns it on); reopening a session keeps your settings.
- **Per-slot level** trims, auto-normalised to each capture's loudness metadata (−18 dBFS; switch with *Level match*).
- **Sample rates.** Captures run at their trained rate (normally 48 kHz). At other host rates the engine resamples in and out with a
  windowed-sinc resampler (~1.5–1.7 ms latency, reported to the host). At 48 kHz with oversampling off, latency is **zero**.
- **Dual IR.** IR A and IR B (.wav/.aiff/.flac, any sample rate, mono or stereo): on/off, level, pan, phase invert, 0–2 ms alignment,
  low cut and high cut, A/B blend and a global IR switch. Zero-latency partitioned convolution; loading a new IR crossfades.
- **Sound quality.** 64-bit smoothing, filters, gate and limiter; double-precision host processing supported; no denormals;
  no allocations or locks on the audio thread; every parameter smoothed per sample. Optional 2×/4× oversampling for the tone stack
  and limiter.
- **Tuner** in the header, listening to your guitar input: note, a cents needle (green within ±3 cents) and the frequency.
  Accurate to about a cent from low F# on an 8-string up to C6. Click it to mute the output while you tune.
- **Effects rack** under the amp head, in rack-mount units (show or hide it with **RACK**):
  - **VX-O2 tube opto leveler:** feedback leveling amp with an optical-cell model — ~10 ms attack, two-stage
    program-dependent release (~60 ms to half, then 0.15–4 s), COMPRESS (~3:1, very soft knee) or LIMIT (~9:1), tube output
    stage (~0.1 % THD at nominal level, mostly 2nd harmonic), flat 20 Hz–20 kHz. GAIN, PEAK REDUCTION, HF EMPHASIS, MIX,
    analog VU meter (gain reduction / output). **POSITION** moves it: PRE AMP, POST CAB or END (click-free while playing).
  - **Graphic EQ** — pick the type with the **10-BAND / 31-BAND** buttons (switching is click-free):
    - **VX-10:** 10 bands (31 Hz – 16 kHz, ±12 dB) on faders, a live response curve, FLAT to reset.
    - **VX-31 dual 31-band EQ / limiter:** two channels (A / B, LINK), 31 constant-Q 1/3-octave bands on the ISO centres
      20 Hz – 20 kHz (±15 or ±6 dB), INPUT gain (±12 dB), 40 Hz 18 dB/oct Bessel LOW CUT, NR (up to 10 dB downward
      expansion), peak LIMIT with THRESH, OUT / LIMIT / NR LEDs. Built to the published specs of a classic dual 31-band
      graphic EQ / limiter; bit-exact passthrough when flat.
  - **VX-D8 studio multi-delay:** six modes — DIGITAL (pristine), TAPE (wow, flutter, saturation, repeats darken), ANALOG
    (warm bucket-brigade style), MOD (chorused repeats), REVERSE (each stretch of playing comes back backwards) and DUAL
    (right side at 1.5x for a rhythmic spread). Time up to 2.5 s or synced to the host (1/1 … 1/16, dotted, triplet), feedback,
    diffusion (smears repeats into a wash), mix, low cut / high cut in the feedback loop, modulation depth and rate, ducking
    (repeats dip while you play and swell back when you stop), ping-pong. Trails ring out when switched off.
  - **VX-R16 studio reverb:** true-stereo, 16-line feedback delay network with modulated lines and in-loop diffusion. Programs
    AMBIENCE, ROOM, CHAMBER, HALL, PLATE and CATHEDRAL, each with its own early reflections and tank. Decay 0.2–20 s with
    separate LOW and HIGH DECAY multipliers (lows ring longer, highs darken), pre-delay, size, diffusion, modulation, early
    reflections level, low cut / high cut, width and mix. Stable at any setting; the tail rings out when switched off.

  These are original algorithms built to the standard of top studio rack units, tuned to the published behaviour of the
  classics; they are not copies of any manufacturer's products.
- **Amp-head UI.** Resizable, vector-drawn: tolex and brushed metal, chicken-head and knurled knobs, pilot light, a glowing ring and
  slot lamps showing which two slots are blending, CPU meter. Drag `.nam` files onto slots (several fill the following slots) and IRs
  onto IR A / B, or anywhere on the faceplate.
- **State and presets.** Session state and `.vfxapreset` files store every parameter and file. Paths are saved absolute, relative to
  your home folder and relative to the preset; files that still can't be found show up in a banner to locate (click a slot, or
  search a folder for all of them at once).

## Download

Every push builds installers in GitHub Actions (**Actions → Build Nam KNoB → latest run → Artifacts**):

- `NamKNoB-macOS-installer` — a DMG: drag *Nam KNoB.app* to Applications; *Install Nam KNoB Plug-ins.pkg* puts the VST3 and AU in
  `/Library/Audio/Plug-Ins`.
  The same artifact also has `NamKNoB-VST3-…-macOS.dmg`: just the VST3 plug-in installer.
- `NamKNoB-Windows-installer` — `NamKNoB-…-Windows-x64-Setup.exe`: VST3 into `C:\Program Files\Common Files\VST3`, plus the standalone app.

Push a version tag (`git tag v1.0.0 && git push origin v1.0.0`) to also attach both installers to a GitHub Release.

> Nam KNoB keeps VFXAamps' plug-in ID, so DAW sessions and presets made with VFXAamps open in it. The installers remove the old `VFXAamps` VST3 / AU (delete the old VFXAamps app by hand), and presets move from Documents/VFXAamps to Documents/Nam KNoB on first launch.
> Its plug-in ID is different (`Vfxc`/`VFXh`), so DAW sessions made with the earlier build won't load into this one.

### macOS signing (optional)

Without Apple Developer credentials the bundles are ad-hoc signed, and macOS asks for confirmation on first launch
(right-click → Open; details in the DMG's *Read Me*). To sign and notarise, add these repository secrets:

| Secret | Value |
| --- | --- |
| `MACOS_CERT_P12_BASE64` | base64 of a .p12 holding your *Developer ID Application* and *Developer ID Installer* certificates |
| `MACOS_CERT_PASSWORD` | the .p12 password |
| `MACOS_APP_SIGN_ID` | `Developer ID Application: Your Name (TEAMID)` |
| `MACOS_PKG_SIGN_ID` | `Developer ID Installer: Your Name (TEAMID)` |
| `MACOS_NOTARY_APPLE_ID`, `MACOS_NOTARY_TEAM_ID`, `MACOS_NOTARY_PASSWORD` | Apple ID, team ID and an app-specific password for `notarytool` |

The signing and notarisation steps only run when these exist.

## Build it yourself

Requirements: CMake 3.22+, a C++20 compiler (Xcode 15+ on macOS, Visual Studio 2022 on Windows), Git. JUCE 8 and Eigen are
downloaded by CMake; NeuralAmpModelerCore is a git submodule.

```bash
git clone https://github.com/vfxchris581/Vfxraman.git
cd Vfxraman
git submodule update --init
```

**macOS (universal):**

```bash
cmake -S . -B build -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
installer/mac/make_installer.sh build/source/VFXAamps_artefacts/Release dist 1.0.0
```

**Windows (x64):**

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
"%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" /DArtefacts=%CD%\build\source\VFXAamps_artefacts\Release /DOutputDir=%CD%\dist installer\windows\VFXAamps.iss
```

Built plug-ins are in `build/source/VFXAamps_artefacts/Release/{VST3,AU,Standalone}`.
Use `-DJUCE_DIR=…` / `-DEIGEN_DIR=…` to build against local checkouts.

## Tests

- `dsp_tests` — resampler (exact integer latency, unity gain), tone stack/gate/limiter, every example NAM architecture,
  single-slot exactness, the 10-slot morph against always-running references (sweeps + jumps), and an automation stress test
  (fast sweeps, random jumps, loading/clearing/replacing slots while playing, 44.1/48/96 kHz, random block sizes).
- `plugin_tests` — reported vs measured latency (all rates × oversampling), full chain in float and double, dual IR
  (transparency, pan, phase, alignment, cuts, blend, global bypass, swaps while playing), state/presets with relative paths and
  missing-file relinking, and an editor screenshot.

## Layout

```
  dsp/            JUCE-free DSP: morph engine (NAM), resampler, filters, dynamics
  source/         plug-in: processor, dual IR, editor and UI components
  tests/          dsp_tests.cpp, plugin_tests.cpp
  installer/      macOS DMG/pkg script, Windows Inno Setup script
  external/       NeuralAmpModelerCore (submodule)
```

Third-party: [NeuralAmpModelerCore](https://github.com/sdatkinson/NeuralAmpModelerCore) (MIT), [JUCE](https://juce.com) (AGPLv3 / commercial),
[Eigen](https://eigen.tuxfamily.org) (MPL2).
