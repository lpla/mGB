# Fork Audit

Checked on June 4, 2026.

## Parents

| Repository | Default branch head | Result |
| --- | --- | --- |
| `trash80/mGB` | `4e70bdf` | The only commit missing from `vifino/mGB` added historical 1.3.x source ZIP archives. It is now included. |
| `vifino/mGB` | `b34cb30` | All vifino commits are included in this fork. |
| `lpla/mGB` | current `master` | Includes vifino, the remaining trash80 archive commit, and this fork's GBDK-2020/live-performance work. |

## Other Forks With Notable Development

| Fork | Notes | Action |
| --- | --- | --- |
| `tonytwostep/mGB` / `dalton-tulou/mGB` | Fixed frequency-table lookups when the table crosses a 256-byte page boundary. Also carried old gbdk-n/toolchain and pause-screen work. | Ported the frequency/noise-frequency lookup fix. Did not carry old toolchain or pause-screen changes. |
| `tstirrat/mGB` | Reworked the project around GBDK-2020 4.3.0, split much assembler into C modules, and added synth/noise frequency safety fixes. | Current fork already builds with GBDK-2020 and has emulator regression coverage. Broad refactor was not merged; small frequency lookup issue is fixed here. |
| `AP-AAiS/mGB_Hydra` | Builds on `tstirrat/mGB` and adds dynamic generator-to-MIDI-channel mappings plus a CC9 channel-remap command for 16-channel multi-Game-Boy playback. It notes that POLY mode is no longer practically reachable. | Main use case is covered here with runtime channel mapping, saved GB1/GB2/GB3 profiles, manual per-synth channels, and preserved POLY mode. CC-based remapping remains a possible future feature. |
| `eggstoastbacon/mGB` | Save-data experiments around `mgb_save.c` and memory functions. | Not merged. This fork now places save data in cartridge SRAM with validation and SameBoy save-recovery tests. |
| `greigs/mGB` / `catskull/mGB` | Removed or reshuffled historical source ZIP archives. | Not useful for this fork beyond the upstream archive commit now included. |

Most other direct forks of `trash80/mGB` were either identical to `trash80/master`, older snapshots, or documentation/archive-only changes.

## Future Candidate

CC-based runtime remapping from `mGB_Hydra` could be useful for DAW automation or hardware controllers. It should be added only if it can coexist with the current saved setup, per-synth channel rows, and POLY mode without adding live-performance ambiguity.
