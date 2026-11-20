# Dreamcast models: notes

Work in progress: Raziel's and Kain's Dreamcast models, drawn in the port
with their own 256 x 256 textures.

## Trying it

1. `python tools/dc_import.py "<path to the .gdi>" build/Debug/dreamcast`
   writes `raziel.pcm`, `kain.pcm` and the `texNNNN.vq` textures they use.
2. Build and run; the log says `Dreamcast model for raziel: ...`. Without
   the `dreamcast` folder the PlayStation models are used.

## What the Dreamcast disc holds

- `BIGFILE.DAT`: same layout as the PlayStation one (count, then hash, size,
  offset, checksum). Objects and areas are `.pcm` (extension table pcm, crm,
  tim, pmp, pnd, pmf, pnf). The name hash did not match the game's
  LOAD_HashName; the two objects are found by fixed hashes (raziel
  0x90850620, kain 0x797909D0), checked by the object name inside.
- Object files: the game's relocation table and Object/Model layout.
  Raziel 510 vertices / 1222 faces (PSX 299 / 640), Kain 449 / 1080
  (PSX 335 / 652); same segments (66, 49), so the PSX animations drive them.
- Texture records (16 bytes): three UVs as the top halves of floats (0-1),
  then a 16-bit texture number (0x2000 bit: probably "use alpha").
- `TEXTURES.VQ`: no index. Texture n is the 0x4800-byte slot at n * 0x4800
  (loader at 0x8C1278E0 in 1ST_READ.BIN): 256 x 256 VQ, 2048-byte ARGB1555
  codebook of 2x2 blocks, 128 x 128 indices in Morton order with x in the
  EVEN bits (vqdecode.py: set VQ_TRANSPOSE=1 for that order).

## How the port draws them (port/src/dc_models.c)

- Loaded after STREAM_PackVRAMObject (fixup in STREAM.c); the model replaces
  the object's model. It lives in a new 2 MB area at 0x80400000
  (port_extra_alloc): DRAW_AnimatedModel_S tells texture pointers from
  colours by their sign (bgtz), so data must sit at 0x80xxxxxx.
- Records are rewritten as PSX TextureMT3: UVs 0-255, tpage = 0x8000 |
  16-bit mode | slot. PsyCross's AddSplit (fixup) draws tpage bit 15 from the
  port's RGBA texture for that slot; the RGBA shader discards alpha 0.
- MEMPACK relocation and AdjustVramCoordsObject skip the external model.

## Open problems

- Holes behind Raziel's right knee. Ruled out: flag 0x10 faces (doubled,
  no change), cull threshold in DRAW_AnimatedModel_S, alpha, draw order
  (sortPush), wrong winding, missing faces (both legs closed and
  symmetric). Current try: also double every face joining two segments
  (1685 faces) - with that, the game exited by itself after ~120 frames
  ("exit() called"); doubling every face (2444) made Raziel vanish. Likely a
  limit on faces or primitives per model to find first.
- The 0x2000 texture flag (probably alpha / translucency) is not used yet.
- Kain was not seen in game (no area spawns him); a -spawn test option hung
  when called on the first gameplay frame.
- Raziel's DC faces lack flag 0x1 (PSX ones have it): a different lighting
  path in DRAW_AnimatedModel_S.

## Scripts here

gdiread.py (GDI file system), bigfile.py, findentry.py, hashprobe.py,
objinfo.py, texuse.py, vqdecode.py, uvcheck.py (UVs drawn on a texture),
uvorder.py, winding.py, facenormals.py, flipped.py, sh4dis.py (minimal SH-4
disassembler for 1ST_READ.BIN), strings.py. They expect the files extracted
next to them (BIGFILE.DAT, TEXTURES.VQ, 1ST_READ.BIN, dc_raziel.pcm, ...).
