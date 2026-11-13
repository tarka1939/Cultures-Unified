"""Assemble the playable build from the user's own game copies (sources are only read).

  python build.py [--saga D:/Cultures_Saga] [--gog "D:/GOG Games"] [--out build] [--fullscreen]

Phase 1 output (build/):
  * a copy of the Cultures Saga install (Game.exe CD check patched),
  * Game.ini with set_language 1 (eng),
  * DataX/Libs/data0002.lib - overlay holding only data\\...\\eng\\... files:
      - English text/graphics taken from the GOG 8th Wonder / Northland archives for every Saga
        language file that has an English counterpart (global strings, hypertext, shared maps),
      - Saga-only graphics (menu logos / title pictures) copied unchanged under the eng path,
      - our translations (translations/eng) for campaign 4 and saga001.
"""
import argparse, json, os, re, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import libtool, bmd, peimport  # noqa: E402
sys.path.insert(0, os.path.join(ROOT, 'loader'))
import make as cx_make  # noqa: E402

BS = '\\'
LANG = re.compile(r'\\ger\\', re.I)
# Saga's own versions must win over the 8W/NL English ones: the menu bank holds the campaign 2/4 map frames
# (8W's lacks them), the pictures carry the Saga branding.
SAGA_ONLY = ('bobs\\ls_menu_logos.bmd', 'pictures\\titlescreen.bmp', 'pictures\\loadscreen.bmp')
SKIP_INSTALL ={'autorun.exe', 'register.exe', 'unregister.exe', 'uninst.isu', 'uninstall.dll', 'funatics website.url'}

# Game.exe CD check: FUN_004d43bd (CD present && extro.dat ok) -> return true. See re/NOTES.md.
PATCHES = {
    'Game.exe': [(0x4d43bd, bytes.fromhex('e8fde3f2ff84c07501c3'), bytes.fromhex('b001c3'))],
}


def log(*a):
    print(*a, flush=True)


# ------------------------------------------------------------------ install copy
def copy_install(src, dst):
    n = 0
    for d, dirs, files in os.walk(src):
        rel = os.path.relpath(d, src)
        if rel.split(os.sep)[0].lower() == 'editor':
            dirs[:] = []; continue
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
        for f in files:
            if rel == '.' and f.lower() in SKIP_INSTALL:
                continue
            s, t = os.path.join(d, f), os.path.join(dst, rel, f)
            st = os.stat(s)
            if os.path.exists(t):
                tt = os.stat(t)
                if tt.st_size == st.st_size and int(tt.st_mtime) == int(st.st_mtime):
                    continue
                os.chmod(t, 0o666)
            shutil.copy2(s, t); os.chmod(t, 0o666); n += 1
    log(f'install: {n} files copied/updated')


def patch_exe(path, patches):
    b = bytearray(open(path, 'rb').read())
    sys.path.insert(0, os.path.join(ROOT, 're')); import pe
    p = pe.PE(path)
    for va, orig, new in patches:
        o = p.off(va)
        if b[o:o + len(new)] == new and b[o + len(new):o + len(orig)] == orig[len(new):]:
            continue  # already patched
        if b[o:o + len(orig)] != orig:
            raise SystemExit(f'{path}: unexpected bytes at {va:#x}: {b[o:o+len(orig)].hex()}')
        b[o:o + len(new)] = new
    open(path, 'wb').write(b)
    log(f'patched {os.path.basename(path)}: {len(patches)} patch(es)')


# ------------------------------------------------------------------ overlay
def eng_name(name):
    return LANG.sub(lambda m: BS + 'eng' + BS, name, count=1)


def menu_bank_with_c2(saga_bmd, c2_bmd):
    """Saga LS_menu_logos.bmd + the C2 campaign path frames 3..11 appended as frames 26..34 (cx.dll c2_paths)."""
    s, c = bmd.Bmd.parse(saga_bmd), bmd.Bmd.parse(c2_bmd)
    if len(s.frames) != 26:
        raise SystemExit(f'LS_menu_logos.bmd: expected 26 frames, found {len(s.frames)}')
    s.frames += c.frames[3:12]
    return s.build()


# C2 maps brought into the Saga data (campaign 1 + scenarios); campaign_01_09 is briefings only (no map).
C2_MAPS = ('campaign_01_', 'singleplayer_01_', 'multiplayer_0')
# Northland maps the Saga left out (christmas 2002 scenario).
NL_EXTRA_MAPS = ('weinachten_2002',)
# Scenario/multiplayer maps (maptype 2 and 4) get their game of origin in front of the name in the unified lists.
# Folder naming: singleplayer_<game>_xx / multiplayer_<game>_xx_..., multiplayer_<game-1><nn>_... (0 C2, 1 NL, 2 8W).
ORIGIN_TAGS = {'01': '[C2] ', '02': '[C3] ', '03': '[C4] '}  # Northland = Cultures 3, 8th Wonder = Cultures 4
ORIGIN_EXTRA = {'demo_singleplayer_02': '02', 'weinachten_2002': '02'}


def map_origin(folder):
    f = folder.lower()
    if f in ORIGIN_EXTRA:
        return ORIGIN_EXTRA[f]
    m = re.match(r'(?:singleplayer|multiplayer)_(\d\d)_', f)
    if m:
        return m.group(1)
    m = re.match(r'multiplayer_(\d)\d\d_', f)
    return f'0{int(m.group(1)) + 1}' if m else None


def tag_scenarios(ov, sg, put):
    """Prefix each scenario/multiplayer map name with its origin, e.g. '[C2] HAITABU'."""
    files = {**{n.lower(): (sg, n) for n in sg.names()}, **{n.lower(): (ov, n) for n in ov.files}}
    maps = BS.join(['data', 'maps', ''])
    folders = {n.split(BS)[2] for n in files if n.startswith(maps) and n.endswith(BS + 'map.cif')}
    for f in sorted(folders):
        cif = libtool.cif_decode(files[maps + f + BS + 'map.cif'][0].read(files[maps + f + BS + 'map.cif'][1]))
        types = re.findall(r'^maptype (\d+)', cif, re.M)
        nid = re.search(r'^mapnamestringid (\d+)', cif, re.M)
        origin = map_origin(f)
        if not nid or not origin or not set(types) & {'2', '4'}:
            continue
        key = BS.join([maps + f, 'text', 'eng', 'strings.cif'])
        if key not in files:
            raise SystemExit(f'tag: {f} has no English strings.cif')
        a, real = files[key]
        text = libtool.cif_decode(a.read(real))
        tag = ORIGIN_TAGS[origin]
        new, n = re.subn(r'^(stringn? %s ")(?!\[)' % nid.group(1), lambda m: m.group(1) + tag, text, count=1,
                         flags=re.M)
        if n != 1:
            raise SystemExit(f'tag: {f}: name string {nid.group(1)} not found')
        put(real, libtool.cif_encode(new), 'tagged-names')


def build_overlay(saga_lib, eng_libs, translations, manifest, out, c2_lib):
    sg = libtool.Archive.open(saga_lib)
    c2 = libtool.Archive.open(c2_lib)
    sources = []  # (label, archive, {lower eng name: real name})
    for label, p in eng_libs:
        a = libtool.Archive.open(p)
        sources.append((label, a, {n.lower(): n for n in a.names() if (BS + 'eng' + BS) in n.lower()}))
    ov = libtool.Archive()
    stats = {}

    def put(name, data, how):
        ov.set(name, data); stats[how] = stats.get(how, 0) + 1

    translated = {}
    for rel, m in manifest.items():
        if m['container'] != 'saga_lib':
            continue
        tp = os.path.join(translations, *rel.split('/')[1:])
        if os.path.exists(tp):
            translated[m['internal'].lower()] = (tp, m)

    for name in sg.names():
        if not LANG.search(name):
            continue
        en = eng_name(name)
        if name.lower() in translated:
            tp, m = translated[name.lower()]
            text = open(tp, encoding='utf-8').read()
            if m['kind'] == 'cif':
                data = libtool.cif_encode(text)
            else:
                data = text.replace('\n', '\r\n').encode('cp1252')
            put(en, data, 'translation'); continue
        if any(s in name.lower() for s in SAGA_ONLY):
            data = sg.read(name)
            if name.lower().endswith('ls_menu_logos.bmd'):
                data = menu_bank_with_c2(data, c2.read(eng_name(name)))
            put(en, data, 'saga-copy'); continue
        for label, a, idx in sources:
            hit = idx.get(en.lower())
            if hit:
                put(en, a.read(hit), label); break
        else:
            put(en, sg.read(name), 'saga-copy')
    # C2 campaign history pages share the history book with the mythology pages: no file names collide except
    # index.hlt (fonts/palettes are byte-identical), so add them and append C2's index entries to the index.
    hist = BS.join(['data', 'text', 'eng', 'hypertext', 'history', ''])
    have = {n.lower(): n for n in ov.files}
    idx = have.get(hist + 'index.hlt')
    if not idx:
        raise SystemExit('overlay: no English history index to merge into')
    for name in c2.names():
        if name.lower().startswith(hist) and name.lower() not in have:
            put(name, c2.read(name), 'c2-history')
    c2_idx = c2.read(hist + 'index.hlt').split(b'\n', 1)[1]          # drop its leading <anchor:0>
    ov.set(idx, ov.read(idx).rstrip(b'\r\n') + b'\r\n\r\n' + c2_idx)
    for name in c2.names():
        parts = name.lower().split(BS)
        if parts[:2] == ['data', 'maps'] and len(parts) > 3 and parts[2].startswith(C2_MAPS):
            put(name, c2.read(name), 'c2-maps')
    nl = next(a for label, a, _ in sources if label == 'nl')
    for name in nl.names():
        parts = name.lower().split(BS)
        if parts[:2] == ['data', 'maps'] and len(parts) > 3 and parts[2] in NL_EXTRA_MAPS:
            put(name, nl.read(name), 'nl-extra-maps')
    tag_scenarios(ov, sg, put)
    ov.save(out)
    log(f'overlay: {len(ov.files)} files -> {out}')
    for k, v in sorted(stats.items()):
        log(f'   {k:12s} {v}')
    return stats


def encode_translation(path, kind):
    text = open(path, encoding='utf-8').read()
    return libtool.cif_encode(text) if kind == 'cif' else text.replace('\n', '\r\n').encode('cp1252')


# The Saga ships the user-campaign maps with their umlauts lost (U+FFFD on disk); restore them by file prefix.
UMLAUT_NAMES = {
    ('Campaign00', '08'): '08_Österreich.c2m', ('Campaign00', '12'): '12_Die_Prüfung.c2m',
    ('Campaign00', '13'): '13_Das_Tal_der_Könige.c2m', ('Campaign00', '20'): '20_Die_Höhle.c2m',
    ('Campaign00', '21'): '21_Südafrika.c2m', ('Campaign01', '04'): '04_Die_Händler.c2m',
    ('Campaign01', '05'): '05_Die_Münzsammler.c2m',
}


def build_usercampaigns(saga, out, translations, manifest):
    """Yogi / Yogi Part 2: add currentusermap\\text\\eng\\ (copy of ger with English briefings + strings)."""
    by_container = {}
    for rel, m in manifest.items():
        if m['container'].lower().startswith('datax\\usercampaigns\\'):
            tp = os.path.join(translations, *rel.split('/')[1:])
            if os.path.exists(tp):
                by_container.setdefault(m['container'], {})[m['internal'].lower()] = (tp, m['kind'])
    n = 0
    for camp in ('Campaign00', 'Campaign01'):
        sd, od = os.path.join(saga, 'DataX', 'UserCampaigns', camp), os.path.join(out, 'DataX', 'UserCampaigns', camp)
        os.makedirs(od, exist_ok=True)
        for f in sorted(os.listdir(sd)):
            fixed = UMLAUT_NAMES.get((camp, f[:2]), f) if '�' in f else f
            if '�' in fixed:
                raise SystemExit(f'no restored name for {camp}\\{f}')
            if fixed != f and os.path.exists(os.path.join(od, f)):
                os.chmod(os.path.join(od, f), 0o666); os.remove(os.path.join(od, f))
            a = libtool.Archive.open(os.path.join(sd, f))
            tr = by_container.get(f'DataX\\UserCampaigns\\{camp}\\{f}', {})
            if not tr:
                raise SystemExit(f'no translation for {camp}\\{f}')
            for name in list(a.names()):
                if not LANG.search(name):
                    continue
                t = tr.get(name.lower())
                a.set(eng_name(name), encode_translation(*t) if t else a.read(name))
            dst = os.path.join(od, fixed)
            if os.path.exists(dst):
                os.chmod(dst, 0o666)
            a.save(dst); n += 1
    log(f'user campaigns: {n} maps with English text')


# English versions of the Saga's campaign videos (same films as the GOG Northland / 8th Wonder ones; sizes match
# within a few KB). Saga-only intro.mpg has no English counterpart: the engine falls back to FMV\Ger.
FMV_ENG = {'intro01.mpg': ('Northland', 'intro.mpg'), 'Seq_0001.mpg': ('Northland', 'Seq_0000.mpg'),
           'intro02.mpg': ('8th Wonder of the World', 'intro.mpg'),
           'Seq_0002.mpg': ('8th Wonder of the World', 'Seq_0000.mpg')}


def install_fmv(nw, c2_dir, out):
    d = os.path.join(out, 'DataX', 'FMV', 'Eng')
    os.makedirs(d, exist_ok=True)
    for dst, (game, src) in FMV_ENG.items():
        s, t = os.path.join(nw, game, 'DataX', 'FMV', 'Eng', src), os.path.join(d, dst)
        if not (os.path.exists(t) and os.path.getsize(t) == os.path.getsize(s)):
            shutil.copy2(s, t); os.chmod(t, 0o666)
    log(f'videos: {len(FMV_ENG)} English campaign films')
    install_c2_fmv(c2_dir, d)


# Cultures 2 ships Bink videos, which the Saga can't play: convert them to the Saga's MPEG-1 format (640x480 25 fps,
# MP2 48 kHz). intro00 = C2's game intro, shown before campaign 1 mission 10 (cx.dll); Seq_0000 = C2's ending, which
# the C2 maps request as sequence 0 (the Saga's own films are 1 and 2).
C2_FMV = {'intro00.mpg': 'Intro.bik', 'Seq_0000.mpg': 'Seq_0000.bik'}


def find_ffmpeg():
    local = os.path.join(ROOT, '.tools', 'ffmpeg', 'ffmpeg.exe')
    return local if os.path.exists(local) else shutil.which('ffmpeg')


def install_c2_fmv(c2_dir, d):
    ff = find_ffmpeg()
    src_dir = os.path.join(c2_dir, 'DataX', 'FMV', 'Eng')
    if not ff:
        log('videos: no ffmpeg (.tools/ffmpeg or PATH): Cultures 2 videos skipped')
        return
    n = 0
    for dst, src in C2_FMV.items():
        s, t = os.path.join(src_dir, src), os.path.join(d, dst)
        if os.path.exists(t) and os.path.getmtime(t) >= os.path.getmtime(s):
            continue
        r = subprocess.run([ff, '-v', 'error', '-y', '-i', s, '-r', '25', '-c:v', 'mpeg1video', '-b:v', '3000k',
                            '-maxrate', '3000k', '-bufsize', '640k', '-c:a', 'mp2', '-ar', '48000', '-ac', '2',
                            '-b:a', '192k', '-f', 'mpeg', t + '.tmp'], capture_output=True, text=True)
        if r.returncode:
            raise SystemExit(f'ffmpeg failed on {src}:\n{r.stderr}')
        os.replace(t + '.tmp', t); n += 1
    log(f'videos: Cultures 2 films converted ({n} new)')


def install_cx(out):
    """cx.dll (loader/cx.c: C2 campaign screen hooks) next to Game.exe, imported by it."""
    shutil.copy2(cx_make.build(), os.path.join(out, 'cx.dll'))
    exe = os.path.join(out, 'Game.exe')
    b = open(exe, 'rb').read()
    nb = peimport.add_import(b, 'cx.dll', 'cx_version')
    if nb != b:
        open(exe, 'wb').write(nb)
    log('cx.dll installed and imported by Game.exe')


def set_option(out, key, value):
    """Set one key in Saves\\opt_game.ini (the in-game options file, FUN_004e3abd), keeping the player's others."""
    p = os.path.join(out, 'Saves', 'opt_game.ini')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    lines = open(p, encoding='cp1252').read().splitlines() if os.path.exists(p) else []
    lines = [l for l in lines if l.split(' ', 1)[0].lower() != key] + [f'{key} {value}']
    open(p, 'w', encoding='cp1252', newline='\r\n').write('\n'.join(lines) + '\n')


def install_ddraw(src, out, window):
    """cnc-ddraw (DirectDraw re-implementation): fixes windowed-mode artifacts and upscales the 800x600 game.
    window: None -> borderless fullscreen (aspect kept), 'WxH' -> scaled window of that size."""
    for f in ('ddraw.dll',):
        shutil.copy2(os.path.join(src, f), os.path.join(out, f))
    if not os.path.isdir(os.path.join(out, 'Shaders')):
        shutil.copytree(os.path.join(src, 'Shaders'), os.path.join(out, 'Shaders'))
    ini = open(os.path.join(src, 'ddraw.ini'), encoding='utf-8', errors='replace').read()
    # options are set in the global [ddraw] section, which ends at the first per-game section
    first_game = ini.index('\n[', ini.index('[ddraw]') + 7)
    sect, tail = ini[:first_game], ini[first_game:]
    w, h = (window.split('x') if window else ('0', '0'))
    opts = {'windowed': 'true', 'fullscreen': 'false' if window else 'true', 'width': w, 'height': h,
            'maintas': 'true', 'adjmouse': 'true', 'renderer': 'auto', 'savesettings': '0'}
    for k, v in opts.items():
        sect, n = re.subn(rf'(?m)^{k}=.*$', lambda m: f'{k}={v}', sect, count=1)
        if not n:
            raise SystemExit(f'ddraw.ini: option {k} not found')
    open(os.path.join(out, 'ddraw.ini'), 'w', encoding='utf-8', newline='\r\n').write(sect + tail)
    log(f"cnc-ddraw installed ({'window ' + window if window else 'borderless fullscreen'})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--saga', default='D:/Cultures_Saga')
    ap.add_argument('--gog', default='D:/GOG Games')
    ap.add_argument('--out', default=os.path.join(ROOT, 'build'))
    ap.add_argument('--ddraw', default=os.path.join(ROOT, '.tools', 'cnc-ddraw'),
                    help='extracted cnc-ddraw release (ddraw.dll, ddraw.ini, Shaders)')
    ap.add_argument('--no-ddraw', action='store_true', help='use the system DirectDraw (original behaviour)')
    ap.add_argument('--window', metavar='WxH', help='with cnc-ddraw: scaled window instead of borderless fullscreen')
    ap.add_argument('--fullscreen', action='store_true', help='without cnc-ddraw: exclusive fullscreen')
    ap.add_argument('--hw-cursor', action='store_true',
                    help='use the hardware cursor (leaves artifacts around the pointer on modern Windows)')
    a = ap.parse_args()
    use_ddraw = not a.no_ddraw
    nw = os.path.join(a.gog, 'Northland and 8th Wonder of the World')
    eng_libs = [('8w', os.path.join(nw, '8th Wonder of the World', 'DataX', 'Libs', 'data0001.lib')),
                ('nl', os.path.join(nw, 'Northland', 'DataX', 'Libs', 'data0001.lib'))]

    copy_install(a.saga, a.out)
    for exe, pl in PATCHES.items():
        patch_exe(os.path.join(a.out, exe), pl)
    install_cx(a.out)
    install_fmv(nw, os.path.join(a.gog, 'Cultures and Cultures 2', 'Cultures 2'), a.out)
    with open(os.path.join(a.out, 'Game.ini'), 'w', newline='\r\n') as f:
        f.write('set_language 1\n')
        # with cnc-ddraw the game must request fullscreen; the wrapper decides window/borderless/scaling
        f.write(f'gfx_fullscreen {1 if (use_ddraw or a.fullscreen) else 0}\n')
    set_option(a.out, 'gui_mouse_software', 0 if a.hw_cursor else 1)
    if use_ddraw:
        install_ddraw(a.ddraw, a.out, a.window)
    else:
        for f in ('ddraw.dll', 'ddraw.ini'):
            if os.path.exists(os.path.join(a.out, f)):
                os.remove(os.path.join(a.out, f))
    manifest = json.load(open(os.path.join(ROOT, 'work', 'manifest.json'), encoding='utf-8'))
    build_overlay(os.path.join(a.saga, 'DataX', 'Libs', 'data0001.lib'), eng_libs,
                  os.path.join(ROOT, 'translations', 'eng'), manifest,
                  os.path.join(a.out, 'DataX', 'Libs', 'data0002.lib'),
                  os.path.join(a.gog, 'Cultures and Cultures 2', 'Cultures 2', 'DataX', 'Libs', 'data0001.lib'))
    build_usercampaigns(a.saga, a.out, os.path.join(ROOT, 'translations', 'eng'), manifest)
    log('done:', a.out)


if __name__ == '__main__':
    main()
