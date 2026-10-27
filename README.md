# GBA Music Mangle

A card for the Music Thing Modular **Workshop Computer**. It boots a Game Boy Advance over the link
cable while a game cartridge stays inserted. The GBA finds the game's own music, and plays it so your
modular can drive it: clock it, restart it, loop its bars, strip its tracks back, transpose it, and
mangle its instruments, samples and envelopes, up to per-note CHAOS. This works with any game that
uses Nintendo's MP2K ("Sappy") sound engine, which most GBA games do.

Siblings: [89_GBA](https://github.com/uglifruit/89_GBA) (GBA PSG Voice) and
[GBA_FM](https://github.com/uglifruit/GBA_FM) (GBA FM Voice). All three use the same cable and
link.

**Status: beta.** Booting with a cartridge in, the scan and playback have been tried on hardware
(Pokémon Ruby). Everything is verified on the PC (see "Testing"); the newest features have not all
been played on hardware yet.

## Starting up

1. Connect the GBA's link port to the Workshop: Pulse Out 1 → SC, Pulse Out 2 → SI,
   Pulse In 1 ← SO, and ground. This is the same cable as the sibling cards.
2. Put a game cartridge in the GBA.
3. **Hold START + SELECT while you switch the GBA on.** This tells the GBA to skip the cartridge
   and wait for the Workshop. The Workshop's LEDs show upload progress (about 10 seconds).
4. The GBA scans the cartridge for music, shows how many songs it found, and starts the first piece
   of music (sound effects are listed separately; see "Song tables and sound effects").

Use an original or reproduction cartridge. Flash carts usually show their own menu at this point
rather than a game, so they will normally report no music.

### "No MP2K music found"

Not every game uses Nintendo's engine. When the scan finds no MP2K music, it says what it found
instead, and waits for a button:

| Screen shows | Meaning |
|---|---|
| ENGINE: GAX | Shin'en's GAX engine, used by many Western third-party games. The line below gives its version. |
| ENGINE: KRAWALL | The Krawall tracker engine (XM/S3M modules), common in European games. |
| ENGINE: XM x*n* | *n* whole FastTracker 2 modules stored in the cart, with the first one's name. |
| ENGINE UNKNOWN | No known signature: an in-house engine, or one that leaves no text behind. |

None of these play yet: each would need its own engine. If a game you know uses MP2K reports
nothing, try SET > CART SPEED SAFE and SET > RESCAN CART. The CAL page repeats the engine line.

Game Boy and Game Boy Color carts can't be used. They switch the GBA into its Game Boy Color mode
at power-on, so there's nothing for the Workshop to talk to.

## Playing

| GBA button | On the PLAY page |
|---|---|
| L / R | Previous / next song |
| Up / Down, Left / Right | Choose a track |
| A | Mute the chosen track |
| B | Solo the chosen track |
| SELECT | **Perform mode** on/off (below) |
| START | Open the editor (MAP, BTN, BASE, SET, MEM, CAL pages; L/R moves between them, START returns) |

The PLAY page shows the song number, the clock, the bar and beat, all sixteen tracks (activity,
current note, mute and solo), and the live value of every mangle.

### Mangling from the GBA itself

You don't need anything patched to mangle a song:

- **Perform mode** (SELECT on the PLAY page) hands the GBA's buttons over to the **BTN** page's
  mappings. The bottom of the screen shows what each button does. The factory buttons:

  | Button | Does |
  |---|---|
  | A | Restart the bar (tap it for a stutter) |
  | B | Reroll the chaos pattern |
  | L / R | Reverse / crush, while held |
  | Up / Down | Transpose up / down a semitone |
  | Left | Clear the mangles (tempo and pitch stay) |
  | Right | More chaos (each press adds some; Left clears it) |

  Each button can **HOLD** a mangle while pressed, **LATCH** it on and off, or **STEP** its base
  value. It can also fire a clock, a reset, the next song, **RESTART LOOP**, **RESTART BAR**,
  **REROLL CHAOS** or **CLEAR MANGLES**.
- The **BASE** page sets each mangle's resting value in its own units: voice shift in programs,
  transpose in semitones, layers in tracks stripped, and so on. CV, knobs and buttons all add on
  top. The page scrolls: there are 21 values.
- **CLEAR MANGLES** (assign it to a button on the BTN page, or to a CV on MAP) puts every BASE
  value back to zero and switches off every latched button. It leaves tempo, transpose and fine
  pitch alone, because those are where the song sits rather than how it's mangled. It can't move a
  knob or a CV, so anything they're doing carries on.

### Mangling the sound itself

These reach into the engine's own controls, on every voice (samples and Game Boy channels alike):

| Destination | Does |
|---|---|
| ATTACK | Up: notes swell in slowly. Down: snappier. |
| DECAY | Up: notes take longer to fall to their sustain level. Down: shorter. |
| SUSTAIN | Up: held notes stay louder (organ-like). Down: plucky. |
| RELEASE | Up: long tails that ring over each other. Down: notes cut dead. |
| NOTE LENGTH | Up: legato, up to ×4. Down: staccato, down to ¼. |
| REVERB | Adds to the song's own reverb (the engine's echo). Down removes the song's. |
| VIBRATO | Wobbles the pitch of every track. A small amount (4–10) is a normal vibrato. |
| TREMOLO | Wobbles the volume of every track. |
| LFO SPEED | The vibrato and tremolo rate, ×¼ to ×4 around the engine's usual 5 Hz. |

The envelope changes act on notes already sounding, so holding a button (say B on RELEASE, HOLD,
amount 40) stretches the tails of whatever is playing. None of these are in the factory map;
assign them on MAP and BTN.

### Both sides of noon

Every mangle does something in both directions, from a knob either side of noon or a CV either side
of 0 V. These ones have a different effect on each side:

| Destination | Right of noon / positive | Left of noon / negative |
|---|---|---|
| START / END | Start later into each sample | Cut each sample's end off (a chop) |
| LAYERS | Strip tracks from the top | Strip tracks from the bottom |
| SOLO / MUTE | Solo one track, picked by the level | Mute one track, picked by the level |
| REVERSE/DROP | Chance a note plays backwards | Chance a note is dropped (thins it out) |
| CRUSH | Lower sample rate (gritty, aliased) | Fewer bits (noisy, down to one) |
| VIBRATO/TRILL | Smooth pitch wobble | Square wobble: a trill |
| TREMOLO/GATE | Smooth volume wobble | Square wobble: a rhythmic gate |
| CHAOS | A pattern that repeats every loop | Free chaos: new dice every time |

The factory main knob is CHAOS, so left of noon is free chaos and right is the repeating kind.

### Restarting: song, loop, bar

Three actions jump back, each one assignable to a button (BTN), a CV or gate (MAP), or both:

| Action | Jumps to |
|---|---|
| RESET | The start of the song. |
| RESTART LOOP | The song's loop point: most game music is an intro and then a looping body, and this skips the intro. A song that doesn't loop restarts instead. |
| RESTART BAR | The start of the current section. SET > RESTART BAR sets the section: 1 beat, 2 beats, 1 bar, 2 bars or 4 bars. |

**SET > RESTARTS** chooses when they happen: **NOW**, or **AT BAR END**, which holds them until the
next section line so they always land on the grid (the PLAY page shows QUEUED while one waits).
Pushing the switch down counts as a RESET too.

RESTART BAR fed from a clock divider (or tapped on a button) is a **beat-repeat**: the same bar
again and again while the triggers keep coming, and the song carries on when they stop. Notes
already sounding ring on through each jump, and CHAOS rewinds with it, so a repeated bar keeps its
chaos. With an external clock, all three line up with the clock pulse they arrive with.

### CHAOS

**CHAOS** rolls dice on every note. The higher it goes, the more often each thing happens and the
further it goes:

- **Any amount:** notes jump by fifths and octaves, and pan hard left or right.
- **More:** instruments are swapped for random ones from the game's set; samples play backwards
  or start partway in; notes are stretched or chopped.
- **Past halfway:** some notes play raw cart data (the game's code and graphics) as sound.

The dice restart every time the song loops, so chaos is a **pattern that repeats**, not noise: the
same note gets the same treatment each time round. **REROLL CHAOS** deals a new pattern. Try CHAOS on
the main knob or a CV, and REROLL on a button, then reroll until a pattern grabs you. CLEAR MANGLES
stops it, and so does the switch when SET > SWITCH UP is BYPASS.

### Song tables and sound effects

Some cartridges contain more than one set of music. A two-games-in-one cart has one per game. The
scan finds them all, and SET > SONG TABLE switches between them.

Sound effects are songs too, played on a different "music player". SET > SONG LIST shows ALL,
MUSIC or SFX. It starts on MUSIC. (Pokémon Ruby has over 400 songs, and the first 250 or so are
effects.) The PLAY page shows each song's player (P0 is the music). Effects are short, so they
behave as hits:
- With SET > SONG END on REPEAT, an effect loops.
- With STOP, each reset trigger plays it once, like a drum.

## The panel (factory settings; every input can be re-assigned on the MAP page)

| Input | Does |
|---|---|
| Pulse In 2 | **Clock.** Set the PPQN on the SET page (1, 2, 4, 8, 12, 24). With no clock patched, songs play at their own tempo. |
| Audio In 1 | **Reset:** restart the song, in step with the clock |
| Audio In 2 | **Restart bar:** from a clock divider, a beat-repeat |
| CV In 1 | **Voice shift:** every instrument swapped for one further along the game's instrument table |
| CV In 2 | **Transpose,** 1 V/octave, in semitones |
| Main knob | **Chaos:** right of noon a pattern that repeats each loop, left of noon free chaos |
| Knob X | **Tempo** (when not externally clocked), ×0.25 to ×4. Around noon is the song's own tempo. |
| Knob Y | **ROM scrub:** samples are read from elsewhere in the cartridge, a little at first, then a lot. Around noon is off. |
| Switch up | **Repeat bar:** the section playing loops while the switch is up; back to the middle and the song carries on. (SET > SWITCH UP can make it BYPASS, PAUSE or nothing.) |
| Switch down | Restart the song |

The knobs have a wide dead zone at noon, so a knob left roughly in the middle changes nothing.

| Output | Carries |
|---|---|
| CV Out 2 | The followed track's pitch, 1 V/octave (choose the track on the SET page) |
| CV Out 1 | The followed track's gate |
| Audio Out 1 | A pulse on every beat of the song |
| Audio Out 2 | A pulse each time the song loops |

Audio comes out of the GBA's own headphone socket.

Other things the MAP page can assign: next song, song select by CV, layers, start / end, table
offset (reads the instrument table from the wrong place), solo / mute, fine pitch, reverse / drop,
crush, the sound destinations above, RESTART LOOP, REROLL CHAOS, and CLEAR MANGLES (a gate clears
them).

The CAL page shows SVC, the share of each millisecond the modulator and clock take.

The MEM page saves the maps, base values and settings to the Workshop's 16 memory slots. A saved
slot also remembers the song table, list, song and mutes, and restores them when loaded with the
same game inserted.

**A saved slot also skips the scan.** Each save remembers which cart it was made on and where that
cart's music is. When you boot with the same cart in (same game and revision), the GBA finds that
slot, loads it, and starts playing in about a second instead of scanning. If several slots were
saved on the cart, the lowest-numbered one is used. Hold **B** while it says CHECKING SAVED SLOTS
to scan instead, and SET > RESCAN CART always scans.

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
tools/pcplay/build/pcplay test.gba --songs                  # list songs (--songtable N, --list 0/1/2)
tools/pcplay/build/pcplay test.gba --song 0 --secs 10 --out song.wav
tools/pcplay/build/pcplay test.gba --fuzz 6          # every song, random mangles, bounds-checked
tools/pcplay/build/modtest test.gba                  # the CV / button / base logic
python -I tools/pcplay/mkenginerom.py engines.gba    # a cart with no MP2K, only other engines' signatures
tools/pcplay/build/pcplay engines.gba                # should report GAX KRAWALL XM x2
```
`pcplay` renders a song with the payload's own engine. The PSG voices (squares, wave, noise) are
silent in it. Use your own cartridge dumps.

The briefs and notes: [docs/PLAYER_SPEC.md](docs/PLAYER_SPEC.md) (the player's interface and
rules).

## Licence

MIT, see [LICENSE](LICENSE).
