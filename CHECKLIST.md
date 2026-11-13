# Play-test checklist

Run after a build that changes the loader, the overlay or the translations. Start `build\Game.exe` by hand.
Write what fails, with the map and a screenshot, into PLAN.md under the phase it belongs to.
Marks: ✅ seen working, ⬜ not checked yet.

## Start and display

- ✅ The game starts borderless fullscreen at the desktop resolution, with no CD prompt.
- ✅ The menus are English and sharp (TrueType text), and the GUI is at a comfortable size.
- ✅ The pointer has no trails and clicks land where it points (menus and in game).
- ⬜ Options > Video: picking another resolution and restarting keeps the GUI at the same size.
- ⬜ Alt+Tab out and back in, in a menu and in game.
- ⬜ `python build.py --window 1600x1000`: a window, the pointer can leave it, and Alt+Tab and Win+D work.
- ⬜ The loading screen shows no German copyright notice (known: it still does).

## In game (any map)

- ✅ The world is at full display resolution, and zoom in and out work.
- ✅ The minimap is twice the size and sharp, with a normal-size frame and buttons. Clicking it moves the view.
- ⬜ The minimap buttons (zoom, filters) work, and its picture updates when buildings go up.
- ⬜ Tooltips don't cover each other (known: they overlap).
- ⬜ Settler names and selection marks over the world are readable (known: they are at GUI scale).
- ⬜ Save and load a game, then quit to the menu and start another map.

## Campaigns

- ✅ Single player shows the Gates of Asgard, Northland, 8th Wonder, Saga and user campaign buttons.
- ⬜ **Gates of Asgard** (campaign 1): the campaign map shows C2's markers and paths, and the first mission starts
  with an English briefing. Play it through.
  - ⬜ Mission 10 is preceded by the Cultures 2 intro film.
  - ⬜ The ending film plays after the last mission.
  - ⬜ The C2 history pages open from the help.
- ⬜ **Northland** (campaign 2) and **8th Wonder** (campaign 3): their intro films are English, and the first mission
  starts.
- ⬜ **Saga** (campaign 4): all 14 missions. The briefings, objectives and messages are English and fit their windows.
- ⬜ **User campaigns** Yogi00 (22 maps) and Yogi01 (6 maps): each map loads with English text, and the names with
  umlauts (`08_Österreich`, `12_Die_Prüfung`, ...) appear.

## Scenarios and multiplayer

- ⬜ Free Play lists 22 scenarios, tagged `[C2]`, `[C3]` or `[C4]`, in a sensible order.
- ⬜ A `[C2]` scenario starts and plays.
- ⬜ The multiplayer list shows the 30 tagged maps. A LAN game between two copies starts.

## Feature switches (`build\cx.ini`)

Edit the key, start the game, check, and set it back.

- ⬜ `c2_campaign=0`: the Gates of Asgard button is gone. Northland's campaign map is the original one.
- ⬜ `fmv=0`: no film before Gates of Asgard mission 10.
- ⬜ `world=scaled`: the world is drawn at GUI size and scaled up. Clicking units still hits them.
- ⬜ `minimap=1`: the original minimap size. `minimap_hires=0`: the picture is blocky and clicks still match.
- ⬜ Without `render=native`: the older `ui_scale` mode starts.
- ⬜ `trace=1` writes `cx_trace.log`.
