# mGB Musician Guide

This guide is for playing mGB as a live Game Boy sound module.

## What You Need

 * A DMG Game Boy, Game Boy Color, Game Boy Advance/SP, Analogue Pocket with a cartridge route, or an emulator for testing.
 * A flash cart or cartridge that can run the mGB ROM and persist cartridge SRAM saves.
 * A MIDI-to-Game-Boy link adapter in full mGB/MIDI mode, such as Arduinoboy, Synccross, Synthboy+, Nanoloop USB-MIDI in MIDI mode, or compatible hardware.
 * A MIDI keyboard, sequencer, tracker, DAW, or hardware router.
 * The Game Boy audio output connected to headphones, an audio interface, mixer, or amp.

mGB does not make sound by itself at the menu. It responds when MIDI note data arrives through the link cable.

## First Sound

1. Flash or load `Releases/mGB_1_4_0.gb`.
2. Start mGB on the Game Boy.
3. Put your link adapter into full mGB/MIDI mode. For Arduinoboy, this is Mode 5.
4. Send a note on MIDI channel 1.
5. If there is no sound, press `Start` once for panic, then check the troubleshooting section.

Default channels:

| MIDI channel | mGB voice |
| --- | --- |
| 1 | PU1 pulse |
| 2 | PU2 pulse |
| 3 | WAV |
| 4 | NOISE |
| 5 | POLY, using PU1/PU2/WAV |

The NOISE channel is not a pitched oscillator like PU1, PU2, or WAV. It is best for percussion, texture, and Game Boy noise colors.

## Runtime MIDI Channel Setup

Open global setup with `Select + Start`.

Use the D-pad to move the cursor. Hold `A` and press the D-pad to change a value. Press `Select + Start` again to return to the normal synth screens.

Global setup options:

| Option | Meaning |
| --- | --- |
| BASE | First MIDI channel used by PU1, PU2, WAV, NOISE, and POLY. Valid values are 1-13. |
| PROFILE | Manual, GB1, GB2, or GB3. |
| MPE | Enables pressure and CC74 performance controls. |
| VEL | Velocity curve: linear, soft, hard, or full. |
| TUNE | Equal, just, Pythagorean, 19-EDO, or 24-EDO. |
| LEGATO | Pulse-channel legato without retriggering overlapping notes. |
| POLY CH | MIDI channel for poly mode, or `OF` for off. |

Profile mappings:

| Profile | PU1 | PU2 | WAV | NOISE | POLY |
| --- | --- | --- | --- | --- | --- |
| GB1 | 1 | 2 | 3 | 4 | 5 |
| GB2 | 6 | 7 | 8 | 9 | 10 |
| GB3 | 11 | 12 | 13 | 14 | 15 |

Press `Select + B` on the global setup screen to save the global setup. Press `B` on the global setup screen to load it.

Each normal synth screen also has its own MIDI channel row. Set a synth channel to `OF` to ignore that voice. Editing an individual channel changes the profile back to Manual.

## Multi-Game-Boy Polyphony

The GB1/GB2/GB3 profiles are the quickest way to use three Game Boys on one MIDI stream:

 * Game Boy 1: profile GB1, channels 1-5.
 * Game Boy 2: profile GB2, channels 6-10.
 * Game Boy 3: profile GB3, channels 11-15.

For a keyboard or sequencer that rotates each note to a different MIDI channel, use the pitched voices and disable POLY:

| Game Boy | PU1 | PU2 | WAV | NOISE | POLY |
| --- | --- | --- | --- | --- | --- |
| 1 | 1 | 2 | 3 | 4 | OF |
| 2 | 5 | 6 | 7 | 8 | OF |
| 3 | 9 | 10 | 11 | 12 | OF |

That gives twelve independently addressable Game Boy voices, with nine pitched voices and three noise voices. Save each Game Boy's setup with `Select + B` before performing.

## Live Controls

Buttons:

 * `Start`: panic/all sound off.
 * `Select + Start`: global setup screen.
 * `Select + A`: screen off/on for lower noise, battery life, and DMG performance.
 * `Select + D-pad`: select multiple synth columns for editing.
 * `Select + B`: save preset on synth screens, save global setup on global setup.
 * `B`: load preset on synth screens, load global setup on global setup.

MIDI controls:

 * Program Change 1-15: load preset.
 * Pitch Bend: pitch bend per voice.
 * CC1/CC2/CC3/CC4/CC5/CC10/CC64: original mGB sound controls.
 * CC11: vibrato depth.
 * CC12: vibrato rate.
 * CC74: timbre, only when MPE mode is on.
 * CC120: global panic/all sound off.
 * CC121: reset controllers.
 * Channel pressure and poly aftertouch: volume/expression, only when MPE mode is on.

MPE mode is intentionally small. It does not turn one Game Boy into a full MPE synth, but it gives modern controllers a useful path for pressure and timbre.

## Microtuning

The tuning modes are:

 * Equal: normal 12-tone equal temperament.
 * Just: just-intonation table.
 * Pythagorean: Pythagorean tuning table.
 * 19-EDO: 19 equal steps per octave.
 * 24-EDO: 24 equal steps per octave.

In 19-EDO and 24-EDO, one MIDI key equals one EDO step. Octaves repeat after 19 or 24 MIDI notes, not after 12 piano keys. Use a sequencer, MIDI processor, or controller layout that matches that expectation.

## Troubleshooting

No sound:

 * Confirm the adapter is in full MIDI/mGB mode, not clock/sync-only mode.
 * Confirm MIDI OUT from your controller goes to MIDI IN on the adapter.
 * Try MIDI channel 1 first.
 * Check that the mGB voice output is not set to off or muted left/right.
 * Press `Start` for panic, then send a fresh note.
 * Try a short direct chain before adding MIDI thru, mergers, splitters, or DAW routing.

Wrong voice responds:

 * Check the global setup profile.
 * Check the individual MIDI channel row on each synth screen.
 * Remember that MIDI channel numbers shown in mGB are 1-16, while code and some tools use 0-15.

Global settings do not persist:

 * Save on the global setup screen with `Select + B`.
 * Confirm your flash cart or emulator supports cartridge SRAM saves for MBC1 cartridges.
 * If the cart has a battery-backed save, make sure the battery is good.

Microtuning sounds unexpected:

 * In 19-EDO and 24-EDO, every MIDI key is a tuning step. This is not a 12-key octave-preserving retune.

Before a set, do a quick preflight: load each Game Boy, confirm the saved profile, send one note to every assigned channel, test `Start` panic, and make sure your mixer gain leaves headroom.
