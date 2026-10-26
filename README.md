# GBA Cart Player

A card for the Music Thing Modular **Workshop Computer**. It boots a Game Boy Advance over the link
cable while a game cartridge stays inserted. The GBA finds the game's own music, and plays it so your
modular can drive it: clock it, restart it, strip its tracks back, transpose it, and mangle its
instruments and samples. This works with any game that uses Nintendo's MP2K ("Sappy") sound engine,
which most GBA games do.

Siblings: [89_GBA](https://github.com/uglifruit/89_GBA) (GBA PSG Voice) and
[GBA_FM](https://github.com/uglifruit/GBA_FM) (GBA FM Voice). All three use the same cable and
link.

**Status: beta, not yet tested on hardware.** The engine has been verified on the PC (see
"Testing").

## Starting up

1. Connect the GBA's link port to the Workshop: Pulse Out 1 → SC, Pulse Out 2 → SI,
   Pulse In 1 ← SO, and ground. This is the same cable as the sibling cards.
2. Put a game cartridge in the GBA.
3. **Hold START + SELECT while you switch the GBA on.** This tells the GBA to skip the cartridge
   and wait for the Workshop. The Workshop's LEDs show upload progress (about 7 seconds).
4. The GBA scans the cartridge for music, shows how many songs it found, and starts song 1.

Use an original or reproduction cartridge. Flash carts usually show their own menu at this point
rather than a game, so they will normally report no music.

## Playing

| GBA button | On the PLAY page |
|---|---|
| L / R | Previous / next song |
| Up / Down, Left / Right | Choose a track |
| A | Mute the chosen track |
| B | Solo the chosen track |
| SELECT | Restart the song |
| START | Open the editor (MAP, SET, MEM, CAL pages; L/R moves between them, START returns) |

The PLAY page shows the song number, the clock, the bar and beat, all sixteen tracks (activity,
current note, mute and solo), and the live value of every mangle.

## The panel (factory settings; every input can be re-assigned on the MAP page)

| Input | Does |
|---|---|
| Pulse In 2 | **Clock.** Set the PPQN on the SET page (1, 2, 4, 8, 12, 24). With no clock patched, songs play at their own tempo. |
| Audio In 1 | **Reset:** restart the song, in step with the clock |
| Audio In 2 | **Sample start:** start every sample further in |
| CV In 1 | **Voice shift:** every instrument swapped for one further along the game's instrument table |
| CV In 2 | **Transpose,** 1 V/octave, in semitones |
| Main knob | **Layers:** turning it up strips tracks away from the top down |
| Knob X | **Tempo** (when not externally clocked), ×0.25 to ×4 |
| Knob Y | **ROM scrub:** samples are read from elsewhere in the cartridge, a little at first, then a lot |
| Switch up | Bypass every mangle |
| Switch down | Restart the song |

| Output | Carries |
|---|---|
| CV Out 2 | The followed track's pitch, 1 V/octave (choose the track on the SET page) |
| CV Out 1 | The followed track's gate |
| Audio Out 1 | A pulse on every beat of the song |
| Audio Out 2 | A pulse each time the song loops |

Audio comes out of the GBA's own headphone socket.

Other things the MAP page can assign: next song, song select by CV, table offset (reads the
instrument table from the wrong place), solo, fine pitch, reverse (a chance each note plays
backwards), and crush.

The MEM page saves the map and settings to the Workshop's 16 memory slots. A saved slot also
remembers the song and mutes, and restores them when loaded with the same game inserted.

## Building

RP2040 firmware (produces `build/gba_link.uf2`):
```sh
cmake -G Ninja -B build -S .
cmake --build build
```
The GBA payload (regenerates `gba_payload.h`; rebuild the firmware afterwards):
```sh
cd payload && ./build.sh
```
Both use the Pico SDK's toolchain. No devkitPro is needed.

## Testing

```sh
python -I tools/m4a_scan.py yourgame.gba --songs     # what the GBA's scan should find
tools/pcplay/build.sh                                # needs a C compiler; `pip install ziglang` works
python -I tools/pcplay/mktestrom.py test.gba         # a synthetic MP2K cartridge
tools/pcplay/build/pcplay test.gba --song 0 --secs 10 --out song.wav
tools/pcplay/build/pcplay test.gba --fuzz 6          # every song, random mangles, bounds-checked
```
`pcplay` renders a song with the payload's own engine. The PSG voices (squares, wave, noise) are
silent in it. Use your own cartridge dumps.

The briefs and notes: [docs/PLAYER_SPEC.md](docs/PLAYER_SPEC.md) (the player's interface and
rules).

## Licence

MIT, see [LICENSE](LICENSE).
