# mGB Developer Guide

This guide is for building, testing, and modifying this fork.

## Requirements

 * GBDK-2020. This fork has been tested with GBDK-2020 4.5.0.
 * Python 3.
 * SameBoy source for emulator regression testing.
 * SameBoy boot ROMs, usually from `/Applications/SameBoy.app/Contents/Resources` on macOS.
 * A C compiler for the SameBoy harness.

The old `gbdk-n` submodule has been removed. GBDK-2020 is intentionally not
vendored as a submodule; install it externally and pass its path with
`GBDK_HOME`.

## Build

```sh
make -C Source build GBDK_HOME=/path/to/gbdk
```

The build writes `Source/mgb.gb`. That file is ignored by git so it can be rebuilt freely.

The repo keeps historical release ROMs under `Releases/` and the original root `mGB.gb` as regression baselines.

## Local ROM Checks

```sh
make -C Source test GBDK_HOME=/path/to/gbdk
```

This checks ROM header compatibility and validates the generated ROM against historical committed binaries.

To compare against the configured upstream remote:

```sh
git fetch upstream
make -C Source test-upstream GBDK_HOME=/path/to/gbdk
```

## SameBoy MIDI Regression

```sh
make -C Source sameboy-midi-regression GBDK_HOME=/path/to/gbdk
```

Defaults:

 * SameBoy source: `/tmp/SameBoy-src`
 * Boot ROM directory: `/Applications/SameBoy.app/Contents/Resources`
 * Work directory: `/tmp/mgb-sameboy-midi-regression`

Override with environment variables:

```sh
SAMEBOY_SRC=/path/to/SameBoy \
SAMEBOY_BOOT_DIR=/path/to/bootroms \
MGB_SAMEBOY_WORK_DIR=/tmp/mgb-test \
make -C Source sameboy-midi-regression GBDK_HOME=/path/to/gbdk
```

The harness injects MIDI bytes through SameBoy's external serial/link API. It covers CGB and DMG behavior, current/previous/upstream ROM comparisons, runtime channel maps, saved global setup, 4-way Synccross-style fan-out, panic/reset, MPE-lite controls, velocity curves, microtuning, legato, and save-data recovery.

Results are written to:

```text
/tmp/mgb-sameboy-midi-regression/sameboy_midi_summary.txt
```

## Data Model

`dataSet` stores both sound parameters and global setup.

| Range/index | Meaning |
| --- | --- |
| 0-23 | PU1, PU2, WAV, and NOISE sound parameters. |
| 24-27 | Per-synth preset slots. |
| 28-32 | PU1, PU2, WAV, NOISE, and POLY MIDI channels. Values 0-15 mean MIDI channels 1-16; value 16 means off. |
| 33 | Base MIDI channel. Value 0 displays as channel 1. |
| 34 | Channel profile: 0 manual, 1 GB1, 2 GB2, 3 GB3. |
| 35 | MPE-lite mode. |
| 36 | Velocity curve. |
| 37 | Tuning mode. |
| 38 | Pulse-channel legato mode. |

`saveData` is placed in cartridge SRAM by `Source/mgb_save.c`. Global setup uses a small validated block inside SRAM with a magic byte, version byte, and checksum. Presets still use the original preset slots.

## Adding MIDI Behavior

Most MIDI parsing is in `Source/mGBASMMidiFunctions.s`. Keep these points in mind:

 * Preserve running-status handling.
 * Real-time bytes such as MIDI clock can arrive between data bytes.
 * Per-channel behavior should compare against the runtime channel map in `dataSet[28..32]`.
 * Global panic/reset should work even if the incoming channel is not mapped.
 * Update SameBoy cases for new MIDI behavior before release.

Display and button behavior live mainly in:

 * `Source/mGBDisplayFunctions.c`
 * `Source/mGBUserFunctions.c`
 * `Source/mGBSynthCommonFunctions.c`
 * `Source/mGBMemoryFunctions.c`

## Release Process

1. Build and run the local checks.
2. Run SameBoy MIDI regression.
3. Copy the generated ROM to `Releases/mGB_1_4_0.gb`.
4. Commit source, docs, tests, and the release ROM.
5. Tag the commit.
6. Create a GitHub release with the `.gb` file and checksum.

Example:

```sh
make -C Source test GBDK_HOME="$HOME/.cache/gbdk-2020/4.5.0/gbdk"
make -C Source sameboy-midi-regression GBDK_HOME="$HOME/.cache/gbdk-2020/4.5.0/gbdk"
cp Source/mgb.gb Releases/mGB_1_4_0.gb
shasum -a 256 Releases/mGB_1_4_0.gb
```
