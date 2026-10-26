# GBA Cart Player (work in progress)

A card for the Music Thing Modular **Workshop Computer**. It boots a Game Boy Advance over the link
cable (BIOS Multiboot, as in its siblings
[89_GBA](https://github.com/uglifruit/89_GBA) and [GBA_FM](https://github.com/uglifruit/GBA_FM))
while a commercial cartridge stays inserted. The GBA then scans that cartridge for the music data of
Nintendo's MP2K ("Sappy"/m4a) sound engine and plays its songs through a new player. The modular
clocks the player and mangles it: external clock, instrument and sample-pointer shifting, and track
muting.

**Status: unfinished and does not build yet.** It won't build until the player exists.

## For the developer writing the player

Your brief is **[docs/PLAYER_SPEC.md](docs/PLAYER_SPEC.md)**. It names the three files you write
(`payload/player.c`, `payload/cgb.h`, `payload/cgb.c`), the frozen interface they implement
(`payload/player.h`), the build rules, and the acceptance checks.

## What exists

| Area | Files | State |
|---|---|---|
| RP2040 firmware (multiboot, SPI link, patch storage) | `main.cpp`, `gba_link.*`, `gba_multiboot.*`, `gba_spi.*`, `patch_store.*`, `ComputerCard.h` | From GBA_FM, still unchanged. The pulse-output edit is to do. |
| Wire protocol, shared by both sides | `gba_proto.h` | Adds `GBA_UP_PULSE` (beat/loop counters). |
| GBA link, IRQ, graphics, diagnostics | `payload/link.*`, `irq.*`, `gfx.*`, `diag.*`, `crt0.s`, `multiboot.ld` | From GBA_FM. `link.c` sends `GBA_UP_PULSE`. |
| Cartridge access | `payload/rom.*` | Written |
| MP2K format and tables (from pret's pokeemerald) | `payload/m4a.h`, `m4a_tables.c` | Written |
| Song-table scanner | `payload/scan.*`, `tools/m4a_scan.py` (PC mirror) | Written. The Python mirror is tested on a synthetic ROM. |
| Sample mixer and Direct Sound output | `payload/mixer.*`, `audio.*` | Written |
| Sequencer and voices | `payload/player.h` (interface), `player.c`, `cgb.*` | **Interface only. See the brief.** |
| Clock, mod matrix, UI, main, PC test harness | — | To do, after the player |

## Checking a cartridge dump on the PC

```sh
python -I tools/m4a_scan.py yourgame.gba --songs
```

This prints what the GBA's scan should find: the song table's address, the song count and each
song's track count. Use your own cartridge dumps.

## Licence

MIT, see [LICENSE](LICENSE).
