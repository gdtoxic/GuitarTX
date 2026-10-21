# Changelog

All notable changes to GuitarTX. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions: [SemVer](https://semver.org/).

## [0.3.0] - 2026-10-21

### Added
- **MIDI control** — expression pedals, footswitches and preset changes from any MIDI controller.
  - **MIDI Learn**: right-click any knob or switch -> *MIDI Learn*, then move a pedal / knob (CC).
    Value 0-127 maps over the full parameter range. Mappings follow the effect type, not the preset:
    an expression pedal on *Pitch: Position* works in every preset that has a Pitch.
  - **Footswitch On/Off** for any pedal: right-click the pedal's title -> *MIDI Learn: On/Off*.
    **Momentary** (each press toggles) or **Latching** (>= 64 on, < 64 off) per mapping.
  - **Program Change N** selects preset N+1 in the preset list (factory first, then user).
  - CC actions: **Next preset**, **Previous preset**, **Tuner on/off** (fire on press).
  - **MIDI window** (MIDI button in the header): enabled inputs, last received message,
    list of mappings (remove, switch mode, clear all), Learn buttons for actions.
  - Mappings are saved with the session settings. All MIDI inputs are enabled on first run.
- MIDI input selection in Audio Settings.

## [0.2.2] - 2026-10-21

### Added
- **Built-in cabinet models** — the Cabinet works without an IR file: 4x12 Modern, 4x12 Vintage, 2x12 Open,
  1x12 Combo (new **Model** knob). **Auto** (default) uses the loaded IR, otherwise 4x12 Modern —
  factory presets now sound right straight after install. Models are loudness-matched; Cab Level works without an IR.
- **EQ** — parametric equalizer: Low Cut, Low shelf, two peaking bands (Freq / Gain / Q), High shelf, High Cut, Level,
  with a live frequency-response graph.
- **Delay and Reverb tails spill over** when the pedal is switched off: repeats and the reverb tail ring out naturally;
  processing stops once the tail has died away.

### Changed
- Default window height 820 px; the effect panel scrolls if it does not fit.
- Factory preset levels re-balanced for the built-in cabinet (cleans closer to the distorted presets).
- Full Rig includes EQ (off).

## [0.2.1] - 2026-10-21

### Added
- **Delay** — Digital / **Tape** (darker repeats, saturation, wow & flutter) / **Ping-Pong** (repeats alternate left-right).
  Time up to 2 s (glides like tape when changed), Feedback up to 95 %, Tone and Low Cut inside the loop,
  Mod, Mix, and **Duck** — repeats get quieter while you play and bloom in the gaps.
- **Reverb** — 8-line feedback delay network: **Room / Plate / Hall**, Decay (RT60 0.2-20 s), Pre-delay,
  Size, Damping, Low Cut, Mod (no metallic ringing), Mix, Duck. Fully decorrelated stereo tail.
- **27 new factory presets (48 total)** for alternative rock, nu-metal, industrial, extreme and metal —
  e.g. Thrash Rhythm, Metalcore Breakdown, Drop Chug, Virtual Drop -5, Whammy Squeal, Machine Rhythm,
  Robot Ring, Black Metal Tremolo, Swedish Buzzsaw, Grind Gate, Blackgaze, Funeral Doom, Grunge Wall,
  Shoegaze Wash, Clean Arpeggio, Dotted Eighth.
- Factory presets are grouped into categories in the preset menu
  (Doom, Metal, Nu-Metal, Industrial, Extreme, Alt Rock, Clean & Crunch, Utility).

### Changed
- Levels of factory presets balanced against each other; clean presets louder (Cabinet level, needs an IR loaded).
- Full Rig includes Delay and Reverb (off).

## [0.2.0] - 2026-10-21

### Added
- **Pitch** effect — octaver / whammy / pitch-drop:
  - Shift -24...+24 semitones, works with **chords** (power chords down to drop-A): delay-line shifter
    with correlation-aligned splices; accuracy within ~3 cents, 8-28 ms delay of the shifted signal.
  - Modes: **Static** (whammy Position 0-100 %), **Dive** (every hit dives from the original pitch to Shift
    over Time — the signature pitch-drop), **Rise** (every hit scoops up from Shift to the original pitch).
  - Sens (attack threshold), Mix (dry / shifted), Tone (low-pass on the shifted signal), Level.
- 3 new factory presets (21 total): Octave Down Doom, Pitch Drop Hits, Whammy Octave Lead; Full Rig now includes Pitch.
- Integration tests (`tests/`, CMake option `GUITARTX_BUILD_TESTS`), run in CI on every build.

## [0.1.2] - 2026-10-20

### Added
- **Tuner** (TUNER button, Esc to close): note + octave, cents needle, frequency in Hz.
  - YIN pitch detection, accurate to < 1 cent from drop-A on 7/8-strings (27.5 Hz) up to ~1.5 kHz.
  - Mutes the output while open (both lanes); can be switched off.
  - Adjustable reference A4 = 430...450 Hz. Mute and A4 are remembered between sessions.
- **11 new factory presets** (18 total):
  Doom Divider, Doom Lead Scream, Djent Tight, Sludge Fuzz, Drone Wall, Clean Compressed,
  808 Hybrid, 808 Square Grind, Industrial Crush, Tone Match Template, Full Rig.
- Factory presets can now set the Master compressor (Djent Tight, Drone Wall, Clean Compressed, ...).
- CMake build (`CMakeLists.txt`) next to the Projucer project; JUCE is downloaded automatically.
- GitHub Actions: Windows x64 build on every push; zip + SHA-256 are attached automatically to a published release.
- `CHANGELOG.md`, issue templates (bug report / feature request).

## [0.1.1] - 2026-10-17

First public release.

- Reorderable effect chain (drag & drop, click-free rebuilds) and parallel lane.
- Effects: Noise Gate, Overdrive, Distortion (2-stage, 4x oversampling), Cabinet (IR),
  Tone Match, Sub 808 (pitch-tracked sub: Sine / Square / Divider), Chaos.
- Master compressor + lookahead limiter.
- Factory and user presets (`%APPDATA%\GuitarTX\Presets`).

[0.2.2]: https://github.com/gdtoxic/GuitarTX/compare/v0.2.1...v0.2.2
[0.2.1]: https://github.com/gdtoxic/GuitarTX/compare/v0.2.0...v0.2.1
[0.2.0]: https://github.com/gdtoxic/GuitarTX/compare/v0.1.2...v0.2.0
[0.1.2]: https://github.com/gdtoxic/GuitarTX/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/gdtoxic/GuitarTX/releases/tag/v0.1.1
