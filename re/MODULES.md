# Saga Game.exe: module map

The Cultures Saga `Game.exe` (build date 5 Nov 2003, MSVC6, no RTTI), as exported by Ghidra into `re/export/saga`.
MSVC places functions in source-file order, so each module is a contiguous address range. The ranges below come from
the strings each block refers to (`python re/blocks.py saga`) and from the decompiled code (`re/q.py`). They are a first
draft: boundaries are rounded to 0x1000 and some names are guesses (marked "?").

| Range | Module |
|---|---|
| 0x401000-0x405fff | App startup, main loops, options at start, FMV, CD audio (`CCDAudioManager`) |
| 0x406000-0x408fff | NXBasics: `CFile`, errors, savegames |
| 0x409000-0x40bfff | Map files (`map.dat`, `$maproot$`) |
| 0x40c000-0x40dfff | NXBasics: `CPicture` (BMP/PCX loading), `CBitmap` |
| 0x40e000-0x411fff | Bob libraries (sprite sets), palettes ?, map manager (0x411) |
| 0x412000-0x413fff | Network checksums |
| 0x414000-0x41ffff | Static logic data: goods, jobs, houses, landscape types, missions |
| 0x423000-0x427fff | Ini reader (`CIniFileReader`, 0x426) |
| 0x428000-0x477fff | Game simulation: humans, jobs, houses, AI; font class at 0x43a6 |
| 0x478000-0x478fff | GUI manager (`gui_add` 0x47802d) |
| 0x479000-0x47dfff | NXSys2DApp: application, window, DirectDraw display (`X2DDDDisplay.cpp`), palette, DirectInput |
| 0x47e000-0x48ffff | engine2d data: human graphics, palettes (0x481), weather, sounds (0x486), landscape graphics (0x48a-0x490) |
| 0x490000-0x49ffff | World renderer: landscapes (0x4905d2 loads `landscapes.ini`), bobs, effects, particles; `CBobManager` blitters |
| 0x4a0000-0x4d7fff | In-game GUI windows, texts, help, menus; campaign screen (0x4d4) |
| 0x4d8000-0x4dcfff | Game flow, multiplayer setup, minimap (0x4dc) |
| 0x4dd000-0x4e2fff | DirectPlay networking (`DPlayBase`, `DPlayLogic`, file transfer, command buffer) |
| 0x4e3000-0x4e3fff | Config and options (`opt_glob.ini` writer 0x4e3929), string tables (0x4e3ede) |
| 0x4e4000-0x4e4fff | Hypertext (`{gfxgood}`, `{setfontcolor}`, ...), GUI resources loader 0x4e4e31 |
| 0x4e5000-0x4e6fff | DirectMusic |
| 0x4e7000-0x4f5fff | MSVC C runtime |

## Renderer

The engine draws every frame in software into memory bitmaps and copies one of them to DirectDraw per frame.
Measured with `cx.ini [cx] trace=1` (writes `cx_trace.log`) at 1680x1050: 60 frames a second, 16-bit bitmaps.

**Bitmap** (`NXBasics::CBitmap`, 0x40c-0x40d), fields seen so far:

| Offset | Field |
|---|---|
| +0x10, +0x14 | width, height |
| +0x18, +0x1c | width - 1, height - 1 (clip limits ?) |
| +0x24 | pixels |
| +0x28 | pixels again (row base ?) |
| +0x30 | pitch in pixels |
| +0x34 | bytes per pixel (1, 2 or 4) |
| +0x38 | pitch in bytes |
| +0x3c | 1 = view onto another bitmap's pixels ("virtual") |
| +0x48 | for a view: the parent bitmap |
| +0x4c | palette for 8-bit bitmaps (0 = the global one at 0x5138c4) |

**Frame present:** `0x479f24` (thiscall on the app object `[0x557f5c]`; bitmap, rect, flags) calls
`CDDDisplay::BitmapToScreen` `0x47c893`. That one picks a copy routine by display depth and bitmap depth:
`0x47c9b9` (8-bit display), `0x47cbef`/`0x47cd06` (8-bit bitmap through the palette to 16/32-bit), `0x47ce1d`/`0x47d011`
(16/32-bit copies), `0x47ceee`. The in-game loop (0x401d38) presents the GUI manager's screen bitmap `[[0x557f44]+0x10]`.
The other callers (0x4022af, 0x40278a, 0x402cdf, 0x403493) are menus, loading screens and FMV.

**Text:**
- Fonts are bob libraries: `data\gui\fonts\font08/10/12.fnt` and `fontdebug.fnt`, loaded at 0x4e4e31 into
  `0x56cae0`/`0x56cae4`/`0x56cae8`/`0x56caec`. Their colours come from `data\gui\palettes\font_*.pcx` (white, dimmed, red, dark).
- Font object: +0x8 line height, +0xc letter spacing, +0x10 bob library.
- `0x43a639` `CFont::DrawChar` (thiscall: colour, bitmap, char, x, y) draws a glyph through the bob blitter `0x466ead`. All text
  goes through it; it has two callers, the hypertext renderer `0x4e4782` and `0x40fa15`.
- `0x43a65c` is char width; `0x43a6a6` is string width (27 callers, used for layout).
- Text is drawn straight into the presented frame, in drawing order. Windows draw through a view bitmap: a second
  `CBitmap` with the same pixel pointer, the view flag at +0x3c and the screen as parent (+0x48). All glyphs went through
  such a view, about 12 per frame on a quiet map. There's no separate text or GUI layer.

**Sprites:** `NXBasics::CBobManager` blitters around 0x49b000-0x49ffff (`PrintBob_Shadow` 0x49c2ad,
`PrintBob_1Bit_TimeMask` 0x49f4d7, and more without names). The bob blitter used for glyphs is `0x466ead`.

**World renderer** (object 0x994 bytes, constructor `0x47d522`; the map window `0x4bb05b`, vtable 0x4f9fd4, keeps it at
+0x30; the in-game GUI object `[0x56c2b4]` keeps the map window at +4):
- +0x64 target bitmap (the window's view), +0x68 draw bitmap, +0x78 zoom (-1..1), +0x96c landscape cache.
- `0x47dd84` SetZoom (clamps to -1..1, rebuilds the bitmaps through `0x482318`); `0x47ddf0`/`0x47de13`/`0x47de2d` zoom keys.
- At zoom -1, +0x68 is a separate bitmap of twice the view size. `0x48250a` draws a frame and ends by shrinking +0x68
  into +0x64 with `0x40e09b` (thiscall src, dst; needs src = 2x dst, width a multiple of 64), called at 0x48255e.
- Other renderers: menu background `0x491c56`, minimap `0x4dc076`/`0x4dc151`, `0x4a57f5`.

**Minimap:**
- Window class `0x4ca767` (vtable 0x4fa924), rect (0, h-120, 140, 120), kept at +0x44 of the in-game GUI object `[0x56c2b4]`.
  Buttons and the picture are created in `0x4ca8bf` (CRect calls: window 0x4ca786, picture 0x4ca8e8, buttons 0x4ca930,
  0x4ca989, 0x4ca9e8, 0x4caa44, 0x4caaa0). The frame is one sprite, GUI bob 0x37 (library `[0x56cb00]`, palette
  `[0x56cb1c]`), drawn by `0x4ca883` (vtable slot 3) with the bob blitter `0x467b20` 13 pixels above the window: a top bar
  and a rope column from x 104, black inside. Bitmap views: `0x40d325` (thiscall view, parent, x, y, w, h), `0x40d362` frees. GUI windows keep x, y, w, h at +4..+0x10 and their bitmap at +0x2c.
- Picture widget `0x4cf4a0` (vtable 0x4fab20, 0x816c bytes, at window +0x74, 112x112 at 4,4): draw `0x4cf696`, full redraw
  `0x4cf9a0` (tiles per pixel at +0x8038/+0x803c, from the map size `0x514758`/`0x51475a` and the bitmap size), partial
  redraw `0x4cffe9`, terrain cache bitmap +0x8040, zoom +0x8064, dirty flag +0x8044. Pixel to map `0x4d07f2`, map to pixel
  `0x4d0852`. Events `0x4d08a2`.

## Display settings

- `0x479ad4` `SetDisplayMode` (thiscall: w, h, depth, flag).
- `0x479f9f` app init: settings block with width at +0x14 and height at +0x18.
- `0x479be8`/`0x479bf9`: current width/height getters.
- `0x479c41`: "mode available" check.
- `render=native` (`loader/render.c`) leaves the display at native size and patches the getter call sites instead:
  width 0x40186d, 0x4018f6, 0x401c78, 0x402181, 0x402638, 0x40301a, 0x4035c5, 0x47a551; height 0x401865, 0x4018ee,
  0x401c70, 0x402179, 0x402630, 0x403011, 0x4035bd, 0x47a534. Mode setup (0x40135c/0x40136a, 0x4017d0/0x4017e0),
  0x479b7b/83, 0x4c22d6/e6 and the options writer keep the physical size.
- Mouse: `0x47a294` polls `GetCursorPos` minus the window origin. Cursor calls (`ff 15`): GetCursorPos 0x47a2a3;
  SetCursorPos 0x47a325, 0x47a3b2, 0x47a477; ClipCursor 0x47a33d, 0x47a48f, 0x47a563. render.c maps them between
  logical and screen pixels.
- With `ui_scale`, cx.dll scales the display size from `opt_glob.ini` down by `ui_scale` in the first two. It patches the options writer's
  two getter calls (0x4e3987, 0x4e39bb) so the file keeps the display size.
