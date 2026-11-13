# Reverse-engineering notes

Tooling: Ghidra 12.2 headless (system Temurin JDK 25), exports in `re/export/<prog>/` (not committed).
Regenerate: `bash re/analyze.sh` (full) or `EXPORT_ONLY=1 bash re/analyze.sh` (re-export).
Query: `python re/q.py <saga|w8|c2> s|sf|f|x|g|i <arg>`; raw data tables: `python re/pe.py <exe> <va> <n> [fmt]`.

| prog | file | build date | functions |
|---|---|---|---|
| saga | `D:\Cultures_Saga\Game.exe` | 2003-11-05 | 4875 |
| w8   | GOG `8th Wonder of the World\Game.exe` | 2003-11-29 | 4833 |
| c2   | GOG `Cultures 2\Cultures2.exe` | 2002-04-17 | 5099 |

All addresses below are **Saga Game.exe** VAs unless marked `c2:` / `w8:`.

## Startup, data archives, config — `FUN_00401000` (app ctor)
- Reads `game.ini` (+ command line). Keys `use_data_file_%d` for 9..0: if any is set, those archive paths are
  loaded **instead of** the default set. Otherwise loads `datax\libs\data%4.4d.lib` for 10 down to 1, every file that exists.
  ⇒ overlay archive (`data0002.lib`) is loaded automatically; which copy wins on a duplicate name is still **to verify**
  (lookup function `FUN_0040731e`/`FUN_004071a5` registry).
- `gfx_fullscreen`, `gfx_fullscreen_toggle`, `gfx_screen_width/height/depth` (defaults 800×600×16).
- Screen size globals used by menus: `DAT_0056c69c` = width, `DAT_0056c6a0` = height.

## CD check
- `FUN_0047aadb(label)`: scans drives c..z for `GetDriveType==CDROM` with volume label `"saga"` (`DAT_00501320`) and
  `X:\autorun.exe`; returns drive letter or 0.
- `FUN_0040285f(drive)`: checks `X:\datax\extro.dat` (size arg 0x1cd5116).
- `FUN_004d43bd()` = CD present && extro.dat ok. Used by `FUN_004d3f15` (menu start: CD ok → screen 1 main menu,
  else **screen 0x1c**, the insert-CD screen) and by `FUN_004d86b6` (main-menu button case 8).
- Other callers of `FUN_004027bf` (drive letter) only feed the CD-audio/music manager (`FUN_00403ff5`) — returning 0 is fine.
- **Patch**: `FUN_004d43bd` → `mov al,1; ret` (`B0 01 C3` at VA 0x4d43bd). w8 has no such check path (GOG no-CD build).

## Menu system
- Menu manager singleton `DAT_0056c6c4`; field 0 = current screen id.
  - `FUN_004d4499(mgr, id, flag)` switch screen → `FUN_004d44c2` (teardown), `FUN_004d45a5` (state setup), `FUN_004d4b40` (build widgets, big switch on id).
  - `FUN_004dc151` paint: backgrounds/overlays per screen id.
- Screen ids (Saga): 1 main menu, 4 single-map list (custom/scenario list built from map manager `DAT_00513c0c`,
  flags at map+0x120: 4, 0x20, 0x10, 0x40 = list sections), **5/6/7 campaign map for campaign 2/3/4**,
  0xe/0xf multiplayer, 0x13 MP lobby, 0x15/0x16 load/save, 0x18/0x19 MP auto host/join, 0x1c insert-CD.
  c2: screen 5 = campaign 1 (only campaign).
- Campaign return after a mission: `FUN_004ca384` (mission end dialog) reads map campaign id at map+0x124
  (map flags +0x120 bit 2 = campaign map) and sets ini key `start_campaign_N_screen` (N = 1..4; **1 still present**);
  `FUN_0040212b` consumes keys 2/3/4 → screen 5/6/7. Key 1 has no consumer in Saga (c2: key 1 → screen 5).

### Campaign map screen data (hard-coded)
Mission markers: `{missionId, x, y}` (800×600 coords, centred on the real screen), terminator 0:

| campaign | marker table | missions | bg frame in `LS_menu_logos.bmd` | path-overlay table `{missionId, frame}` |
|---|---|---|---|---|
| c2:1 | c2:`0x4fc2e0` | 10,20..80,100,110 | 2 (c2 bmd) | c2:`0x4fcb48` → frames 3..11 |
| 2 (Northland) | `0x50ba18` | 10..80 | 2 | `0x4faeb0` → frames 3..9 |
| 3 (8th Wonder) | `0x50ba88` | 10..80 | 10 | `0x4faef0` → frames 13..19 |
| 4 (Saga) | `0x50baf8` | 10..50 | 11 | `0x4faf30` → frames 20..23 |

Overlay drawn when `FUN_00417caa(campaignStatic DAT_005146a0, campaign, missionId)` (= mission finished).
Marker button: `FUN_004ba543(.., id 0x29, .., cmd 0x139c, ..)`, enabled when the same predicate holds.
Bmd: `data\gui\lang\%s\bobs\LS_menu_logos.bmd` (`DAT_0056c6ac`), palettes `campaignmap.pcx` (`DAT_0056c6b8`), `campaignbuttons.pcx` (`DAT_0056c6bc`).

**C2 campaign port (Phase 3) recipe**: append c2 bmd frames 2..11 to the Saga bmd (as frames 24..33), add a screen id
(e.g. 8 is taken — pick a free id or re-route: key `start_campaign_1_screen` → new screen), with a marker table copied
from c2 `0x4fc2e0` and overlay table `{20,25}…{110,33}`; hook `FUN_0040212b`, `FUN_004d4b40`, `FUN_004dc151`.
**Implemented** in `loader/cx.c`: screen 5 is reused in C2 mode (patch sites 0x4d5839/57/6c/8c/95 and 0x4dc1e9/f1/200), menu command 5006 = jump-table slot 6 (`0x4d8b02+6*4`, normally the 0x4d8af9 no-op), detours on `FUN_004d4499` (switch), `FUN_004d4b40` (build; adds the button on screen 3) and `FUN_0040212b` (key 1). Widget: rect x/y/w/h at +4..+0x10, command +0x3c; list node {next, widget} with head at list+8.

## Campaign data
- `CCampaignStaticDataManager` (`DAT_005146a0`): `campaign.ini` with `[campaign_variables]`, `[campaign_sub_missions]`
  (load `FUN_00417b5c`/`FUN_00417ce0`, save `FUN_00417ea4`). `FUN_00417caa(this, campaign, mission)` → mission done.
- User campaigns: `datax\usercampaigns\campaign00|01` (`FUN_004118f9`).
- Map manager `DAT_00513c0c` (`FUN_00411604(mgr, guid)` → map record; `+0x120` flags, `+0x124` campaign, `+0x128` mission, `+0x386` file name).

## Hypertext
- `FUN_004a61ba` loads `hypertext\history` and `hypertext\mythology` (w8: history only). C2 campaign history pages
  collide with 8W's `history` set → Phase 3 needs a path rename (e.g. `history_c2`) + hook.

## Open items
- In-game minimap: no string anchor; locate via x32dbg (break on blits of the minimap rect / memory writes to its surface).
- In-game GUI layout and its dependence on screen size (Stage B scaling).
- Archive lookup precedence for duplicate names (overlay strategy).
- Saga vs w8 code diff for bug fixes (w8 is 3 weeks newer but lacks campaigns 2, user campaigns, mythology).

**FMV**: path `datax\FMV\<lang>\<name>.mpg` (FUN_00402bc9 / FUN_0040337d via the resolver FUN_0040679c, eng→ger fallback). FUN_00403100 plays intro01/02/03 before campaign 2/3/4 mission 10 (cfg key `start_pre_ingame_fmv`). Campaign check at 0x4031a7 (`cmp [edi+0x124],2; jne`); cx.dll detours it to add campaign 1 → intro00. Script action 0x30 sets `start_fmv` = `seq_%4.4d`. C2 uses Bink (.bik), which the Saga has no player for: build.py converts it.
**History book**: the 8W `hypertext\history\index.hlt` lists `history_*.hlt` pages; C2 uses `campaign_01_*.hlt` + `history.txt` in the same folder. The names don't clash, so build.py merges the indexes.
**Scenario lists**: `FUN_004118f9` (map manager `DAT_00513c0c`) scans `data\maps\*` for `map.ini` through the VFS (libs included), then `UserMaps\` and `datax\usercampaigns\campaign00`. `[misc_maptype] maptype N` sets bit N in the map flags at map+0x120 (the list sections). The display name is `[misc_mapname] mapnamestringid`, resolved in the map's own `text\<lang>\strings.cif`.
