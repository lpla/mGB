# mGB
mGB is a Gameboy cartridge program (You need a Flash Cart and Transfer hardware) That enables the Gameboy to act as a full MIDI supported sound module. It works with the old DMG Gameboy as well as GBC/GBA.

[More information about Arduinoboy](https://github.com/trash80/arduinoboy)

![ScreenShot](http://trash80.net/arduinoboy/mGB1_2_0.png)

## Latest Fork Release

This fork modernizes mGB for [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020)
and adds live-performance controls that no longer require rebuilding separate
ROMs:

 * Runtime MIDI channel mapping for PU1, PU2, WAV, NOISE, and POLY.
 * GB1/GB2/GB3 channel profiles matching the historical 1-5, 6-10, and 11-15 extended ROM sets.
 * Cartridge SRAM persistence for presets and global performance setup.
 * Velocity curves, MPE-lite timbre/pressure controls, pulse-channel legato, global MIDI panic/reset, and microtuning.
 * SameBoy MIDI/link-cable regression coverage for CGB, DMG, and 4-way Synccross-style fan-out.

Download the latest `.gb` file from the GitHub release page, or use
`Releases/mGB_1_4_0.gb` after building this checkout.

## Documentation

 * [Musician Guide](docs/MUSICIAN_GUIDE.md): flashing, wiring, MIDI channels, multi-Game-Boy polyphony, live controls, microtuning, and troubleshooting.
 * [Developer Guide](docs/DEVELOPER_GUIDE.md): build requirements, GBDK-2020 migration notes, memory layout, SameBoy regression testing, and release process.
 * [Feedback and Roadmap](docs/FEEDBACK_AND_ROADMAP.md): old community feedback, how this fork addresses it, and future ideas that fit the Game Boy limits.

## Building

This fork builds with [GBDK-2020](https://github.com/gbdk-2020/gbdk-2020).

```sh
make -C Source build GBDK_HOME=/path/to/gbdk
```

The ROM is written to `Source/mgb.gb`. If `GBDK_HOME` is omitted, the Makefile
looks for GBDK-2020 at `../gbdk`.

To run local ROM regression checks against the committed binaries:

```sh
make -C Source test GBDK_HOME=/path/to/gbdk
```

To also compare header compatibility against the configured upstream remote:

```sh
git fetch upstream
make -C Source test-upstream GBDK_HOME=/path/to/gbdk
```

To run the SameBoy MIDI/link-cable regression suite:

```sh
make -C Source sameboy-midi-regression GBDK_HOME=/path/to/gbdk
```

The SameBoy suite builds a small headless harness against SameBoy Core, rebuilds
`Source/mgb.gb`, stages the upstream and previous committed ROMs, and injects
MIDI bytes through SameBoy's external serial/link API. It covers CGB and DMG
mode, single-Game-Boy MIDI behavior, 4-way Synccross-style fan-out, Start panic,
save-data recovery, and longer stress/soak scenarios.

By default the runner expects SameBoy source at `/tmp/SameBoy-src` and SameBoy's
installed boot ROMs in `/Applications/SameBoy.app/Contents/Resources`. Override
those paths with `SAMEBOY_SRC=/path/to/SameBoy` and
`SAMEBOY_BOOT_DIR=/path/to/bootroms`.

## Change Log
 * 05/22/18
   * Renamed {address,value}Byte to note/velocity.
   * Implemented proper 3 voice polyphony as opposed to round-robin style.
   * Reindented to get a more consistent codebase.
 * 06/26/15 
   * Project has been moved to GitHub along with sourcecode.
 * 12/21/12 1.3.3
   * Fixed a bug with the new pitchbend resolution
   * Optimized some of the pitchbend routine
 * 12/20/12 1.3.2
   * Increased pitchbend resolution
 * 12/19/12 1.3.1
   * Fixed PU1 Envelope retrigger bug. 
 * Feb 2 2009 1.3.0
   * Rewrote 90% of code into assembly for much faster performance- especially noticeable on DMG.
   * Changed note behavior. Removed Monophonic note memory to increase performance. 
   * Envelope does not retrigger if the notes overlap and have the same velocity- Good for arpeggios / broken chords. 
   * Note off has a slight delay so immediate retrigged notes don't cause "clicking" effect due to turning off the synth. 
   * Added screen off mode for great signal-to-noise ratio, longer battery life, and better performance on DMG. (To toggle the screen mode hold Select and press A.)
   * Created back-end routine that prioritizes processes for better performance. 
   * Added 8 "noise" shapes to the Wav synth for more interesting effects.
   * Made Wav pitch sweep stable and changed it so it glitches out at values above 8. :D
 * Nov 5 2008 Version: 1.2.4
  * Fixed small bug with the indicator arrow, it was offset vertically a bit.
  * Fixed bug with unexpected behavior with large PB Ranges
  * PB Range Max is now 48 notes. (hehe)
  * Octave Shift max is now -2/+3 
  * Added some Octave shift logic. If the current note is greater than what the GB can play due to octave shifting, it will select the lower octave note, so no off key notes will play.
  * Added Gameboy Color fast-cpu mode- better performance with newer Gameboys.
 * Oct 28 2008 - Version: 1.2.3
  * Added Note markers above synth number, so you can tell if the synth is on. ;)
  * Added PB wheel reset to MIDI panic (Start button)
  * Code more efficient (I'm sure there is still more to do here)
  * nitro2k01 @ http://gameboygenius.8bitcollective.com rewrote my gamejack serial function to make it as fast as possible. THANKS!!
 * Oct 25 2008 - Version: 1.2.2
  * Added Program Change messages to mGB
  * Rewrote MIDI data input for mGB. (Rewrote the function in ASM to make it faster)
  * Added Controller Priority. While changing parameters on the gameboy itself, MIDI messages will not overwrite your changes while your editing them. This is a good live mode feature 
 * Oct 23 2008 - Version: 1.2.1
  * Found & Fixed various bugs in 1.2.0 
  * Changed help text. Made it more clear.
 * Oct 23 2008 - Version: 1.2.0
  * Change interface a bit
  * Added presets
  * Optimized code
 * Oct 20 2008 - Version: 1.1.0
  * Added Interface
  * Changed Wav CCs Around to make more consistent with the Pu Synths.
 * Oct 4 2008 - Version: 0.1.2
  * Fixed bug with Wav Synth hanging after sequencer stop. 
  * Fixed bug with Wav Synth not resetting monophonic keyboard note triggers

## Button Shortcuts
 * Start: MIDI Panic
 * Select + Start: Toggle the global setup screen.
 * Select + Dpad: Select multiple synths for editing.
 * Select + A: Toggles the screen on or off, better battery life, less noise, and faster response.
 * Select + B: Copies all parameters on screen while cursor is not on preset number.
 * B: Pastes all parameters while cursor is not on preset number
 * A + Dpad: Change parameter value
 * To load/save presets, put the cursor on the "PRESET" number, and hit B for load, Select+B to save

On the global setup screen, B loads the saved global performance config and
Select+B saves it. The setup screen is for live-wide settings, not per-synth
presets.

## Live Performance Setup

The global setup screen adds runtime controls that do not require rebuilding the
ROM:

 * BASE: Sets the first MIDI channel for PU1/PU2/WAV/NOISE/POLY. Valid bases are 1 to 13.
 * PROFILE: Manual, GB1, GB2, or GB3. GB1 maps channels 1-5, GB2 maps 6-10, and GB3 maps 11-15.
 * MPE MODE: Enables channel pressure/poly aftertouch for volume and CC74 for timbre.
 * VELOCITY: Linear, soft, hard, or full velocity response.
 * TUNING: Equal temperament, just intonation, Pythagorean, 19-EDO, or 24-EDO.
 * LEGATO: Suppresses pulse-channel retrigger for overlapping notes while still changing pitch.
 * POLY CH: Sets the poly channel to 1-16 or OF for off.

The normal synth screens still expose each synth's MIDI channel. Each channel can
be set to 1-16 or OF for off; changing an individual synth channel switches the
profile back to manual.

## MIDI Implementation
Note: the name and number at the bottom left of the screen indicates the midi CC of the selected parameter.

 * PU1 - MIDI CH1
  * Program Change: 1 to 15
  * PB: Pitch bend - up to +/- 12
  * cc1: Pulse width - 0,32,64,127
  * cc2: Envelope mode - 0 to 127, 16 possible steps
  * cc3: Pitch sweep
  * cc4: Pitchbend Range
  * cc5: Load Preset
  * cc10: Pan
  * cc64: Sustain- Turns off note off. <64 = off, >63 = on

 * PU2 - MIDI CH2
  * Program Change: 1 to 15
  * PB: Pitch bend - up to +/- 12
  * cc1: Pulse width - 0,32,64,127
  * cc2: Envelope mode - 0 to 127, 16 possible steps
  * cc4: Pitchbend Range
  * cc5: Load Preset
  * cc10: Pan
  * cc64: Sustain- Turns off note off. <64 = off, >63 = on

 * WAV - MIDI CH3
  * Program Change: 1 to 15
  * PB: pitch bend - up to +/- 12
  * cc1: shape select : 16 possible on a 0 to 127 range
  * cc2: shape offset : 32 possible on a 0 to 127 range
  * cc3: Pitch Sweep speed. 0=Off, 1-127 speed.
  * cc4: Pitchbend Range
  * cc5: Load Preset
  * cc10: pan
  * cc64: Sustain- turns off note off. <64 = off, >63 = on

 * NOISE - MIDI CH4
  * Program Change: 1 to 15
  * PB: pitch bend +/-24
  * cc2: envelope mode - 0 to 127, 16 possible steps
  * cc5: Load Preset
  * cc10: pan
  * cc64: (sustain) turns off note off. <64 = off, >63 = on

 * POLY MODE - MIDI CH5 - Plays Pu1/Pu2 and Wav in poly
  * Program Change: 1 to 15
  * PB: pitch bend +/-2
  * cc1: See cc1
  * cc5: Load Preset
  * cc10: pan
  * cc64: (sustain) turns off note off. <64 = off, >63 = on

Additional live controls:

 * cc11: Vibrato depth.
 * cc12: Vibrato rate.
 * cc74: Timbre in MPE mode only. Maps to pulse width, WAV shape, or NOISE envelope.
 * cc120: Global panic/all sound off, regardless of the incoming MIDI channel.
 * cc121: Global reset controllers, regardless of the incoming MIDI channel.
 * Channel pressure and poly aftertouch: Volume/expression in MPE mode only.
