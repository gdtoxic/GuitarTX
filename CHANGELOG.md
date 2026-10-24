# Changelog

All notable changes to GuitarTX. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions: [SemVer](https://semver.org/).

## [0.3.3] - 2026-10-25

### Added
- **Glitch** pedal (stutter / glitch / freeze) for noise breaks:
  - **Stutter** — the first beat plays and is recorded, then repeats on the grid while the pedal is on;
    **Ramp** makes every repeat shorter (the "speed-up" before a break);
  - **Glitch** — every grid step gets a random action: repeat, reverse, half / quarter slicing, octave up,
    drop or live; **Chaos** sets how often and how wild;
  - **Freeze** — holds the sound endlessly (grains from a captured fragment), you play on top; after switching off
    it fades out over **Fade**; switching on again while it still sounds grabs a new chord;
  - **BPM** 40–240 with a **Tap** button, **Division** 1/2 … 1/64 incl. triplets, **Mix**, **Tone** (low-pass on the
    effect), **Level**; repeats are exactly on the grid, edges are crossfaded (no clicks).
- **MIDI Tap tempo**: a CC button that taps the BPM of every Glitch in the chain (Learn: Tap tempo).
- **6 new factory presets with Glitch** (74 in total): Doom Freeze, Stutter Breakdown, Glitch Machine, Freeze Pad,
  Stutter Lead, Broken Tape; Glitch on snapshot B in Drone Wall and Funeral Doom (Freeze), Stomp Sub (stutter),
  Glitch Stairs (glitch slicing). Full Rig gets a Glitch pedal (off). 39 factory presets now have A/B.

### Changed
- Footswitch modes renamed: **Toggle** (each press switches) and **Hold / Latch** (on while the CC is ≥ 64:
  with a momentary switch it is "hold to stutter / freeze", with a latching one a normal on/off).
  Saved MIDI settings keep working.

## [0.3.2] - 2026-10-22

### Added
- **Preset browser** — Browse button next to the preset name:
  - search by name, category, folder and tags ("lead delay", "808", "A/B"), Esc clears;
  - groups: All, Favourites, every factory category, User presets and each user folder;
  - **tags**: factory presets are tagged automatically from their chain (clean / crunch / high-gain, rhythm / lead,
    pitch, sub, chaos, delay, reverb, ambient, A/B); user presets get the same plus **your own tags** (Tags...);
  - **favourites** (click the star): shown first in the browser and at the top of the quick preset menu;
  - **folders** for user presets (New folder..., Move to...), subfolders of `%APPDATA%\GuitarTX\Presets`;
  - **import / export** of `.gtxpreset` files; an imported name that already exists gets a number ("Name 2"),
    files that are not GuitarTX presets are skipped;
  - double-click or Enter loads; the current preset is highlighted.
- Quick preset menu (click on the name): Favourites on top, user presets grouped by folder.

### Changed
- **All 46 original factory presets reworked** with the effects that did not exist when they were made:
  - an EQ after the cabinet in every distorted preset, matched to the style (Doom weight, tight metal,
    thrash scoop, nu-metal low end, djent click, lead mid push, clean sparkle);
  - **snapshot B** in 23 of them (30 factory presets with A/B in total): rhythm -> lead with delay and reverb
    (Doom Rhythm, Tight Chug, Thrash Rhythm, Classic Heavy, Crunch Rhythm, Grunge Wall…), pitch tricks on B
    (Doom Divider and Metalcore Breakdown dive on every hit, Machine Rhythm octave trill, Robot Ring random notes,
    Whammy Squeal sweeping LFO, Djent Tight / Scooped 7-String / Grind Gate octave down, Drop Chug squeal),
    Clean Verse -> heavy chorus, Industrial Crush -> "radio" break, Black Metal Tremolo -> atmospheric part;
  - leads got delay and reverb where they had none (Doom Lead, Doom Lead Scream, Whammy Octave Lead, Bitcrush Lead);
  - chorus-like shimmer (Pitch LFO) on Clean Verse, Cold Wave Clean, Clean Arpeggio, Shoegaze Wash;
  - levels re-balanced; the Cabinet stays on Auto everywhere, so your own IR keeps working.
- **EQ curves**: `<` / `>` buttons step through the factory curves, the name opens the list.
- User presets are tracked by file: presets with the same name in different folders no longer mix up;
  Save writes back into the preset's folder, Save As saves into the current folder.

### Fixed
- Cabinet: the line under the buttons showed the previous state after turning **Model** (e.g. "Built-in 4x12 Modern"
  while Vintage was selected). Loading an IR now switches Model to **Auto** so the IR is actually heard;
  long IR names are cut with "…" (full path in the tooltip).

## [0.3.1] - 2026-10-21

### Added
- **Recorder** — REC button in the header: two WAV files of equal length, **DI** (dry guitar, mono, before the tuner
  and Input gain) and **Wet** (what you hear, stereo). 24-bit or 32-bit float, folder `Documents\GuitarTX\Recordings`
  (changeable). Disk writing runs in a background thread.
- **Re-amp** — menu next to REC: a DI file plays instead of the guitar through the current preset and the result is
  recorded (2.5 s of tail after the end of the file for delay / reverb).
- **Snapshots A/B** — A / B buttons next to the preset name: two sets of knob values and pedal On/Off for the same chain
  (rhythm / lead). Instant switching, saved in presets and the session. Right-click: copy to the other snapshot.
  Switching without edits does not mark the preset as modified.
- **Pitch LFO** — cycles the pitch: Sine, Triangle, Square (octave trill), Saw Up, Saw Down, Random, Random Steps
  (semitone-quantised), Rate 0.05–20 Hz, Depth ±24 st, Retrig (each pick attack restarts the cycle).
- **EQ curves** — 16 factory EQ curves in the EQ panel (Tight Metal, Doom Weight, Thrash Scoop, Nu-Metal Low,
  Djent Clank, Lead Push, Mix Ready, Fizz Killer, Telephone, AM Radio, Cocked Wah…), the active one is shown by name.
- **Clear IR** button in the Cabinet — back to the built-in cabinet model.
- **20 new factory presets (68 total)**:
  - *Top Picks* (most with A/B snapshots): BFG Rhythm / Lead, Modern Metal A/B, Clean to Crush, Nu-Metal Drop A/B,
    Eternal Hybrid, Thall, Arena Lead;
  - *Insane*: Octave Machine Gun, Random Robot, Air Raid Siren, Dive Bomb Loop, Seasick, Glitch Stairs, Helicopter,
    Black Hole, Broken Radio, Slayer Scream, Stuck Wah Lead, Deathcore Abyss, Infinite Feedback.
- MIDI actions: **Record start/stop** and **Snapshot A/B** (MIDI window -> Learn).
- **App icon** — guitar pick with a clipped waveform (exe, window, taskbar); small sizes are drawn
  separately with a bolder line so the icon stays readable at 16 px.

### Changed
- MIDI window: two rows of Learn buttons.
- Factory preset categories: Top Picks and Insane go before Utility (Program Change numbers of Tone Match Template
  and Full Rig moved to the end of the list).

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
