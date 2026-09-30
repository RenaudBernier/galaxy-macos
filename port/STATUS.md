# Port status

## Working

* The whole decompilation (Game, JSystem, nw4r, RVLFaceLib) compiles and links
  for arm64 macOS against the host runtime and Aurora.
* Boot, strap screen, title (with streamed music), file select, save data
  (host files), the prologue (picture book, Peach's letter) and gameplay, at a
  steady 60 fps, in a window of any shape (the view and HUD follow its
  aspect ratio).
* Save states: five slots in the States menu, lasting across launches (same
  build only).
* THP movies: video (baseline JPEG with THP's quirks) and audio; the prologue
  movie plays with subtitles. All nine movies on the disc decode cleanly
  offline (every frame of every movie).
* Audio: sound effects, sequenced music and streamed music through the DSP
  emulation and SDL3.
* Every galaxy and hub stage (47 stages, scenario 1) loads and runs its intro.
* Mario moves, jumps, spins, crouches, long/back-flips and collides with the
  ground (checked through scripted input).

## Known gaps

* Miis: the Mii library (RVLFaceLib) is disabled, so the file select shows a
  Mii error message once and only character icons are available.
* HOME Menu (PowerPC RSO module) is disabled.
* Z textures (`GXSetZTexture`) are not implemented in Aurora; the two uses
  that clear the depth buffer are handled by drawing the quad on the far plane.
* EFB readback (`GXPeekARGB`) is not implemented.
* One material enables a texture coordinate generator it never configures
  (Aurora logs "unhandled tcg src 21"); it gets zero coordinates.
* Save data is written in a mixed-endian layout (self-consistent, not
  compatible with Wii saves).
* Paired-single reciprocal/sqrt estimates (`frsqrte`, `fres`) use exact host
  math instead of the Broadway's table-based estimates.

## Not yet verified

Long play sessions and later-game content beyond each stage's first scenario:
boss fights, power star collection sequences, the ending.

## Testing aids

`SMG_INPUT_RECORD` records a session's inputs; `SMG_INPUT_SCRIPT` replays them
(or any scripted timeline) and `SMG_NAND_DIR` points the run at a scratch
save. See README.md.
