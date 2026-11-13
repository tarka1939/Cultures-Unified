# Cultures Unified: plan (draft 2, includes Cultures Saga)

Goal: one executable and one install that can play:
- the **Cultures 2** campaign (campaign 1),
- the **Northland** campaign (campaign 2),
- the **8th Wonder of the World** campaign (campaign 3),
- the **Saga** campaign (campaign 4, German only, to be translated),
- the two **user campaigns** shipped with Saga (22 + 6 maps, German only, to be translated),
- every single-player and multiplayer scenario from all of them, from one scenario list,
- with the UI scaled to the desktop resolution and a minimap whose size you can change.

The GOG installs and `D:\Cultures_Saga` are used **read-only** as a source. This repo holds only tools, patches, translations, loader source and build scripts. A build script puts the playable game together in `build/` from those folders, so no copyrighted assets go into the repo.

---

## 1. What the exploration found

### 1.1 Installs

| | Cultures 2 | Northland | 8th Wonder | **Cultures – Die Saga** (German) |
|---|---|---|---|---|
| Exe | `Cultures2.exe`, Apr 2002 | `Game.exe`, Sep 2003 | `Game.exe`, **Nov 29 2003** | `Game.exe`, **Nov 5 2003**, the largest code section (0xF4808) |
| Campaigns in the exe | 1 | 2 | 3 | **2, 3, 4 + user campaigns, with a picker** |
| Data | `data0001.lib`, 2216 files | 2506 files | 2691 files | **3982 files** (NL + 8W + campaign 4) |
| Extras | — | Editor | Editor | Editor, `DataX\UserCampaigns\Campaign00\` (22 `.c2m`), `Campaign01\` (6 `.c2m`), 5 FMVs |

- All five exes are the same Funatics "ProjectD" engine: 2D DirectDraw, DirectMusic, plain unpacked x86 PE, image base 0x400000.
- They load `datax\libs\data%4.4d.lib`. That points to **multiple numbered libs**, which would let us ship an overlay `data0002.lib` instead of rewriting the base one. Needs confirming.
- They read the config keys `gfx_screen_width`, `gfx_screen_height` and `gfx_screen_depth`, so they can already render at any resolution. The UI is drawn 1:1 in pixels, which is why it's unusable above 720p.
- The language table is built into the exe (`ger`, `eng`, `fra`, `ita`, `cze`, `rus`, `pol`, `spa`, `por`, `hun`). The Saga exe can therefore load `data\text\eng\…` once that data exists.

### 1.2 Saga is the strongest base
- **Its engine data is byte-identical to 8th Wonder's.** Every bob, palette, texture, sound, `engine2d\inis` file and `logic\*.cif` file matches. The only differences are 2 random-group files and the language folders.
- **All 142 shared map files** (campaigns 2 and 3, the Northland and 8W scenarios and multiplayer maps, the tutorials) are byte-identical to the English GOG versions. So the English map text from GOG pairs with them safely.
- **The multi-campaign menu already exists.** `strings\saga\saga001.cif` holds the buttons "'Nordland' campaign", "'Weltwunder' campaign", "'Saga' campaign", "Customer campaigns" → "Yogi00", "Yogi01". The exe has `start_campaign_1…4_screen` and keeps the NL `hypertext\mythology` and 8W `hypertext\history` folders apart.
- **What's missing is only Cultures 2 (campaign 1).** We need one more button, plus C2's history pages, which clash with 8W's `hypertext\history` path.

**Decision: base engine = the Saga `Game.exe`.** It is a functional superset: the campaign picker, campaign 4 and the user-campaign loader are all in it. The 8W build is 3 weeks newer, but that looks like the English localisation build of the same branch. Phase 1 will diff the two exes for any bug fixes worth backporting.
Risks to check right away: a CD check or serial registration (`Register.exe`, a "please insert the CD" string). Any such check gets bypassed in the loader, which is fine for your own legitimate copy.

### 1.3 Data formats (identical in every game)
- **`.lib`**: header, then a directory table, then a file table (`path, offset, size`), then raw data. No compression. **`.c2m` user-campaign maps use the same format** (`currentusermap\map.dat`, `map.cif`, `text\ger\…`).
- **`.cif`**: an XOR-obfuscated text table, which decodes to INI-like text. It holds type tables, string tables (`stringn <id> "…"`) and map scripts.
- **`map.dat`**: chunked, version 1 everywhere, with its own name dictionary. Terrain and objects are looked up by name, not index, so Cultures 2 maps should survive the newer type tables.
- **`map.cif`**: the mission script, plus `maptype` (1 campaign / 2 single-player / 3 menu demo / 4 multiplayer) and `mapcampaignid <campaign> <mission>`. C2 = 1, NL = 2, 8W = 3, Saga = 4, tutorial = 100.

### 1.4 Maps, by origin

| Source | Campaign | Scenarios / MP | Text language |
|---|---|---|---|
| Cultures 2 (GOG) | `campaign_01_01…11` + subs | `singleplayer_01_*` (8), `multiplayer_00x_*` (19), `demo_mainmenu_00…03` | eng |
| Northland (GOG ≡ Saga) | `campaign_02_*` | `singleplayer_02_*`, `demo_singleplayer_02`, `weinachten_2002` (GOG only), `multiplayer_10x_*` | eng |
| 8th Wonder (GOG ≡ Saga) | `campaign_03_*` | `singleplayer_03_*`, `multiplayer_20x_*`, `tutorial_001…007` | eng |
| **Saga only** | `_campaign_04_01…05` + 9 subs (14 maps) | — | **ger → translate** |
| **Saga user campaigns** | Campaign00 "Yogi00" (22 `.c2m`), Campaign01 "Yogi01" (6 `.c2m`) | — | **ger → translate** |

### 1.5 Translation scope (measured)

| Item | German text | Notes |
|---|---|---|
| Global strings | **15 strings** (`saga001.cif`) | Every other global table matches 8W English 1:1 by ID |
| Saga campaign 4 | ~117 KB: `briefings.txt` ≈ 72 KB, `.hlt` ≈ 37 KB (mostly markup), `strings.cif` ≈ 8 KB | 14 maps |
| User campaigns | ~139 KB: `briefings.txt` ≈ 84 KB, `.hlt` ≈ 50 KB (mostly markup), `strings.cif` ≈ 5 KB | 28 `.c2m` files |
| GUI graphics | `gui\lang\ger\bobs\ls_menu_logos.bmd` (2.5 MB vs 1.6 MB for 8W eng), `titlescreen.bmp`, `menubg.bmp`, `loadscreen.bmp` | The extra frames are probably the campaign picker buttons, possibly with German text baked in |
| FMVs | `intro.mpg`, `intro01/02`, `Seq_0001/0002` (German audio/subtitles?) | Use the English NL/8W videos where they're the same sequence; check the Saga-only ones |

**Translation method:**
1. **Build a parallel glossary.** Saga's German text for campaigns 2 and 3 and every global table lines up with GOG's official English text by file and string ID. Aligning them gives the official term list: hero names, tribes, buildings, goods, gods/mythology terms, UI vocabulary. That keeps the new translation consistent with the published games.
2. I translate the text directly, file by file. Every markup tag (`<usericon:…>`, `<include:…>`, `[blockstart:…]`, `@` prefixes, `\n`) and every string ID stays unchanged.
3. `tools/check_translation.py` checks that the same blocks, IDs and tags are present, that nothing is left untranslated, and that the line lengths fit the briefing window.
4. The output goes to `translations/` as plain-text sources (`.txt` / ini). The build script encodes them into `.cif`, packs `text\eng\` into the overlay lib, and rebuilds each `.c2m` with a `text\eng\` folder next to `text\ger\`.

### 1.6 Content overlap between the GOG games
- **8W ⊇ NL ⊇ C2 for graphics.** C2 has *zero* bobs, palettes or textures that 8W lacks. A few same-named files differ (7 bobs and 33 textures between C2 and 8W); they're probably updated versions.
- 8W's and Saga's `DataX\DM2` already contain **all of C2's and NL's music segments**.
- **Conflicts to merge**: C2's `hypertext\history\` (`campaign_01_xx.hlt`, `index.hlt`) clashes with 8W's history folder. C2 also has its own `datax\pictures\pre00/post00.bmp` and FMVs.

### 1.7 Tooling on this machine
- Present: Python 3.14, Git, CMake, Visual Studio 2026 Community (for the 32-bit loader DLL), Cheat Engine.
- Missing: Ghidra (and the JDK 21 it needs; only Java 8 is installed), the REA/ReVa MCP bridge, x32dbg, any DirectDraw wrapper.

---

## 2. Architecture

```
D:\repos\Cultures\
  tools\         Python: lib/c2m read+write, cif encode/decode, map validator, string diff, translation checker
  translations\  eng text we author: saga001, _campaign_04_*, usercampaigns\Campaign00|01\*, glossary.tsv
  overlay\       other authored data: campaign-1 picker strings, C2 history index, menu graphics
  loader\        C++ proxy DLL (e.g. dinput.dll / version.dll) for Saga Game.exe:
                   CD/serial bypass, campaign-1 button, history path for C2, UI scale, minimap size
  re\            Ghidra project notes and address maps (Saga exe primary, C2/NL/8W for diffing)
  build.py       sources -> build\ (Saga exe + data0001.lib, overlay data0002.lib with eng text,
                 C2 maps, English .c2m files, loader, config)
  build\         playable output (git-ignored)
```

Code changes go through a **loader DLL with runtime hooks**. The original exe stays untouched, and each feature can be switched off.

---

## 3. Phases

### Phase 0: Tooling and RE setup ✅
1. ✅ `tools/libtool.py`: lib/c2m read+write and cif encode/decode; `tests/test_roundtrip.py` shows every archive and cif in all four games round-trips byte-for-byte.
2. ✅ Ghidra 12.2 headless (it runs on the system JDK 25), x64dbg in `.tools/`. `re/analyze.sh` analyses Saga/8W/C2 and exports decompiled C, strings, xrefs and imports to `re/export/` (~4.9k functions each, 0 decompile failures); `re/q.py` queries them, `re/pe.py` dumps data tables. No MCP bridge for now: the text exports cover static work without changing Claude Code config.
3. ✅ First RE pass, in `re/NOTES.md`: startup and archive loading (`use_data_file_%d`, data0010..0001), the CD check and its 3-byte patch, menu screen ids, the campaign map screens (hard-coded marker and overlay tables + bmd frames, with the concrete C2 port recipe), campaign data and hypertext. Still open: the in-game minimap (needs x64dbg), in-game GUI layout, archive precedence for duplicate names.

### Phase 1: Saga exe in English ✅ (first playable milestone)
1. ✅ `python build.py` copies the Saga install to `build\`, patches the Game.exe CD check (3 bytes, verified against the original bytes), and writes `Game.ini` (`set_language 1`, windowed by default; `--fullscreen` for fullscreen).
2. ✅ Overlay `DataX\Libs\data0002.lib` (2,482 files, only `...\eng\...` paths): English files from 8W (1,298) and NL (820) for every Saga language file that has a counterpart; Saga's own menu bank `ls_menu_logos.bmd` (it holds the campaign 2/4 map frames that 8W's lacks), title screen and load screen; campaign 4 + `saga001` from `translations/eng` (29). Every Saga string ID exists in the English tables, so no string is missing.
3. ✅ The overlay works: the engine loads data0010..0001, and because the overlay only adds new `eng` paths, precedence for duplicate names doesn't matter yet. If a localized file is missing the engine falls back to the `eng`/`ger` directories, so a gap shows German rather than crashing.
4. ✅ Smoke test (windowed): English main menu, no CD prompt, campaign picker with the translated buttons, Saga campaign map, and the first campaign 4 mission loads with its English briefing.
    Cursor artifacts: the user still saw them with cnc-ddraw alone, so they come from the game's hardware cursor (`.cur` files). The build now writes `gui_mouse_software 1` to `Saves\opt_game.ini` (other keys are kept; `--hw-cursor` reverts). In this mode the game draws its own cursor into the frame (seen in a screenshot). **User confirmed: artifacts gone.** cnc-ddraw is installed by default (borderless fullscreen, `--window WxH`, `--no-ddraw`); at 1600x1200 the menus scale and clicks map correctly.
   Polish list: the "DIE SAGA" plaque on the title logo, title screen and load screen (relettered "THE SAGA"). Needs `work/manifest.json` (run `tools/extract_text.py` first).

### Phase 2: Translate Saga content
1. ✅ Build the glossary from the parallel GOG-English / Saga-German corpus: `translations/glossary.tsv`, about 110 terms.
2. ✅ Translate `saga001`, `_campaign_04_*` (14 maps), Yogi00 (22) and Yogi01 (6), 85 files in all. They're in `translations/eng/`; the checker reports 0 errors and 3 minor long-line warnings. Naming choices still open: see `translations/PROGRESS.md`.
3. ✅ `build.py` (`build_usercampaigns`) rebuilds all 28 Yogi `.c2m` files: the map's `text\ger\` folder is copied to `text\eng\` (the briefings load fonts and colours from their own folder via `$local$`), with `briefings.txt` and `strings.cif` replaced by the translations. The 7 broken filenames are restored (`08_Österreich`, `12_Die_Prüfung`, `13_Das_Tal_der_Könige`, `20_Die_Höhle`, `21_Südafrika`, `04_Die_Händler`, `05_Die_Münzsammler`). In game, the Yogi Part 2 list shows all 6 English map names and 'The Merchants' loads. Baked-in German text: none in the campaign 4 briefing pictures (20 checked); only the 'DIE SAGA' logos, which are on the polish list.
4. Play-test each map (with the user): briefings render, objectives show up, the text fits its windows.

### Phase 3: Port the Cultures 2 campaign (campaign 1)
1. ✅ `tools/validate_maps.py`: every house, good, job, vehicle, tribe and palette the C2 maps use exists in the Saga. All script verbs exist in the exe, except `True`/`Exit`/`None`, which are handled in code (they're missing from C2's own strings too). `hero_bjarni` differs from the definition only in case. `campaign_01_09` holds only briefings (no map, no marker), so there's nothing to play.
2. ✅ The overlay carries C2's `campaign_01_*`, `singleplayer_01_*` and `multiplayer_0*` maps (915 files, English text and sfx as shipped by GOG). The C2 maps carry `mapcampaignid 1 <mission>`, so the Saga engine treats them as campaign 1 with no changes. `demo_mainmenu_0x` is left out (menu backdrops).
3. ✅ `loader/cx.c` → `cx.dll`, built with MSVC by `loader/make.py` and added to Game.exe's import table by `tools/peimport.py`:
   - a "'Gates of Asgard' Campaign" button (saga001 strings 25/26) above Northland on the single-player screen, using command 5006 (a free jump-table slot);
   - screen 5 (the Northland map, the same picture as C2's) is switched to campaign 1 (campaign id, marker table from Cultures2.exe `0x4fc2e0`, path overlays = C2 frames 3..11 appended to `LS_menu_logos.bmd` as 26..34);
   - `start_campaign_1_screen` is consumed after a C2 mission.
   Every patch site is checked against the original bytes first. The user confirmed in game that it works.
   C2 history pages: the 8W and C2 page names don't collide (`history_*` vs `campaign_01_*`). build.py adds C2's 26 pages and appends C2's `index.hlt` entries to the 8W index (not yet checked in game).
   Saga videos: the Saga ships only German FMVs. build.py copies the identical English NL/8W films to `FMV\Eng` (intro01/Seq_0001 ← NL, intro02/Seq_0002 ← 8W). The Saga-only `intro.mpg` stays German (via the fallback).
   C2 videos: build.py converts C2's Bink films with ffmpeg (`.tools/ffmpeg` or PATH; skipped if missing) to the Saga's MPEG-1 format: `Intro.bik` → `intro00.mpg`, shown before campaign 1 mission 10 by a cx.dll hook in `FUN_00403100`; `Seq_0000.bik` → `Seq_0000.mpg` (C2 maps request sequence 0; the Saga uses 1 and 2). Hooks are verified in memory; playback not yet seen in game.
4. Play-test the C2 campaign end to end: balance changes or removed script verbs could soft-lock missions.

### Phase 4: Unified scenario panel ✅ (pending in-game check)
1. ✅ The Saga builds its scenario and multiplayer lists by scanning every `data\maps\*` folder (`FUN_004118f9`, map manager `DAT_00513c0c`; list section by `maptype`: 2 = scenario, 4 = multiplayer). So every map in the overlay appears automatically. The lists now hold 22 scenarios (8 C2, 9 Northland incl. the demo and the Christmas map, 5 8th Wonder) and 30 multiplayer maps (18 C2, 7 Northland, 5 8th Wonder). The only map that was missing, NL's `weinachten_2002` ("Let it snow"), is now in the overlay.
2. ✅ `build.py` (`tag_scenarios`) prefixes each maptype 2/4 map name (`mapnamestringid` in the map's `text\eng\strings.cif`) with its origin: `[C2]` Gates of Asgard, `[C3]` Northland, `[C4]` 8th Wonder (the series numbering), worked out from the folder name. 52 maps are tagged, and in all 114 map string files only the name line changed. The tag also shows wherever the game prints the map name (e.g. save names).
3. ✅ The multiplayer lists use the same names, so they're tagged too.
4. To check in game: the scenario list (single player → Free Play), its sort order with the tags, and that C2 scenarios start and play.

### Phase 5: Resolution and UI scaling
- **Stage A, quick win:** add an open-source DirectDraw wrapper (cnc-ddraw, MIT). The game renders at a lower internal resolution and the wrapper scales it up to the native display (borderless window, integer/bilinear/shader scaling, configurable). Everything gets bigger, the world view included. **Started:** `build.py` installs cnc-ddraw (global `[ddraw]` section: windowed + fullscreen = borderless, maintas, adjmouse, renderer=auto; Game.ini `gfx_fullscreen 1`).
- **Stage A+, tried and rejected:** rendering at screen size / UI scale (e.g. 1064x800, 1280x720) and letting cnc-ddraw upscale. The menus look fine, but the user found in game that panels overlap and pictures are clipped, and non-standard sizes stop at start with the game's own "display mode not available" (it only accepts modes DirectDraw lists). The GUI is laid out for 800x600, so the build pins `gfx_screen_*` to 800x600 again; `build.py --game-res WxH` is kept as an experiment switch. Widescreen and a larger UI therefore need Stage B (GUI layout/scaling hooks).
- **Stage B, true UI scaling:** the world renders at native resolution and only the GUI is magnified. This needs reverse engineering of the X2D GUI blit path: the loader would draw the GUI into its own surface, scale it up, and remap the mouse.
  - **Findings so far:** the engine runs at any size given by `gfx_screen_width/height/depth` in `Saves\opt_glob.ini` (default 800x600; read at startup, written back by `FUN_004e3929`). At 1600x1200 it really renders at 1600x1200 (confirmed in memory: `[[[0x557f5c]+0x4c]+8]+4/+8`). The menus and the in-game GUI are drawn 1:1, so at high resolution they're tiny (the in-game bar is ~40 px wide). So Stage B = keep the engine at native resolution and magnify only the GUI.
  - Test harness: `start_map data\maps\<folder>` in `Game.ini` loads a map directly; the game window can be captured with `PrintWindow`, and the engine state read with `ReadProcessMemory`. Background captures show the in-game GUI but not the world view (black); the "Game paused" text is the game-speed check at `0x4bb341` (`[0x513a30] < 0.01`), not focus: the app active flag (`[[0x557f5c]+0x4c]+5`, set on WM_ACTIVATE at `0x47c596`) reads 1. Not pursued further, since the GUI is what matters here.
  - **Display modes (done):** cnc-ddraw `resolutions=2` (full mode list) removes the "display mode not available" error; 1280x720 now starts.
  - **Layout at 1280x720:** in game coordinates the frame stays pinned top-left (side bar, mail bar) and the minimap bottom-left; window rects are identical at 800x600 and 1280x720 (GUI manager `[0x557f44]`, top-level list at +0x14, widget rect x/y/w/h at +4). (An earlier "120 px lower" reading was cnc-ddraw letterboxing in the capture.) Background captures never show the world or pop-up windows: game speed `[0x513a30]` stays 0 (map start waits for input), so overlaps must be checked with the game in the foreground.
  - **Foreground play-test at 1280x720 (2026-11-13):** main/single-player/map-selection/difficulty menus, mission briefing, house and settler windows, build window and statistics window are all placed correctly (side bar and minimap pinned left, info windows pinned right, dialogs centred). The engine already lays out the in-game GUI for any size; the only effect is that the GUI is drawn 1:1, i.e. smaller relative to the world. So with the full mode list, true UI scaling = pick a standard game mode below the window size and let cnc-ddraw upscale (e.g. 1280x720 in a 1920x1080 window = 1.5x GUI, more world than 800x600). Non-standard sizes (e.g. 1064x800) are still refused. Unrelated bug seen at both sizes: the Free Play button shows "To the Tutorial screen." over its label. (A doubled "Dwelling 1" label is the building name plus the hover tooltip, not a bug.)
  - **Auto game resolution (done):** `build.py` now picks the game resolution itself: the largest standard mode (`GAME_MODES`, cnc-ddraw full list) that fits the window, or the screen in borderless fullscreen, divided by `--ui-scale` (default 1.5), preferring the same aspect ratio; falls back to 800x600. Examples: 1600x1200 window -> 1024x768, 1920x1080 -> 1280x720, 2560x1440 -> 1600x900, 2560x1600 -> 1680x1050, 3840x2160 -> 1920x1080. `--game-res WxH` overrides. Verified 1024x768 and 1680x1050 start. Without cnc-ddraw the game stays at 800x600.
  - **Options > Video (in progress):** picking a mode in the menu used to set the internal resolution directly (2560x1600 = tiny GUI). Now `opt_glob.ini` holds the display size and cx.dll scales it by `ui_scale` from `cx.ini`: hooks on app init `0x479f9f` (settings block, w +0x14 / h +0x18) and `CApplication::SetDisplayMode` `0x479ad4` (re-applying the mode cx.dll produced is passed through, to avoid double scaling). Startup verified (1600x1200 -> 1024x768); changing the mode in the menu while running is not yet tested.
  - **User verdict (2026-11-13):** upscaling the whole frame is not enough. Labels are tiny and blurry when zoomed out (fonts are upscaled bitmaps), and the world still looks too small. Continued as Phase 9, which rebuilds the renderer so text and world are drawn at native resolution.
  - Found on the way: a German copyright notice picture is shown while a map loads; it needs an English replacement (Phase 2 leftovers).
- **Windowed cursor lock (fixed):** cnc-ddraw's cursor lock (`devmode=false`) clipped the pointer to the window, and while locked, Alt+Tab and Win+D did nothing. Windowed builds now set `devmode=true` (`build.py --lock-cursor` restores the lock; Ctrl+Tab unlocks it). Borderless fullscreen keeps the lock. Confirmed by a scripted repro (SendInput click, Alt+Tab, Win+D) and by the user in game.

### Phase 6: Variable minimap size
Reverse engineer the in-game minimap widget (its rectangle, render routine, click mapping) and hook it to a configurable size or a scale factor, with an optional hotkey to cycle sizes.

### Phase 9: Rebuilding the engine (decompilation)
**Goal:** our own source code for the game, built from the decompiled Saga exe, so that rendering, fonts, GUI and the world view are no longer limited by the 2003 engine. The end state is a standalone exe built from our source that loads the players' original data.

**Approach: incremental reimplementation inside the running game** (the way OpenRCT2 was made). Our code lives in `cx.dll`, which is already loaded into Game.exe. One function or module at a time is rewritten in C, hooked over the original and checked in game against the original behaviour. The game stays playable after every step, and what's done is useful at once. Once nothing calls into Game.exe any more, the code is linked into its own exe.
- **Rejected for now:** a clean-room engine written from scratch against the data formats. Nothing would be playable until the whole simulation (jobs, AI, scripts, trading, combat) matched the original; that's years of work with no milestones in between.
- **Rejected:** a matching decompilation (byte-identical rebuild of the exe). It's a lot of effort for exactness the goals don't need.

**Scale:** 4,903 functions (Ghidra export in `re/export/saga`, queried with `re/q.py`). There's no RTTI and only 64 qualified names in strings, so names come from analysis. Source paths left in asserts (`BaseLibs\XSystem\X2DApp\X2DDDDisplay.cpp` ~0x47b-0x47d, `ToolLibs\DPlayBase\*` ~0x4dd-0x4e1, `ToolLibs\DPlayLogic\*` ~0x4e0-0x4e3) anchor the module map: MSVC lays out functions in source-file order.

**Order, driven by what the player sees:**
1. **Module map:** split the exe into modules (address ranges) with names: app/window, X2D display, sprite blitter ("bobs", `.bmd`), fonts (`.fnt`), landscape/world view, GUI widgets, game logic, scripts, network, CRT. Output: `re/MODULES.md` plus a symbols file the tools and Ghidra share.
2. **Renderer at native resolution** (milestone 1, fixes this phase's complaints):
   - Our display layer replaces X2DDDDisplay: the engine keeps a *logical* screen (e.g. 1280x800 for 2x on 2560x1600), and we draw into a *physical* native-size buffer.
   - Sprite, tile and GUI blits are drawn scaled (nearest neighbour, which looks like the original pixel art).
   - Text is drawn at physical resolution from a scalable font, so labels and windows are sharp at any size.
   - Separate scales for world and GUI, so the map can be made larger without the GUI growing with it (and the reverse).
   - cnc-ddraw is no longer needed for scaling.
3. **GUI system:** widgets, windows, layout; resolution-independent layout, larger minimap (Phase 6).
4. **Everything else, as needed for independence:** map and data loading, simulation, AI, scripts, sound, video, network. Each piece is rewritten when it blocks a feature or when we go for the standalone exe.

**Progress:**
- **Module map, first draft:** `re/MODULES.md` (address ranges, renderer entry points, bitmap fields). There's no symbols file yet.
- **Renderer findings:**
  - Frames are drawn in software into 16-bit memory bitmaps, then one call per frame copies one of them to DirectDraw (0x479f24). That call is the place to scale to native resolution.
  - All text goes through one function (`CFont::DrawChar` 0x43a639). That's where crisp text can be drawn instead.
  - Text is drawn straight into the presented frame, through view bitmaps onto the same pixels. There's no separate text or GUI layer.
  - **Plan for sharp text:** the DrawChar hook records each glyph (string, font, colour, position in the frame) instead of drawing it at low resolution. Present scales the frame to native size, then draws the recorded text over it at native size.
  - **Open question:** text that a later-drawn window covers (for example a building name under an open menu) would then show through. Check whether the GUI draws windows over map labels; if it does, clip each glyph against the windows drawn after it.
  - `cx.ini [cx] trace=1` logs frames and glyphs per second to `cx_trace.log`.
- **Native renderer prototype (`loader/render.c`, `cx.ini render=native scale=2`, now the build.py default):**
  - The engine runs at display size / scale (1280x800 on a 2560x1600 screen). Its size getters and the cursor calls (GetCursorPos, SetCursorPos, ClipCursor) are patched to that logical size; the display itself stays at native size.
  - Present scales each frame up (nearest neighbour). Menus and FMV that are smaller than the logical size are scaled too and centred.
  - DrawChar records glyphs on the screen bitmap instead of drawing them. They are redrawn at native size in a TrueType font (Georgia bold by default, `font=`, `font_weight=`), a word at a time, centred in the bitmap font's box. Colours come from drawing one test glyph with the original font: fill plus dark edge.
  - cx.dll makes the process DPI-aware. With Windows scaling at 125%, the game used to be stretched again by Windows.
  - Checked in game: menus, Free Play map, mission text, building labels and mouse all correct at 2560x1600.
  - **World at 1x, GUI at 2x (`world=native`, the default):** the map window uses the engine's own zoom-out (renderer zoom -1 draws the world at twice the view size, then shrinks it). cx.dll skips the shrink, keys the view and puts the full-size world on the display 1:1 at present. Sprites keep their original pixels, the map shows 4x the area, and the side panel, minimap, labels and windows stay 2x. Mouse picking is the engine's own zoom-out code. The zoom key still cycles to 2x and 4x. `world=scaled` goes back to scaling the world too.
  - **Minimap twice the size, at display resolution (`minimap=2`, the default):** the engine builds the minimap window 112 GUI pixels wider and taller (its constructors' CRect calls are adjusted), so the picture is 224 GUI pixels while the frame and buttons stay normal GUI size. The frame sprite is redrawn in pieces around it: bar ends, tiled bar, corner, tiled rope column, column foot. The picture is drawn by the engine at 448x448 (display resolution): the widget gets bigger bitmaps and the engine picks finer tiles per pixel from their size. Clicks inside are scaled to match. Phase 6 in effect.
  - Known issues: tooltips draw over button labels on some menus (as before). Text covered by a later-drawn window would still show (not seen yet). Things drawn over the world in the view after the shrink (selection marks, labels) are at GUI scale. `ui_scale` (`--render scaled`) stays as the fallback.
- **Bug fixed on the way:** the game writes `opt_glob.ini` at every start, and it wrote the scaled mode (1680x1050). The next start scaled that again (to 800x600). cx.dll now gives the options writer the display size. Checked: after a run the file still says 2560x1600.

**Rules:** each rewritten function has its original address, a short note on what it does, and an in-game check. Every hook keeps checking the original bytes, as cx.dll does now. Game assets never go into the repo.

### Phase 7: Packaging ✅
`build.py`, one command from the source folders to a playable `build\`. Plus a play-test checklist, a README, and a toggle to switch each loader feature on or off.
- ✅ `python build.py` builds from a fresh checkout. It runs `tools/extract_text.py` itself when `work/manifest.json` is missing, and it stops with a clear message when cnc-ddraw is missing. A fresh build into an empty folder matched `build\` (checked 2026-11-13).
- ✅ Loader switches in `cx.ini` (all on by default): `c2_campaign`, `fmv`, `world`, `minimap`, `minimap_hires`, plus the existing `render`, `scale`, `ui_scale`, `font`, `font_weight` and `trace`. `build.py` writes every key with a comment. `--off FEATURE` and `--minimap N` set them at build time. With `trace=1`, the loader's status lines also go to `cx_trace.log`. Each switch was checked from the loader log, not yet in play.
- ✅ `README.md` (requirements, build, options, cx.ini keys) and `CHECKLIST.md` (the play-test list, with what has been seen working).

### Phase 8: Downloadable installer (final step)
A single Windows setup `.exe` that turns the player's own installs into the finished product, with no Python, MSVC or repo checkout needed. It contains only our work, never game files.

1. **What the installer ships:** the frozen build pipeline (`build.py` + `tools/` as one exe, e.g. PyInstaller), a prebuilt `cx.dll` (every hook: C2 campaign screen, videos, and later the scenario panel, UI scaling and minimap), all translations (`translations/eng`), the overlay recipe (which maps, history pages and menu frames to take from which game), cnc-ddraw (MIT, with its license), a README and a play-test checklist.
2. **Wizard pages:**
   1. Welcome and licenses.
   2. Game locations: Cultures Saga, GOG "Cultures and Cultures 2" and GOG "Northland and 8th Wonder of the World". Paths are pre-filled from the GOG registry keys and the default folders, with Browse buttons.
   3. Checks: each folder is verified (expected exe and lib hashes/sizes, `data0001.lib` present). Unknown versions get a clear message instead of a broken build.
   4. Target folder: a new folder. The source installs are only read, never written.
   5. Options: borderless, windowed or exclusive fullscreen; window size; software cursor; whether to convert the Cultures 2 videos.
   6. Progress.
   7. Finish: desktop and Start-menu shortcuts.
3. **Cultures 2 videos:** ffmpeg isn't bundled, because of its GPL/LGPL obligations and size. The option converts with an ffmpeg already on PATH, or downloads the official build after the player agrees. Without it, the game simply skips those two videos.
4. **Uninstall and repair:** the uninstaller removes only the target folder and shortcuts. Saves are kept unless the player opts in. Re-running setup rebuilds in place, e.g. after a game update or a new installer version.
5. **Release:** the installer is built by a script (Inno Setup or NSIS around the frozen pipeline) and published as a release download. Smoke test: install on a clean Windows user account from the three GOG/Saga installs, then run the play-test checklist.

---

## 4. Risks
| Risk | Mitigation |
|---|---|
| Saga exe has a CD or serial check | Bypass in the loader; Phase 1, step 1 |
| The Saga build lacks fixes that are in the newer 8W build | Diff the exes in Ghidra and backport fixes through hooks if any matter |
| C2 maps rely on C2-era balance or removed script verbs | Validator plus per-map script fixes plus play-testing |
| Translated text overflows the briefing or objective windows | The checker flags long lines; play-testing |
| Menu graphics have German text baked in | Redraw the affected `.bmd` frames (needs a bmd tool) |
| Campaign id is hard-coded in many places | Hook the single choke point, not each call site |
| GUI uses absolute coordinates everywhere | Ship Stage A first; Stage B is incremental |
| Copyright | Repo holds tools and our own translations only; build from your own copies |
| Installer redistributes something it shouldn't | It ships only our code, translations and MIT cnc-ddraw. Game data is read from the player's installs at setup time; ffmpeg is used or downloaded only with consent |
| Players have other game versions (retail CD, other GOG builds) | The installer verifies hashes and names the version it expected; add recipes per version as reported |

---

## 5. Decisions and open questions
Decided:
- Reverse-engineering tooling is my choice: Ghidra headless + text exports + x64dbg (see Phase 0; MCP bridge optional later).
- Game naming: Cultures 2 is "The Gates of Asgard" (its campaign button reads "'Gates of Asgard' Campaign", like the Northland and 8th Wonder buttons). Map tags follow the series numbering: `[C2]` Gates of Asgard, `[C3]` Northland, `[C4]` 8th Wonder of the World.
- The user campaigns show in the picker as **"Yogi"** and **"Yogi Part 2"** (already applied in `translations/eng/global/saga001.ini`).

Still open:
1. UI scaling: is Stage A acceptable as the first deliverable, or is Stage B required?
2. Multiplayer: in scope, or single-player only for now?
3. Translation naming: German-invented names are kept, descriptive ones translated. The specific choices to review are in `translations/PROGRESS.md`.
