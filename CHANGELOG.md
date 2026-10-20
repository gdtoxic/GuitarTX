# Changelog

All notable changes to GuitarTX. Format: [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions: [SemVer](https://semver.org/).

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
- GitHub Actions: Windows x64 build on every push, draft release with zip + SHA-256 on a `v*` tag.
- `CHANGELOG.md`, issue templates (bug report / feature request).

## [0.1.1] - 2026-10-17

First public release.

- Reorderable effect chain (drag & drop, click-free rebuilds) and parallel lane.
- Effects: Noise Gate, Overdrive, Distortion (2-stage, 4x oversampling), Cabinet (IR),
  Tone Match, Sub 808 (pitch-tracked sub: Sine / Square / Divider), Chaos.
- Master compressor + lookahead limiter.
- Factory and user presets (`%APPDATA%\GuitarTX\Presets`).

[0.1.2]: https://github.com/gdtoxic/GuitarTX/compare/v0.1.1...v0.1.2
[0.1.1]: https://github.com/gdtoxic/GuitarTX/releases/tag/v0.1.1
