# GBA Music Mangle: further information

Back to the [manual](../README.md).

## The sound engines

Not every game uses Nintendo's engine (MP2K, "Sappy"), and a cart can use more than one. The search
always looks for MP2K. It looks for **GAX**, **Krawall** and **GBAModPlay** when their names are
written in the cart, and for **MusyX** (which leaves no name) when nothing else turns up. A cart that
names one of them skips the MP2K backup search and MusyX, so its search is quicker. SET > RESCAN CART
can be switched to FULL (LEFT/RIGHT) to look for every engine whatever the cart says.

Each engine's songs are a **song table** of their own, so a cart with MP2K and GAX shows two
(SET > SONG TABLE switches between them; a two-games-in-one cart also has one per game). The result
screen lists the engines found ("MP2K + GAX"), and the PLAY page shows the engine beside the song.

GAX, Krawall, MusyX and GBAModPlay are **previews**: some songs may be incomplete or wrong. Everything
works on them as on MP2K (cue and PLAY, the clock, REPEAT BAR, every mangle, CHAOS) except MP2K's own
envelopes, note length, reverb and LFO, which show N/A. On GAX, Krawall and GBAModPlay, RESTART BAR
cuts the sounding notes at the jump, and so does GAX's and Krawall's RESTART LOOP; MusyX's and
GBAModPlay's RESTART LOOP restart the song. A Krawall module can hold several songs: each is listed
on its own.

| Engine | Games (examples) |
|---|---|
| MP2K | most GBA games, Nintendo's and many others |
| GAX | Shin'en's engine: Finding Nemo, Tang Tang, Tony Hawk's Pro Skater 3, Spider-Man: Mysterio's Menace, Bruce Lee |
| Krawall | Lemony Snicket, The Chronicles of Narnia, many European games |
| MusyX | Factor 5's: Lady Sia, Denki Blocks!, BattleBots, Go! Go! Beckham!, The Incredibles, Harry Potter |
| GBAModPlay | Logik State's: Super Puzzle Fighter II Turbo, Driv3r, Need for Speed Underground 2 and Carbon (and Medal of Honor: Infiltrator, Lego Bionicle ...) |

### "No music found that it can play"

When no engine's songs are found, the screen says what the cart uses instead, and waits for a button:

| Screen shows | Meaning |
|---|---|
| ENGINE: GAX | Shin'en's GAX engine. The line below gives its version. |
| ENGINE: KRAWALL | The Krawall tracker engine (XM/S3M modules). |
| ENGINE: XM x*n* | *n* whole FastTracker 2 modules stored in the cart (these don't play). |
| ENGINE: GBAMODPLAY | Logik State's driver. |
| ENGINE UNKNOWN | No known signature: an in-house engine, or one that leaves no text behind. |

A named engine with no songs found adds a red line saying why ("GAX: NO SONGS MATCHED", "KRAWALL: NO
SAMPLE BANK", "MODPLAY: NO SAMPLE BANK" ...). If a game you know uses MP2K reports nothing, try
SET > CART SPEED SAFE and SET > RESCAN CART.

### The CAL page's engine row

Note it when reporting a song:

| Engine | Shows | Means |
|---|---|---|
| GAX | `S` songs, `OLD` how many a stricter search would have kept, `FX` effect banks, then the check that turned away the most near-misses (`SHP` shape, `NOT` notes, `INS` instruments, `SMP` samples, `CHN` channels); a GAX 2 cart shows `V2`. A `U` group lists what a song asked for that the player skipped (pattern effects, perf-list effects; features 1 ping-pong, 2 wave modulation). | Where songs went, and what didn't play |
| Krawall | `M` modules, `S`/`I` the sample and instrument banks found, unknown effects, error | Two more rows choose its **sample and instrument banks** (stored apart from the songs; the search takes the largest of each). UP/DOWN picks the row, LEFT/RIGHT steps through the candidates; the choice is saved with the patch. |
| MusyX | unknown commands, `W` warnings (08: all voices busy; 20: a note outside its drum kit), `R` why notes were turned away (2 no instrument, 4 bad sample, 8 bad macro or keymap), error | |
| GBAModPlay | `M` modules, `S` the sample bank's instruments, `U` effects skipped, error | |

The CAL page also shows **DSP** (the sound's share of the CPU) and **SVC** (the share of each
millisecond the modulator and clock take).

How the engines fit together: [ENGINES.md](ENGINES.md). The MusyX driver as read from the carts:
[MUSYX_DRIVER.md](MUSYX_DRIVER.md).

## Songs, jingles and effects in detail

| Kind | What it is | Plays |
|---|---|---|
| SONG | anything that loops | as a song |
| JNGL (jingle) | longer than four bars, and ends | once, as a song |
| SFX | shorter than four bars, and ends (on MP2K also a short loop on an effects player) | once; every PLAY restarts it at once, even with RESTARTS = AT BAR END |

MP2K games also file their sounds under "music players", and the PLAY page shows an MP2K song's
player (P0 to P9), but it doesn't decide the list. Changing the list stops what's playing and cues its
first item. Pokémon Ruby has over 400 songs, and the first 250 or so are effects: hence the MUSIC
list by default.

Choosing an effect: L/R, the SONG CV (up to 16 either side), or a button STEPping SONG. Firing it:
A on the song number, Audio In 1, the switch down, or any gate or button mapped to PLAY.

## The sound destinations (MP2K)

| Destination | Does |
|---|---|
| ATTACK | Up: notes swell in slowly. Down: snappier. |
| DECAY | Up: notes take longer to fall to their sustain level. Down: shorter. |
| SUSTAIN | Up: held notes stay louder. Down: plucky. |
| RELEASE | Up: long tails that ring over each other. Down: notes cut dead. |
| NOTE LENGTH | Up: legato, up to ×4. Down: staccato, down to ¼. |
| REVERB | Adds to the song's own reverb. Down removes it. |
| VIBRATO | Wobbles the pitch of every track; left of noon a square wobble (a trill). |
| TREMOLO | Wobbles the volume; left of noon a square wobble (a gate). |
| LFO SPEED | The vibrato and tremolo rate, ×¼ to ×4 around 5 Hz. |

The envelope changes act on notes already sounding, so holding a button on RELEASE stretches the
tails of whatever is playing.

## Buttons in detail

Each BTN button can **HOLD** a mangle while pressed, **LATCH** it on and off, or **STEP** its BASE
value. It can also fire a clock, PLAY, STOP, a reset, the next song (a cue), RESTART LOOP, RESTART
BAR, REROLL CHAOS or CLEAR MANGLES. A STEP on SONG moves the cue by its amount, so a button can browse
a long list. Perform mode turns auto-repeat off: a held button is a HOLD.

BASE: LEFT/RIGHT change a value by 1; hold A and UP/DOWN to change it by one table entry for TABLE
OFFSET, 12 for TRANSPOSE (an octave), 10 for everything else; A+B puts it back to zero.

TABLE OFFSET: a CV or button moves it a whole entry of the game's table at a time, one entry a
semitone (1V/oct at depth 63), so every step is another instrument. BASE moves it a byte at a time,
which reads the table between its entries: mostly silence, sometimes a strange sound. The PLAY page's
TBL shows the offset in bytes. An entry is 12 bytes on MP2K and GBAModPlay's 2002 games (16 on its
later ones) and 4 on MusyX, GAX and Krawall.

## Saved slots in detail

A slot saves the maps, base values and settings, and the song table, list, song and mutes (restored
when loaded with the same game inserted). Each save also remembers which cart it was made on and
where that cart's music is. Booting with the same cart in (same game and revision), the GBA loads the
lowest-numbered slot saved on it and cues its song in about a second. Hold **B** while it says
CHECKING SAVED SLOTS to search instead; SET > RESCAN CART always searches.

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

The manual's screenshots are rendered by running the real payload under emulation, with a synthetic
cart: `python -I tools/pcplay/mkdemorom.py tools/pcplay/build/demo.gba`, then
`cd payload && python screenshots.py` (`pip install unicorn pillow`).

## Testing

```sh
python -I tools/m4a_scan.py yourgame.gba --songs     # what the GBA's search should find
tools/pcplay/build.sh                                # needs a C compiler; `pip install ziglang` works
python -I tools/pcplay/mktestrom.py test.gba         # a synthetic MP2K cartridge
tools/pcplay/build/pcplay test.gba --songs           # list songs (--songtable N, --list 0/1/2)
tools/pcplay/build/pcplay test.gba --song 0 --secs 10 --out song.wav
tools/pcplay/build/pcplay test.gba --fuzz 6          # every song, random mangles, bounds-checked
python -I engines/musyx/tests/make_demo.py msdemo.gba  # a small authored MusyX cart
python -I tools/pcplay/mkengines.py eng test.gba     # synthetic GAX, Krawall, GBAModPlay, MP2K + GAX carts
tools/pcplay/build/modtest test.gba msdemo.gba eng/gax.gba eng/krawall.gba eng/mixed.gba eng/krawall2.gba eng/gax2.gba eng/lsplay.gba eng/lsplay2.gba
tools/pcplay/build/pcplay yourgame.gba --full --songs   # every engine, as RESCAN FULL
```
`pcplay` renders a song with the payload's own engine, the Game Boy channels through a simple
emulation (`--no-psg` leaves them out). Use your own cartridge dumps. The player's interface and
rules: [PLAYER_SPEC.md](PLAYER_SPEC.md).

## Licence

MIT, see [LICENSE](../LICENSE), except the Krawall player: `engines/krawall/krawall.c` and
`engines/krawall/kr_tables.c`/`.h` are ported from Sebastian Kienzl's Krawall
([github.com/sebknzl/krawall](https://github.com/sebknzl/krawall)) and stay under the GNU Lesser General
Public License 2.1, see [engines/krawall/COPYING.LGPL](../engines/krawall/COPYING.LGPL). The firmware and
payload may be shared freely; whoever hands on a built UF2 must also offer the source of those files
(this repository does), so anyone can rebuild it with their own changes.
