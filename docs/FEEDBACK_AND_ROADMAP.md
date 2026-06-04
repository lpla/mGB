# Feedback and Roadmap

This document connects old public mGB feedback to the current fork and separates good future work from ideas that are probably too risky for a small live ROM.

## Sources Checked

 * Arduinoboy official README: https://github.com/trash80/Arduinoboy
 * Arduinoboy Google Group, "Default mgb midi channel": https://groups.google.com/g/arduinoboy/c/4EBX-RDZ0WM
 * Gameboy Genius, "mGB with extended MIDI channel support": https://blog.gg8.se/wordpress/2009/09/16/mgb-with-extended-midi-channel-support/
 * GameBrew mGB user guide mirror: https://www.gamebrew.org/wiki/MGB
 * mGB Max4Live Controller Pack: https://maxforlive.com/library/device/3323/
 * Analogue Pocket + mGB + MIDI tutorial: https://www.reddit.com/r/AnaloguePocket/comments/18eks6y/tutorial_analogue_pocket_mgb_midi/
 * mGB vibrato discussion: https://www.reddit.com/r/chiptunes/comments/1isejcy/mgb_vibrato_in_v140ts/
 * Raspberry Pi to mGB timing discussion: https://www.reddit.com/r/Gameboy/comments/sbrvdi/need_help_in_mgb_raspberry_communication/
 * Modern Game Boy MIDI adapter discussion: https://www.reddit.com/r/Gameboy/comments/1bmqh5d/may_i_offer_your_game_boy_some_midi_in_these/

## What The Feedback Asked For

Runtime channel selection:

Old users wanted to move mGB away from channels 1-5 so it could share a MIDI output with MIDINES, other synths, or multiple Game Boys. The 2009 extended-channel hack shipped separate 1-5, 6-10, and 11-15 ROMs, and the author explicitly mentioned wanting in-program channel selection later. This fork now provides that at runtime through BASE, per-synth channels, and GB1/GB2/GB3 profiles.

No-code configuration:

Arduinoboy's own docs mention a desktop editor for MIDI settings and an mGB MIDI mapping section. That helps adapter users, but it still puts configuration outside the ROM. This fork keeps the mapping inside mGB, saves it in cartridge SRAM, and works with any adapter that sends the same mGB link data.

Multi-Game-Boy polyphony:

The old extended-channel ROMs and later comments point at exactly this use case. The new profiles suit the historical three-ROM setup. Manual channel mapping also supports a twelve-voice layout with POLY off: three Game Boys, nine pitched voices, and three NOISE voices.

Hidden or missing documentation:

Community posts still explain basics such as "send notes on channels 1-4", "mGB alone does not make sound", and "vibrato exists on CC11/CC12". The new musician guide documents these directly.

Adapter confusion:

Recent posts show that many users are no longer only using classic Arduinoboy hardware. Nanoloop USB-MIDI, Analogue Pocket routes, Raspberry Pi hosts, and new small adapters all appear in the ecosystem. The docs now focus on the requirement that matters to mGB: full MIDI bytes must reach the Game Boy link protocol, not just clock or sync.

Reliability:

Users building custom adapters run into timing problems. The SameBoy harness cannot prove every physical adapter, but it does prove the ROM against serial/link input, CGB/DMG modes, save recovery, and 4-way fan-out. The ROM also now stores setup in real cartridge SRAM instead of relying on unsafe placement.

Modern synth expectations:

Velocity, aftertouch, timbre, panic/reset, legato, and microtuning are normal expectations in current live synth setups. The new controls add those features without trying to make the Game Boy into something it cannot be.

## Future Work Worth Considering

Real hardware release matrix:

Test and document DMG, CGB, GBA/SP, Analogue Pocket, EverDrive/EZ-Flash-style carts, Arduinoboy, Synccross, Nanoloop USB-MIDI, and at least one modern small adapter. Capture audio and note save behavior for each.

Permanent audio waveform regression:

The current SameBoy regression already checks APU and audio hashes. A deeper harness could keep small PCM captures and measure expected pitch, RMS, panic silence, and retrigger behavior automatically.

Preset and setup librarian:

A small desktop or web tool could edit SRAM save files, generate per-Game-Boy maps, and document all CCs. That is safer than adding a large editor UI inside the ROM.

Microtuning expansion:

Add build-time generated tuning tables, or a save-file based tuning-table injector. Useful additions would be octave-preserving 12-key retunes, alternate 19/24-EDO keyboard layouts, and Scala-derived tables. A full Scala editor inside the ROM is probably not worth the UI and memory cost.

MIDI implementation chart:

Publish a clean table of every note, CC, program change, pitch bend, pressure, and global command. This helps DAW users, Max4Live device authors, and hardware adapter builders.

Adapter diagnostic ROM or mode:

A tiny diagnostic ROM could show incoming bytes, channel, status, and link timing. This would help users who are not sure whether a problem is the adapter, cable, MIDI route, cart, or mGB.

Config reset shortcut:

A guarded factory-reset path for global setup could save performers if a cart has bad SRAM or a confusing saved channel map.

## Ideas To Treat Carefully

Full MPE:

True per-note MPE on one Game Boy conflicts with the tiny number of voices and the existing channel model. MPE-lite is a better fit.

Large runtime tuning editor:

The ROM UI is too small for comfortable Scala/MTS editing. Use external tools and save-file generation instead.

Audio input effects:

Delay, sampling, and external audio processing are outside mGB's scope and likely to hurt reliability.

Deep adapter-specific behavior inside mGB:

mGB should stay adapter-agnostic. Hardware-specific quirks belong in adapter firmware or documentation unless a ROM-side change fixes a broad link-protocol issue.
