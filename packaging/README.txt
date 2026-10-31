GuitarTX {{VERSION}} (Windows x64)
==============================

Standalone guitar processor in the Doom / Mick Gordon spirit:
Noise Gate, Compressor, Wah, Overdrive, Distortion (amp models Modern / American / British / Doom Fuzz / Tight,
power amp: Power / Sag / Resonance), Cabinet (IR or built-in models, two IRs with a mix, mic position / distance),
Harmonizer (in key), Modulation (chorus / flanger / phaser / tremolo), EQ, Tone Match, Sub 808, Chaos,
Pitch, Glitch (stutter / glitch / freeze), Delay, Reverb, drum machine, looper,
Master compressor + limiter, Tuner, reorderable chain, parallel lane, presets.

Run: GuitarTX.exe (no installer needed).
Audio: settings button (sliders icon) -> Audio / MIDI device: pick your interface (ASIO recommended).
     Same menu: Interface scale 80-125 %. CPU load is shown at the bottom right.
Tuner: TUNER button (Esc to close). Mutes the output while open (can be turned off).
MIDI: MIDI button. Right-click a knob -> MIDI Learn (expression pedal), right-click a pedal title -> footswitch.
     Switch mode Toggle (each press) or Hold (on while pressed, off when released): pedals, snapshot B,
     tuner, record, drums, LISTEN. Double-click a row in the MIDI window to switch.
     Tap tempo for Glitch. Program Change N -> preset N+1. Enable MIDI inputs in Settings.
Recorder: REC button - DI (dry) + Wet (processed) WAV files in Documents\GuitarTX\Recordings.
     Re-amp (menu next to REC): plays a DI file through the current preset and records it again.
Drums: DRUMS button (play / stop next to it). Kits Doom / Metal / Industrial or your own WAV per track,
     32-step sequencer, 14 factory patterns, global tempo + Tap. Your patterns: %APPDATA%\GuitarTX\Patterns
     LISTEN: play a riff, stop - tempo and a matching beat are set. FOLLOW: drums start with your first note
     and follow your tempo. Generate by style, Vary, fills every 2 / 4 / 8 loops.
     LISTEN takes are saved to Documents\GuitarTX\Recordings\Listen (last 30).
Looper: LOOP button. Record -> Play -> Overdub (Space), Stop, Undo / Redo, x2, Clear, Save WAV
     (Documents\GuitarTX\Recordings\Loops). With drums the loop snaps to bars; without drums the first loop
     sets the tempo and starts the drums. Footswitch: press - main, double press - stop, hold 2 s - clear.
Snapshots A/B: two sets of knob values / On-Off per preset (right-click A or B to copy).
Preset browser: Browse button - search, favourites, tags, folders, import / export (.gtxpreset).
Presets: %APPDATA%\GuitarTX\Presets

Source code: https://github.com/gdtoxic/GuitarTX
License: GNU AGPLv3 (see LICENSE.md). Third-party notices: THIRD_PARTY_NOTICES.md
