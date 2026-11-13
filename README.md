# Cultures Unified

One English install of the Cultures Saga engine (the German *Cultures – Die Saga* `Game.exe`) that plays:

- campaign 1, **Gates of Asgard** (Cultures 2), ported into the Saga engine,
- campaign 2, **Northland**, and campaign 3, **8th Wonder of the World**,
- campaign 4, **the Saga** campaign, translated into English,
- the two Saga **user campaigns** (22 + 6 maps), translated,
- every single-player and multiplayer scenario from all of them in one list, tagged `[C2]`, `[C3]` or `[C4]`,
- at native resolution: the world drawn 1:1, the GUI scaled up, text redrawn with a TrueType font, and a larger
  minimap drawn at display resolution.

This repo holds no game files. It has tools, translations, the `cx.dll` loader source and a build script. The build
script puts the playable game together from your own copies of the games, which it only reads.

## What you need

| | |
|---|---|
| Games | *Cultures – Die Saga* (German retail install), and from GOG *Cultures 1+2* and *Northland + 8th Wonder of the World* |
| Python | 3.10 or newer |
| Visual Studio | 2019 or newer with the C++ x86 tools (builds `cx.dll`; found through `vswhere`) |
| cnc-ddraw | a release extracted into `.tools/cnc-ddraw/` (`ddraw.dll`, `ddraw.ini`, `Shaders/`), [github.com/FunkyFr3sh/cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw) |
| ffmpeg | optional, in `.tools/ffmpeg/ffmpeg.exe` or on `PATH`. It converts the Cultures 2 videos; without it they are skipped |

Default source folders (change them with `--saga` and `--gog`):

```
D:\Cultures_Saga
D:\GOG Games\Cultures and Cultures 2\Cultures 2
D:\GOG Games\Northland and 8th Wonder of the World\Northland
D:\GOG Games\Northland and 8th Wonder of the World\8th Wonder of the World
```

## Build

```bash
python build.py
```

The first run also indexes the game text into `work/` (`tools/extract_text.py`). The result is `build\`. Start
`build\Game.exe`. It opens borderless fullscreen at your desktop resolution.

Run the same command again after changing anything. It rebuilds `build\` in place and keeps `build\Saves`.

### Options

| Option | Effect |
|---|---|
| `--saga DIR`, `--gog DIR`, `--out DIR` | source and output folders |
| `--window WxH` | a window of that size instead of borderless fullscreen |
| `--lock-cursor` | with `--window`: keep the pointer inside the window (Ctrl+Tab frees it) |
| `--render native` | default: the engine runs at display size / `--scale`, `cx.dll` scales each frame and redraws text |
| `--scale N` | screen pixels per GUI pixel with `--render native` (default 2; the full-resolution world and minimap need 2) |
| `--minimap N` | minimap picture size in multiples of the original 112 pixels (default 2, 1 = original) |
| `--off FEATURE` | switch a loader feature off; repeatable (see below) |
| `--render scaled`, `--ui-scale F` | older mode: cnc-ddraw stretches a smaller standard mode by about F |
| `--game-res WxH` | fixed engine resolution, no scaling |
| `--no-ddraw`, `--fullscreen` | the system DirectDraw instead of cnc-ddraw (800x600; `--fullscreen` for exclusive mode) |
| `--hw-cursor` | the hardware cursor (leaves trails on modern Windows) |

## Setup exe (for players)

```bash
python installer/make_setup.py
```

This builds `work\installer\CulturesUnifiedSetup.exe`, about 13 MB. It's a wizard for players who don't have Python
or Visual Studio. It carries the build script, a prebuilt `cx.dll`, the translations, the text index (file names
only) and cnc-ddraw. It holds no game files.

- **Pages:** welcome and license, the three game folders (found automatically and checked), the install folder,
  options (display, cursor, Cultures 2 videos, shortcuts), progress, finish.
- **Install:** it registers an uninstall entry (Windows Settings > Apps) and copies itself into the install as
  `Cultures Unified Setup.exe`. Run it again to change options or rebuild.
- **Uninstall:** `--uninstall` removes the install folder and shortcuts. It asks whether to keep `Saves`.
- **Scripted installs:** `--unattended <folder>` installs with the folders it finds and the default options.
- **Cultures 2 videos:** ffmpeg isn't bundled. Setup looks on PATH, next to itself, in winget's folders and
  `C:\ffmpeg\bin`. If ffmpeg isn't found, the options page says in red that the Gates of Asgard intro and ending
  won't play, with a "Get ffmpeg" button and Browse.
- **Log:** every run writes `%TEMP%\CulturesUnifiedSetup.log` and copies it to `<install>\setup.log`. It records
  the folder checks, the build arguments and output, and any error, including errors inside the window.

## Loader features (`build\cx.ini`)

`cx.dll` reads `build\cx.ini` at start. `build.py` writes every key with a comment. You can edit it without
rebuilding. Every feature is on unless it says 0.

| Key | Effect |
|---|---|
| `c2_campaign` | the Gates of Asgard button and its campaign screen. `0`: the Saga's own single-player screen |
| `fmv` | the Cultures 2 intro film before campaign 1 mission 10 |
| `render=native`, `scale` | the native renderer (missing or another value: off, the `ui_scale` path is used) |
| `world` | `native`: landscape and units at full display resolution; `scaled`: drawn at GUI size and scaled up |
| `minimap` | minimap size, multiples of 112 GUI pixels |
| `minimap_hires` | the minimap picture at display resolution |
| `font`, `font_weight` | the TrueType face and weight used for text |
| `ui_scale` | the GUI magnification without `render=native` |
| `trace` | `1` writes `cx_trace.log` next to `Game.exe` |

`--off c2_campaign`, `--off fmv`, `--off world` and `--off minimap_hires` set the same keys at build time.
Every hook first checks the bytes it replaces. If they differ, it stays off and writes a line to `cx_trace.log`
(with `trace=1`).

## What the build does

- copies the Saga install to `build\` and patches the CD check in `Game.exe` (3 bytes),
- builds `cx.dll` (`loader/`) and adds it to `Game.exe`'s imports,
- writes `DataX\Libs\data0002.lib`, an overlay with only English (`eng`) files:
  - the GOG English text and graphics,
  - the translations in `translations/eng`,
  - the Cultures 2 maps, history pages and campaign-map frames,
  - the scenario tags,
- rebuilds the user campaign maps with English text,
- copies the English campaign films and converts the Cultures 2 ones,
- installs cnc-ddraw and writes `Game.ini`, `cx.ini`, `Saves\opt_glob.ini` (display size) and
  `Saves\opt_game.ini` (software cursor).

## Repo layout

| Path | Contents |
|---|---|
| `build.py` | the build |
| `installer/` | the setup wizard (`setup.py`), its build script (`make_setup.py`), the cnc-ddraw license |
| `loader/` | `cx.c` (campaign, video and scaling hooks), `render.c` (native renderer), `make.py` |
| `translations/` | the English text, glossary and progress notes |
| `tools/` | archive (`.lib`, `.c2m`, `.cif`, `.bmd`) tools, text extraction, translation and map checks, PE import patcher |
| `re/` | reverse-engineering notes (`NOTES.md`, `MODULES.md`) and Ghidra export scripts |
| `tests/` | archive round-trip test |
| `PLAN.md` | phases and progress |
| `CHECKLIST.md` | the play-test checklist |

`build/`, `work/`, `.tools/`, `loader/bin/` and the Ghidra exports are not tracked. Game files never go into the repo.
