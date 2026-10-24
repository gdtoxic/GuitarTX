GuitarTX {{VERSION}} (Windows x64)
==============================

Standalone guitar processor in the Doom / Mick Gordon spirit:
Noise Gate, Overdrive, Distortion, Cabinet (IR or built-in models), EQ, Tone Match, Sub 808, Chaos,
Pitch, Glitch (stutter / glitch / freeze), Delay, Reverb, drum machine,
Master compressor + limiter, Tuner, reorderable chain, parallel lane, presets.

Run: GuitarTX.exe (no installer needed).
Audio: Settings -> pick your audio interface (ASIO recommended), input/output channels.
Tuner: TUNER button (Esc to close). Mutes the output while open (can be turned off).
MIDI: MIDI button. Right-click a knob -> MIDI Learn (expression pedal), right-click a pedal title -> footswitch.
     Footswitch mode Toggle (each press) or Hold / Latch (on while pressed - hold to stutter / freeze).
     Tap tempo for Glitch. Program Change N -> preset N+1. Enable MIDI inputs in Settings.
Recorder: REC button - DI (dry) + Wet (processed) WAV files in Documents\GuitarTX\Recordings.
     Re-amp (menu next to REC): plays a DI file through the current preset and records it again.
Drums: DRUMS button (play / stop next to it). Kits Doom / Metal / Industrial or your own WAV per track,
     32-step sequencer, 14 factory patterns, global tempo + Tap. Your patterns: %APPDATA%\GuitarTX\Patterns
Snapshots A/B: two sets of knob values / On-Off per preset (right-click A or B to copy).
Preset browser: Browse button - search, favourites, tags, folders, import / export (.gtxpreset).
Presets: %APPDATA%\GuitarTX\Presets

Source code: https://github.com/gdtoxic/GuitarTX
License: GNU AGPLv3 (see LICENSE.md). Third-party notices: THIRD_PARTY_NOTICES.md
