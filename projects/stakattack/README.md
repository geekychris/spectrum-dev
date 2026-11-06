# StakAttack (Spectrum Next)

Port of `amiga_mcp/examples/stakattack`: seven gem pieces fall into a 12x19
well; rotate and slide them to complete rows. 100/300/500/800 points times
the stage for 1-4 rows, 1 a row for soft drop, 2 a row for a slam; every 10
rows the stage goes up and pieces fall faster. The game ends when a new
piece can't enter the well.

![title](screenshots/title.png) ![play](screenshots/play.png)

![StakAttack playing](../../docs/images/stakattack.gif)

## Controls

| | |
|---|---|
| move | O / P, Sinclair, Kempston (hold: auto-repeat after 10 frames, as the original) |
| rotate | Q (up), or Z |
| soft drop | A (down) |
| slam (hard drop) | SPACE / fire |
| pause | ENTER |
| start | fire on the title |

## What's where

`src/game.c` is the original's logic: pieces, rotation with wall kicks,
delayed auto-shift, lock delay, line clear animation, scoring, stages, the
LCG for the piece sequence. Only `piece_fits()` is rewritten (same test,
pointer walks; the original is kept under `#if 0`). Port changes are marked
`port:`.

`src/main.c` is the Next front end:

- **Layer 2** (256x192, 8 bpp, double buffered) for the well, the HUD and the
  screens, in the original's 16 colours and 5x7 font (`src/font.c`). The
  12-pixel cells become 9-pixel gems. Each buffer remembers what it shows,
  so a frame redraws only the cells that changed. The title and the empty
  game screen are drawn once into banks of their own; changing screens is a
  DMA copy (no lost frames, against 17 when they were redrawn in C).
- **Hardware sprites** for the falling piece, its landing marks and the next
  piece (12 sprites, 8 patterns).
- **TurboSound**: the original's ProTracker MOD (`tools/stakattack.mod`,
  converted by `sdk/tools/mod2ay.py` (`tools/make_music.sh`) into `src/music.c`, played by the SDK's `modplay`) on two AY chips: lead
  and bass on AY 0, drums (tone-sweep kick, noise snare and hat) and the
  stab/bell channel on AY 1; the drop/lock/clear effects on AY 0's third
  channel.
- **28 MHz**, 50 Hz logic as on the Amiga (one `game_update` per frame).

Additions: a high-score table on the title and initials entry after a game
(SDK `hs_*`), Z as a second rotate key. The pause and game-over boxes
cover the well, as in the original; the pieces are sprites, so they're
hidden while paused.

## Next features used

What it uses beyond a 48K/128K Spectrum, and why:

| feature | used for | why it's needed |
|---|---|---|
| **Layer 2** (256 x 192, 8 bits a pixel, double buffered in banks 9-14) | the well, the HUD, the screens, in the original's 16 colours | the 48K screen has two colours per 8 x 8 cell: 9-pixel gems in 16 colours side by side would clash everywhere |
| **hardware sprites** (12 of 128, 8 patterns) | the falling piece, its landing marks, the next piece | they move every frame without redrawing the well; only changed cells are ever drawn into Layer 2 |
| **palettes** (9-bit RGB) | the Amiga's 12-bit colours, for Layer 2 and the sprites | the original's colours, not the Spectrum's 15 |
| **28 MHz** | the game logic, the C drawing | zsdcc's code is slow; at 3.5 MHz the cell redraws and the ghost search (up to 19 collision tests a frame) wouldn't fit a frame |
| **TurboSound** (3 AY chips) | the original's four-channel MOD on two chips (`modplay`), the effects on a third channel | one AY has three channels; the MOD needs four, plus one for the effects |
| **MMU banking** | the title and the empty game screen kept in banks 18-23 | 48K each; a 64K address space can't hold them |
| **zxnDMA** | changing screens: one of those copied into the back buffer | about 2 T-states a byte: no lost frame (17 when redrawn in C) |
| **Next registers** (`NEXTREG`) | the above, set up in a few instructions | |

Not used: the **tilemap** (the cells are 9 pixels, not 8), the **copper**
(nothing changes mid-screen), **Z80N instructions** other than what z88dk's
Next library uses (`NEXTREG`, `MUL` in its multiplies).

## Test

`./zx test stakattack`: a bot reads `gs` from memory and places each piece
with a classic stacking heuristic (holes, height, bumpiness, rows), steering
with the keys. It clears 10+ rows (stage 2), pauses and resumes once, then
slams pieces into the middle until the stack tops out (the original has no
lives: topping out is the game's only way to lose). It enters BOT in the
high-score table and returns to the title. The game is recorded and replayed
twice, every frame hash checked. `--debug` prints the bot's plans.
