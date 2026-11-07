# Licences and credits

GBA Music Mangle is © 2026 Andy Jenkinson (uglifruit) and released under the **MIT License**
([LICENSE](LICENSE)), except for the parts listed below, which keep their own terms.

No game data is included in this repository or in the firmware. The card reads the music from the
cartridge inserted in the GBA, while it plays.

## In brief

| Part | Licence | Holder |
|---|---|---|
| Everything not listed below | MIT | Andy Jenkinson |
| `ComputerCard.h` | MIT | Chris Johnson |
| `engines/krawall/krawall.c`, `kr_tables.c`, `kr_tables.h` (the Krawall player) | LGPL-2.1 | Sebastian Kienzl |
| `pico_sdk_import.cmake`, and the Raspberry Pi Pico SDK built into the firmware | BSD-3-Clause | Raspberry Pi (Trading) Ltd. |
| `payload/m4a_tables.c`, `payload/cgb.c` (Nintendo's MP2K tables and Game Boy channel routines) | none: see [Nintendo's sound library](#nintendos-sound-library) | Nintendo |
| `payload/gbafix.py`'s logo bytes | none: see [The Nintendo logo](#the-nintendo-logo) | Nintendo |
| Data taken from `BinarySerializer.Audio.GBA` and `shinen-gax-python` | MIT | see [Data from other projects](#data-from-other-projects) |

## Sharing the firmware

The firmware (`UF2/gba_link.uf2`) and the GBA software it carries include the Krawall player,
which is under the GNU Lesser General Public License 2.1. Anyone may share the UF2. Whoever hands it
on must also offer the source of those files: pointing to this repository does that. Anyone can
then rebuild it with their own changes. The licence text is in
[engines/krawall/COPYING.LGPL](engines/krawall/COPYING.LGPL).

## Included code

**ComputerCard** (`ComputerCard.h`), Chris Johnson's library for the Workshop Computer:

> MIT License
>
> Copyright (c) 2024-6 Chris Johnson
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software without restriction,
> including without limitation the rights to use, copy, modify, merge, publish, distribute,
> sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
> NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
> OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
> CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**The Krawall player** (`engines/krawall/krawall.c`, `kr_tables.c`, `kr_tables.h`) is ported from
Sebastian Kienzl's Krawall ([github.com/sebknzl/krawall](https://github.com/sebknzl/krawall)): its
`lib/player.c` and `player_effects.h` re-expressed over bounds-checked reads, and its tables. These
files are under the GNU Lesser General Public License 2.1
([engines/krawall/COPYING.LGPL](engines/krawall/COPYING.LGPL)); each file says so.

**The Raspberry Pi Pico SDK** is built into the firmware, and `pico_sdk_import.cmake` is a copy of
the SDK's own file:

> Copyright 2020 (c) 2020 Raspberry Pi (Trading) Ltd.
>
> Redistribution and use in source and binary forms, with or without modification, are permitted
> provided that the following conditions are met:
>
> 1. Redistributions of source code must retain the above copyright notice, this list of conditions
>    and the following disclaimer.
> 2. Redistributions in binary form must reproduce the above copyright notice, this list of
>    conditions and the following disclaimer in the documentation and/or other materials provided
>    with the distribution.
> 3. Neither the name of the copyright holder nor the names of its contributors may be used to
>    endorse or promote products derived from this software without specific prior written
>    permission.
>
> THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
> IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
> FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
> CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
> DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
> DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
> IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
> THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

The firmware is also linked with the Arm GNU Toolchain's runtime libraries (newlib's C library and
libgcc), under their own permissive licences and the GCC Runtime Library Exception. The GBA software
is built without them.

## Nintendo's sound library

The MP2K ("Sappy", m4a) player is written to behave as Nintendo's sound library does, using pret's
decompilation of Pokémon Emerald ([github.com/pret/pokeemerald](https://github.com/pret/pokeemerald))
as the reference. Two files go further than describing that behaviour:

- `payload/m4a_tables.c` holds the library's lookup tables (scale, frequency, clock and Game Boy
  channel tables), transcribed from pret's `src/m4a_tables.c`;
- `payload/cgb.c` is a port of the library's Game Boy channel routines (`CgbSound`,
  `MidiKeyToCgbFreq`) from pret's `src/m4a.c`.

These are Nintendo's work as reconstructed by pret, whose projects are published without a licence.
They are **not** covered by this repository's MIT grant. They are included so that cartridges that
use the library play as they were written to.

## The Nintendo logo

The GBA's BIOS will only start software whose header carries Nintendo's logo bitmap.
`payload/gbafix.py` writes those 156 bytes into the GBA software's header at build time, as
devkitPro's `gbafix` does. The logo is Nintendo's, and it is used here only because the hardware
requires it.

## Data from other projects

Two files take data, not code, from MIT-licensed projects:

- `engines/musyx/macro_lengths.h`: the byte lengths of MusyX's macro commands, from the field
  layouts of BinarySerializer.Audio.GBA
  ([github.com/BinarySerializer/BinarySerializer.Audio.GBA](https://github.com/BinarySerializer/BinarySerializer.Audio.GBA)),
  which also served as the reference for MusyX's data layout. MIT License, Copyright (c) 2021
  BinarySerializer.
- `engines/gax/gax.c`: GAX 3's pitch base and replayer behaviour follow shinen-gax-python
  ([github.com/beanieaxolotl/shinen-gax-python](https://github.com/beanieaxolotl/shinen-gax-python)).
  MIT License, Copyright (c) 2024 beanie_axolotl.

Both are under the MIT terms quoted above for ComputerCard, with their own copyright lines.

## References

These projects documented the formats and rules that parts of this program follow. None of their
code is included; the program re-implements what they describe.

| Project | Licence | Used for |
|---|---|---|
| [saptapper](https://github.com/loveemu/saptapper) (loveemu) | LGPL-3.0 | The byte signature of the MP2K library's song-select routine (Nintendo's own code bytes), and where its song table sits |
| [agbplay](https://github.com/ipatix/agbplay) (ipatix) | LGPL-3.0 | The idea of the fallback search for song tables |
| [gaxtapper](https://github.com/loveemu/gaxtapper) (loveemu) | GPL-3.0 | GAX's song header rules |
| [UnkrawerterGBA](https://github.com/MCJack123/UnkrawerterGBA) (JackMacWindows) | MIT | How Krawall's sample and instrument banks are found |
| Krawall's converter, krawerter (in Krawall's repository) | LGPL-2.1 | Krawall's bank layouts |
| [The Cutting Room Floor](https://tcrf.net/) | | Which engines leave which text in a cartridge |

GBAModPlay (Logik State), GAX 2, MusyX's driver behaviour and the other engines' playback were
worked out by reading the drivers inside cartridges. That code is this project's own and is under
the MIT License.

"Game Boy Advance" and "Nintendo" are trademarks of Nintendo. GAX, Krawall, MusyX and GBAModPlay are
the names of their makers' sound engines. None of these companies or people is connected with this
project.
