"""Cultures Unified setup wizard: builds the game from the player's own installs into a new folder.

Frozen into one exe by installer/make_setup.py, with build.py, tools/, a prebuilt cx.dll, translations/eng, the text
manifest and cnc-ddraw. It holds no game files. The sources are only read.

  Setup.exe                 the wizard (also repairs or rebuilds an existing install)
  Setup.exe --uninstall     removes an install (the uninstall entry in Windows Settings runs this)
"""
import ctypes, hashlib, os, queue, shutil, subprocess, sys, threading, traceback, webbrowser, winreg
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

HERE = os.path.dirname(os.path.abspath(__file__))
RES = getattr(sys, '_MEIPASS', os.path.dirname(HERE))
sys.path.insert(0, RES)
import build  # noqa: E402

APP = 'Cultures Unified'
KEY = r'Software\Microsoft\Windows\CurrentVersion\Uninstall\CulturesUnified'
SETUP_NAME = 'Cultures Unified Setup.exe'
NO_WINDOW = 0x08000000

# What the build was made against. Other versions get a clear message instead of a broken build.
SAGA_EXE = (1159168, '1ac4f291e80fd29f')
LIBS = {
    'saga': ('DataX/Libs/data0001.lib', 320600201),
    'c2': ('DataX/Libs/data0001.lib', 242861354),
    'nl': ('Northland/DataX/Libs/data0001.lib', 234653915),
    '8w': ('8th Wonder of the World/DataX/Libs/data0001.lib', 255230793),
}


# ------------------------------------------------------------------ source checks
def sha16(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()[:16]


def lib_problem(folder, key):
    rel, size = LIBS[key]
    p = os.path.join(folder, *rel.split('/'))
    if not os.path.exists(p):
        return f'{rel} is missing'
    if os.path.getsize(p) != size:
        return f'{rel} is an unknown version ({os.path.getsize(p):,} bytes, expected {size:,})'
    return None


def check_saga(d):
    """-> (folder to use, problem or None)"""
    exe = os.path.join(d, 'Game.exe')
    if not os.path.exists(exe):
        return d, 'Game.exe is missing: pick the "Cultures - Die Saga" install folder'
    if os.path.getsize(exe) != SAGA_EXE[0] or sha16(exe) != SAGA_EXE[1]:
        return d, 'Game.exe is an unknown version (the build needs the German Saga release of 5 Nov 2003)'
    if not os.path.isdir(os.path.join(d, 'DataX', 'UserCampaigns')):
        return d, 'DataX\\UserCampaigns is missing'
    return d, lib_problem(d, 'saga')


def check_c2(d):
    if os.path.isdir(os.path.join(d, 'Cultures 2')) and not os.path.exists(os.path.join(d, 'Cultures2.exe')):
        d = os.path.join(d, 'Cultures 2')  # the GOG "Cultures and Cultures 2" folder was picked
    if not os.path.exists(os.path.join(d, 'Cultures2.exe')):
        return d, 'Cultures2.exe is missing: pick the GOG "Cultures and Cultures 2" folder'
    return d, lib_problem(d, 'c2')


def check_nw(d):
    if not os.path.isdir(os.path.join(d, 'Northland')) and os.path.basename(d.rstrip('\\/')).lower() in (
            'northland', '8th wonder of the world'):
        d = os.path.dirname(d.rstrip('\\/'))  # one of the two game folders was picked
    for k in ('nl', '8w'):
        pr = lib_problem(d, k)
        if pr:
            return d, pr + ': pick the GOG "Northland and 8th Wonder of the World" folder'
    for game in ('Northland', '8th Wonder of the World'):
        for f in ('intro.mpg', 'Seq_0000.mpg'):
            if not os.path.exists(os.path.join(d, game, 'DataX', 'FMV', 'Eng', f)):
                return d, f'{game}\\DataX\\FMV\\Eng\\{f} is missing'
    return d, None


def registry_dirs():
    """Install folders of every program whose uninstall entry mentions Cultures, Northland or 8th Wonder."""
    out = []
    for root, path in ((winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall'),
                       (winreg.HKEY_LOCAL_MACHINE, r'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall'),
                       (winreg.HKEY_CURRENT_USER, r'Software\Microsoft\Windows\CurrentVersion\Uninstall')):
        try:
            k = winreg.OpenKey(root, path)
        except OSError:
            continue
        for i in range(winreg.QueryInfoKey(k)[0]):
            try:
                sk = winreg.OpenKey(k, winreg.EnumKey(k, i))
                name = winreg.QueryValueEx(sk, 'DisplayName')[0]
                loc = winreg.QueryValueEx(sk, 'InstallLocation')[0]
            except OSError:
                continue
            if any(w in name.lower() for w in ('cultures', 'northland', '8th wonder')) and loc:
                out.append(loc)
    return out


def guess(check, names):
    """First folder that passes check, from the registry and the usual places on every drive."""
    cands = registry_dirs()
    for drive in 'CDEFGHIJ':
        for base in ('', 'GOG Games', 'Games', 'Program Files (x86)', 'Program Files',
                     'Program Files (x86)\\GOG Galaxy\\Games', 'Program Files (x86)\\GOG.com'):
            for n in names:
                cands.append(os.path.join(f'{drive}:\\', base, n))
    for c in cands:
        if os.path.isdir(c) and check(c)[1] is None:
            return check(c)[0]
    return ''


# Every run logs to %TEMP%\CulturesUnifiedSetup.log (the wizard also copies it to <install>\setup.log): the
# folders found and their checks, the build arguments and output, and any error, including errors inside the
# window, which a windowed exe would otherwise swallow.
LOGFILE = os.path.join(os.environ.get('TEMP', os.path.expanduser('~')), 'CulturesUnifiedSetup.log')
_log_lock = threading.Lock()


def flog(s):
    import datetime
    with _log_lock, open(LOGFILE, 'a', encoding='utf-8') as f:
        f.write(f'{datetime.datetime.now():%H:%M:%S} {s}\n')


FFMPEG_PAGE ='https://ffmpeg.org/download.html#build-windows'


def bundled_args():
    """build.py arguments for what the setup exe carries; run from the repo, build.py's own defaults are used"""
    if not getattr(sys, 'frozen', False):
        return []
    return ['--cx-dll', os.path.join(RES, 'cx.dll'), '--manifest', os.path.join(RES, 'manifest.json'),
            '--ddraw', os.path.join(RES, 'cnc-ddraw')]


def find_ffmpeg():
    """ffmpeg.exe on PATH, next to Setup, from winget, or in the usual unzip places; '' if none."""
    cands = [shutil.which('ffmpeg') or '',
             os.path.join(os.path.dirname(os.path.abspath(sys.executable)), 'ffmpeg.exe'),
             os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet', 'Links', 'ffmpeg.exe')]
    pk = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'Microsoft', 'WinGet', 'Packages')
    if os.path.isdir(pk):
        for d in os.listdir(pk):
            if d.lower().startswith('gyan.ffmpeg') or 'ffmpeg' in d.lower():
                for root, _, files in os.walk(os.path.join(pk, d)):
                    if 'ffmpeg.exe' in files:
                        cands.append(os.path.join(root, 'ffmpeg.exe'))
    for drive in 'CD':
        cands += [f'{drive}:\\ffmpeg\\bin\\ffmpeg.exe', f'{drive}:\\Program Files\\ffmpeg\\bin\\ffmpeg.exe',
                  f'{drive}:\\ProgramData\\chocolatey\\bin\\ffmpeg.exe']
    return next((c for c in cands if c and os.path.isfile(c)), '')


# ------------------------------------------------------------------ install bookkeeping
def installed_dir():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY) as k:
            return winreg.QueryValueEx(k, 'InstallLocation')[0]
    except OSError:
        return None


def register(target):
    exe = os.path.join(target, SETUP_NAME)
    if os.path.abspath(sys.executable).lower() != exe.lower() and getattr(sys, 'frozen', False):
        shutil.copy2(sys.executable, exe)
    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(target) for f in fs) // 1024
    with winreg.CreateKey(winreg.HKEY_CURRENT_USER, KEY) as k:
        for name, v in (('DisplayName', APP), ('InstallLocation', target),
                        ('DisplayIcon', os.path.join(target, 'Game.exe')),
                        ('UninstallString', f'"{exe}" --uninstall'), ('ModifyPath', f'"{exe}"'),
                        ('Publisher', 'Cultures Unified')):
            winreg.SetValueEx(k, name, 0, winreg.REG_SZ, v)
        winreg.SetValueEx(k, 'EstimatedSize', 0, winreg.REG_DWORD, size)
        winreg.SetValueEx(k, 'NoRepair', 0, winreg.REG_DWORD, 1)


def shell_folder(name):
    """Desktop or Programs (Start menu) of the current user."""
    r = subprocess.run(['powershell', '-NoProfile', '-Command', f"[Environment]::GetFolderPath('{name}')"],
                       capture_output=True, text=True, creationflags=NO_WINDOW)
    return r.stdout.strip()


def shortcut_paths():
    return [os.path.join(shell_folder('Desktop'), APP + '.lnk'), os.path.join(shell_folder('Programs'), APP + '.lnk')]


def make_shortcut(lnk, target):
    ps = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:CU_LNK); "
          "$s.TargetPath = Join-Path $env:CU_DIR 'Game.exe'; $s.WorkingDirectory = $env:CU_DIR; $s.Save()")
    subprocess.run(['powershell', '-NoProfile', '-Command', ps], check=True, creationflags=NO_WINDOW,
                   env=dict(os.environ, CU_LNK=lnk, CU_DIR=target))


def is_inside(a, b):
    a, b = os.path.normcase(os.path.abspath(a)), os.path.normcase(os.path.abspath(b))
    return a == b or a.startswith(b.rstrip('\\') + '\\')


# ------------------------------------------------------------------ wizard
class Wizard(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f'{APP} Setup')
        k = self.winfo_fpixels('1i') / 96  # Windows display scaling: the window is sized in real pixels
        self.geometry(f'{int(780 * k)}x{int(600 * k)}')
        self.minsize(int(720 * k), int(560 * k))
        self.prev = installed_dir()
        self.v = {k: tk.StringVar() for k in ('saga', 'c2', 'nw', 'target', 'display', 'win', 'ffmpeg')}
        self.ok = {k: False for k in ('saga', 'c2', 'nw')}
        self.status = {}
        self.b = {k: tk.BooleanVar(value=True) for k in ('accept', 'swcursor', 'videos', 'desktop', 'startmenu')}
        self.b['accept'].set(False)
        self.v['target'].set(self.prev or os.path.join(os.path.expanduser('~'), 'Games', APP))
        self.v['display'].set('borderless')
        self.v['win'].set('1600x1000')
        self.v['ffmpeg'].set(find_ffmpeg())
        self.q = queue.Queue()
        self.body = ttk.Frame(self, padding=16)
        self.body.pack(fill='both', expand=True)
        nav = ttk.Frame(self, padding=(16, 0, 16, 16))
        nav.pack(fill='x')
        self.next_b = ttk.Button(nav, text='Next >', command=self.next)
        self.next_b.pack(side='right')
        self.back_b = ttk.Button(nav, text='< Back', command=self.back)
        self.back_b.pack(side='right', padx=6)
        self.cancel_b = ttk.Button(nav, text='Cancel', command=self.destroy)
        self.cancel_b.pack(side='left')
        self.pages = [self.p_welcome, self.p_sources, self.p_target, self.p_options, self.p_progress, self.p_finish]
        flog(f'previous install: {self.prev}')
        self.page = 0
        self.show()

    def report_callback_exception(self, *e):
        """an error in a button or page: log it and say so, instead of failing silently"""
        s = ''.join(traceback.format_exception(*e))
        flog('ERROR in the window:\n' + s)
        messagebox.showerror(APP, f'Something went wrong:\n\n{e[1]}\n\nDetails are in {LOGFILE}')

    # -- navigation
    def clear(self):
        for w in self.body.winfo_children():
            w.destroy()

    def heading(self, text, sub=''):
        ttk.Label(self.body, text=text, font=('Segoe UI', 14, 'bold')).pack(anchor='w')
        if sub:
            ttk.Label(self.body, text=sub, wraplength=700, justify='left').pack(anchor='w', pady=(4, 12))

    def show(self):
        self.clear()
        self.back_b.state(['!disabled'] if 0 < self.page < 4 else ['disabled'])
        self.next_b.config(text='Install' if self.page == 3 else 'Next >')
        flog(f'page: {self.pages[self.page].__name__[2:]}')
        self.pages[self.page]()

    def next(self):
        if self.page == 2 and not self.target_ok():
            return
        if self.page == len(self.pages) - 1:
            self.destroy()
            return
        self.page += 1
        self.show()

    def back(self):
        self.page -= 1
        self.show()

    def can_next(self, ok):
        self.next_b.state(['!disabled'] if ok else ['disabled'])

    # -- pages
    def p_welcome(self):
        self.heading(f'Welcome to {APP}',
                     'This puts together one English Cultures that plays the Gates of Asgard (Cultures 2), Northland, '
                     '8th Wonder of the World and Saga campaigns, the two Saga user campaigns and every scenario, at '
                     'your screen resolution.\n\n'
                     'It is built from your own copies of three games, which it only reads:\n'
                     '  •  Cultures – Die Saga (the German release)\n'
                     '  •  Cultures and Cultures 2 (GOG)\n'
                     '  •  Northland and 8th Wonder of the World (GOG)\n\n'
                     'Setup contains no game files. It contains the English translation of the Saga content, '
                     'the cx.dll loader and cnc-ddraw, whose license follows.')
        t = tk.Text(self.body, height=9, wrap='word', font=('Consolas', 9))
        t.insert('1.0', open(os.path.join(RES, 'installer', 'LICENSE-cnc-ddraw.txt'), encoding='utf-8').read())
        t.config(state='disabled')
        t.pack(fill='both', expand=True)
        ttk.Checkbutton(self.body, text='I own these games and accept the license above', variable=self.b['accept'],
                        command=lambda: self.can_next(self.b['accept'].get())).pack(anchor='w', pady=(8, 0))
        self.can_next(self.b['accept'].get())

    def p_sources(self):
        self.heading('Game folders', 'Setup found what it could. Use Browse for the rest. Each folder is checked.')
        rows = (('saga', 'Cultures – Die Saga', check_saga, ('Cultures_Saga', 'Cultures - Die Saga', 'Cultures Saga',
                                                              'Funatics\\Cultures - Die Saga')),
                ('c2', 'Cultures and Cultures 2 (GOG)', check_c2, ('Cultures and Cultures 2',)),
                ('nw', 'Northland and 8th Wonder of the World (GOG)', check_nw,
                 ('Northland and 8th Wonder of the World',)))
        for key, label, check, names in rows:
            f = ttk.Frame(self.body)
            f.pack(fill='x', pady=6)
            ttk.Label(f, text=label, font=('Segoe UI', 10, 'bold')).pack(anchor='w')
            r = ttk.Frame(f)
            r.pack(fill='x')
            ttk.Entry(r, textvariable=self.v[key]).pack(side='left', fill='x', expand=True)
            ttk.Button(r, text='Browse…', command=lambda k=key, c=check: self.browse(k, c)).pack(side='left', padx=4)
            self.status[key] = ttk.Label(f, text='', wraplength=700)
            self.status[key].pack(anchor='w')
            if not self.v[key].get():
                self.v[key].set(guess(check, names))
            self.check(key, check)
            self.v[key].trace_add('write', lambda *_, k=key, c=check: self.check(k, c))

    def browse(self, key, check):
        d = filedialog.askdirectory(initialdir=self.v[key].get() or None, mustexist=True)
        if d:
            self.v[key].set(os.path.normpath(d))

    def check(self, key, check):
        d = self.v[key].get().strip()
        if not d:
            self.ok[key], msg = False, 'Not found yet.'
        elif not os.path.isdir(d):
            self.ok[key], msg = False, 'This folder does not exist.'
        else:
            used, pr = check(d)
            self.ok[key], msg = pr is None, ('✔ ' + used) if pr is None else ('✘ ' + pr)
            flog(f'check {key}: {d} -> {"ok " + used if pr is None else pr}')
            if pr is None and used != d:
                self.v[key].set(used)
                return
        if key in self.status and self.status[key].winfo_exists():
            self.status[key].config(text=msg, foreground='dark green' if self.ok[key] else 'firebrick')
        self.can_next(all(self.ok.values()))

    def p_target(self):
        self.heading('Install folder',
                     'The game is built into this folder. It needs about 800 MB. Saved games are kept here too, so '
                     'pick a folder you can write to (not Program Files).' +
                     (f'\n\nAn existing install was found here. Building again updates it and keeps the saved games.'
                      if self.prev else ''))
        r = ttk.Frame(self.body)
        r.pack(fill='x')
        ttk.Entry(r, textvariable=self.v['target']).pack(side='left', fill='x', expand=True)
        ttk.Button(r, text='Browse…', command=lambda: self.v['target'].set(
            os.path.normpath(filedialog.askdirectory() or self.v['target'].get()))).pack(side='left', padx=4)
        self.can_next(True)

    def target_ok(self):
        t = os.path.abspath(self.v['target'].get().strip())
        for k in ('saga', 'c2', 'nw'):
            src = self.v[k].get()
            if is_inside(t, src) or is_inside(src, t):
                messagebox.showerror(APP, 'The install folder must be outside the game folders: they are only read.')
                return False
        if is_inside(t, os.environ.get('ProgramFiles', 'C:\\Program Files')) or \
                is_inside(t, os.environ.get('ProgramFiles(x86)', 'C:\\Program Files (x86)')):
            messagebox.showerror(APP, 'Program Files needs administrator rights and the game saves into its own '
                                      'folder. Pick another folder.')
            return False
        if os.path.isdir(t) and os.listdir(t) and not os.path.exists(os.path.join(t, 'cx.dll')):
            if not messagebox.askyesno(APP, f'{t} is not empty. Install into it anyway?'):
                return False
        self.v['target'].set(t)
        return True

    def p_options(self):
        self.heading('Options', 'All of these can be changed later by running Setup again.')
        f = ttk.LabelFrame(self.body, text='Display', padding=8)
        f.pack(fill='x')
        ttk.Radiobutton(f, text='Fullscreen at your screen resolution (recommended)', value='borderless',
                        variable=self.v['display']).pack(anchor='w')
        r = ttk.Frame(f)
        r.pack(anchor='w')
        ttk.Radiobutton(r, text='Window, size:', value='window', variable=self.v['display']).pack(side='left')
        ttk.Entry(r, textvariable=self.v['win'], width=12).pack(side='left', padx=4)
        ttk.Radiobutton(f, text='Original: exclusive fullscreen at 800x600, no scaling', value='exclusive',
                        variable=self.v['display']).pack(anchor='w')
        ttk.Checkbutton(self.body, text="Software mouse cursor (the game's own cursor leaves trails on modern Windows)",
                        variable=self.b['swcursor']).pack(anchor='w', pady=(10, 0))
        f = ttk.LabelFrame(self.body, text='Cultures 2 videos', padding=8)
        f.pack(fill='x', pady=10)
        ttk.Label(f, wraplength=680, text='The Cultures 2 intro and ending are Bink videos, which the engine cannot '
                  'play. ffmpeg (not included) can convert them. Without it the game skips those two videos.').pack(
            anchor='w')
        ttk.Checkbutton(f, text='Convert them with ffmpeg.exe:', variable=self.b['videos'],
                        command=self.ff_check).pack(anchor='w')
        r = ttk.Frame(f)
        r.pack(fill='x')
        ttk.Entry(r, textvariable=self.v['ffmpeg']).pack(side='left', fill='x', expand=True)
        ttk.Button(r, text='Browse…', command=lambda: self.v['ffmpeg'].set(os.path.normpath(
            filedialog.askopenfilename(filetypes=[('ffmpeg', 'ffmpeg.exe')]) or self.v['ffmpeg'].get()))).pack(
            side='left', padx=4)
        ttk.Button(r, text='Get ffmpeg', command=lambda: webbrowser.open(FFMPEG_PAGE)).pack(side='left')
        self.ff_status = ttk.Label(f, wraplength=680)
        self.ff_status.pack(anchor='w')
        self.v['ffmpeg'].trace_add('write', lambda *_: self.ff_check())
        self.ff_check()
        f = ttk.LabelFrame(self.body, text='Shortcuts', padding=8)
        f.pack(fill='x')
        ttk.Checkbutton(f, text='Desktop', variable=self.b['desktop']).pack(side='left')
        ttk.Checkbutton(f, text='Start menu', variable=self.b['startmenu']).pack(side='left', padx=12)
        self.can_next(True)

    def ff_ok(self):
        ff = self.v['ffmpeg'].get().strip()
        return self.b['videos'].get() and bool(ff) and os.path.isfile(ff)

    def ff_check(self):
        if not getattr(self, 'ff_status', None) or not self.ff_status.winfo_exists():
            return
        if not self.b['videos'].get():
            self.ff_status.config(text='The Gates of Asgard intro and ending will not play.', foreground='firebrick')
        elif self.ff_ok():
            self.ff_status.config(text='✔ The videos will be converted (a minute or two).', foreground='dark green')
        else:
            self.ff_status.config(text='✘ ffmpeg.exe was not found, so the Gates of Asgard intro and ending will '
                                       'not play. Get ffmpeg (any Windows build), then Browse to its ffmpeg.exe.',
                                  foreground='firebrick')

    def argv(self):
        a = ['--saga', self.v['saga'].get(), '--c2', self.v['c2'].get(), '--nw', self.v['nw'].get(),
             '--out', self.v['target'].get()] + bundled_args()
        if self.v['display'].get() == 'window':
            a += ['--window', self.v['win'].get().lower().replace(' ', '')]
        elif self.v['display'].get() == 'exclusive':
            a += ['--no-ddraw', '--fullscreen']
        if not self.b['swcursor'].get():
            a.append('--hw-cursor')
        if self.ff_ok():
            a += ['--ffmpeg', self.v['ffmpeg'].get().strip()]
        else:
            a.append('--no-c2-videos')
        return a

    def p_progress(self):
        self.heading('Installing', 'Building the game. This takes a minute or two.')
        self.bar = ttk.Progressbar(self.body, mode='indeterminate')
        self.bar.pack(fill='x')
        self.bar.start(12)
        self.log = tk.Text(self.body, height=18, font=('Consolas', 9))
        self.log.pack(fill='both', expand=True, pady=8)
        self.can_next(False)
        self.cancel_b.state(['disabled'])
        import re
        if self.v['display'].get() == 'window' and \
                not re.fullmatch(r'\d{3,5}x\d{3,5}', self.v['win'].get().lower().replace(' ', '')):
            self.q.put(('fail', 'The window size must look like 1600x1000.'))
        else:
            # Tk variables may only be read on this thread: hand the worker plain values
            argv, want = self.argv(), [self.b['desktop'].get(), self.b['startmenu'].get()]
            threading.Thread(target=self.work, args=(argv, want), daemon=True).start()
        self.after(100, self.poll)

    def work(self, argv, want):
        def out(s):
            flog(s)
            self.q.put(('log', s))
        t = None
        try:
            build.LOG = out
            flog('build arguments: ' + ' '.join(f'"{x}"' if ' ' in x else x for x in argv))
            a = build.parse_args(argv)
            t = a.out
            build.run(a)
            out('registering the install and its uninstall entry')
            register(t)
            for lnk, w in zip(shortcut_paths(), want):
                if w:
                    make_shortcut(lnk, t)
                    flog(f'shortcut {lnk}')
                elif os.path.exists(lnk):
                    os.remove(lnk)
            flog('install finished')
            self.q.put(('done', t))
        except SystemExit as e:
            flog(f'FAILED: {e}')
            self.q.put(('fail', f'{e}\n\nLog: {LOGFILE}'))
        except Exception:
            flog('FAILED:\n' + traceback.format_exc())
            self.q.put(('fail', traceback.format_exc() + f'\nLog: {LOGFILE}'))
        finally:
            if t and os.path.isdir(t):  # a copy of the log next to the game
                try:
                    shutil.copy2(LOGFILE, os.path.join(t, 'setup.log'))
                except OSError:
                    pass

    def poll(self):
        try:
            while True:
                kind, s = self.q.get_nowait()
                if kind == 'log':
                    self.log.insert('end', s + '\n')
                    self.log.see('end')
                elif kind == 'done':
                    self.bar.stop()
                    self.page += 1
                    self.show()
                    return
                else:
                    self.bar.stop()
                    self.log.insert('end', '\nSetup failed:\n' + s + '\n')
                    self.log.see('end')
                    self.cancel_b.state(['!disabled'])
                    self.back_b.state(['!disabled'])
                    return
        except queue.Empty:
            pass
        self.after(100, self.poll)

    def p_finish(self):
        t = self.v['target'].get()
        self.heading(f'{APP} is installed', f'Folder: {t}\n\nRun Setup again (Windows Settings > Apps, or '
                     f'{SETUP_NAME} in that folder) to change options or rebuild after a game update.' +
                     ('' if self.ff_ok() else '\n\nThe Cultures 2 videos were not converted, so the Gates of Asgard '
                      'intro and ending will not play. Run Setup again with ffmpeg to add them.'))
        self.play = tk.BooleanVar(value=True)
        ttk.Checkbutton(self.body, text='Start the game now', variable=self.play).pack(anchor='w')
        self.next_b.config(text='Finish', command=self.finish)
        self.cancel_b.state(['disabled'])
        self.can_next(True)

    def finish(self):
        if self.play.get():
            t = self.v['target'].get()
            subprocess.Popen([os.path.join(t, 'Game.exe')], cwd=t)
        self.destroy()


# ------------------------------------------------------------------ uninstall
def uninstall():
    root = tk.Tk()
    root.withdraw()
    t = installed_dir() or os.path.dirname(os.path.abspath(sys.executable))
    if not os.path.exists(os.path.join(t, 'cx.dll')):
        messagebox.showerror(APP, f'No {APP} install found in {t}.')
        return
    if not messagebox.askyesno(APP, f'Remove {APP} from\n{t}?\n\nYour game installs are not touched.'):
        return
    keep = not messagebox.askyesno(APP, 'Delete your saved games and settings too?\n\n'
                                        'No keeps the Saves folder.')
    flog(f'uninstall {t}, keep Saves: {keep}')
    for lnk in shortcut_paths():
        if os.path.exists(lnk):
            os.remove(lnk)
    try:
        winreg.DeleteKey(winreg.HKEY_CURRENT_USER, KEY)
    except OSError:
        pass
    me = os.path.abspath(sys.executable)
    for name in os.listdir(t):
        p = os.path.join(t, name)
        if (keep and name.lower() == 'saves') or os.path.normcase(p) == os.path.normcase(me):
            continue
        try:
            shutil.rmtree(p) if os.path.isdir(p) else os.remove(p)
        except OSError as e:
            flog(f'could not delete {p}: {e}')
    # this exe is running from the folder: delete it, then the folder if empty, once it has exited
    if is_inside(me, t):
        subprocess.Popen(f'cmd /c ping -n 3 127.0.0.1 >nul & del /f /q "{me}" & rmdir "{t}"',
                         creationflags=NO_WINDOW, cwd=os.environ.get('TEMP', 'C:\\'))
    else:
        try:
            os.rmdir(t)
        except OSError:
            pass
    messagebox.showinfo(APP, f'{APP} was removed.' + ('\nThe Saves folder was kept.' if keep else ''))


def unattended(target):
    """Setup.exe --unattended <folder>: found game folders, default options, no shortcuts; log in setup.log there."""
    os.makedirs(target, exist_ok=True)
    with open(os.path.join(target, 'setup.log'), 'w', encoding='utf-8') as log:
        build.LOG = lambda s: (log.write(s + '\n'), log.flush(), flog(s))
        try:
            src = {}
            for key, check, names in (('saga', check_saga, ('Cultures_Saga', 'Cultures - Die Saga')),
                                      ('c2', check_c2, ('Cultures and Cultures 2',)),
                                      ('nw', check_nw, ('Northland and 8th Wonder of the World',))):
                src[key] = guess(check, names)
                if not src[key]:
                    raise SystemExit(f'{key}: game folder not found')
            ff = find_ffmpeg()
            build.run(build.parse_args(
                ['--saga', src['saga'], '--c2', src['c2'], '--nw', src['nw'], '--out', target,
                 ] + bundled_args() + (['--ffmpeg', ff] if ff else ['--no-c2-videos'])))
            register(target)
            log.write('ok\n')
        except BaseException:
            log.write(traceback.format_exc())
            sys.exit(1)


def main():
    import datetime, platform
    try:
        open(LOGFILE, 'w', encoding='utf-8').write(
            f'{APP} setup, {datetime.datetime.now():%Y-%m-%d %H:%M}, {sys.executable} {" ".join(sys.argv[1:])}, '
            f'Windows {platform.version()}\n')
    except OSError:
        pass
    sys.excepthook = lambda *e: flog('ERROR:\n' + ''.join(traceback.format_exception(*e)))
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        pass
    if '--unattended' in sys.argv:
        unattended(os.path.abspath(sys.argv[sys.argv.index('--unattended') + 1]))
    elif '--uninstall' in sys.argv:
        uninstall()
    else:
        Wizard().mainloop()


if __name__ == '__main__':
    main()
