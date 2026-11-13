"""Build the one-file setup exe: python installer/make_setup.py  ->  work/installer/CulturesUnifiedSetup.exe

Needs what build.py needs (MSVC for cx.dll, cnc-ddraw in .tools/cnc-ddraw, the game folders for work/manifest.json
on a fresh checkout) plus PyInstaller. The exe carries build.py, tools/, a prebuilt cx.dll, translations/eng,
the text manifest (file names only) and cnc-ddraw; no game files.
"""
import json, os, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'loader'))
import make as cx_make  # noqa: E402

OUT = os.path.join(ROOT, 'work', 'installer')
NAME = 'CulturesUnifiedSetup'


def main():
    stage = os.path.join(OUT, 'stage')
    shutil.rmtree(stage, ignore_errors=True)
    os.makedirs(stage)
    shutil.copy2(cx_make.build(), os.path.join(stage, 'cx.dll'))
    mpath = os.path.join(ROOT, 'work', 'manifest.json')
    if not os.path.exists(mpath):
        raise SystemExit('work/manifest.json is missing: run python build.py once first')
    # only the container/path index is needed, never the extracted text
    m = json.load(open(mpath, encoding='utf-8'))
    json.dump(m, open(os.path.join(stage, 'manifest.json'), 'w', encoding='utf-8'), ensure_ascii=False)
    ddraw = os.path.join(ROOT, '.tools', 'cnc-ddraw')
    dd = os.path.join(stage, 'cnc-ddraw')
    os.makedirs(dd)
    for f in ('ddraw.dll', 'ddraw.ini'):
        shutil.copy2(os.path.join(ddraw, f), dd)
    shutil.copytree(os.path.join(ddraw, 'Shaders'), os.path.join(dd, 'Shaders'))

    sep = os.pathsep
    data = [(os.path.join(stage, 'cx.dll'), '.'), (os.path.join(stage, 'manifest.json'), '.'),
            (dd, 'cnc-ddraw'), (os.path.join(ROOT, 'translations', 'eng'), os.path.join('translations', 'eng')),
            (os.path.join(ROOT, 'installer', 'LICENSE-cnc-ddraw.txt'), 'installer'),
            (os.path.join(ROOT, 'README.md'), '.'), (os.path.join(ROOT, 'CHECKLIST.md'), '.')]
    cmd = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--onefile', '--windowed', '--name', NAME,
           '--distpath', OUT, '--workpath', os.path.join(OUT, 'pyi'), '--specpath', os.path.join(OUT, 'pyi')]
    for p in (ROOT, os.path.join(ROOT, 'tools'), os.path.join(ROOT, 'loader'), os.path.join(ROOT, 're')):
        cmd += ['--paths', p]
    for src, dst in data:
        cmd += ['--add-data', f'{src}{sep}{dst}']
    cmd.append(os.path.join(ROOT, 'installer', 'setup.py'))
    subprocess.run(cmd, check=True)
    exe = os.path.join(OUT, NAME + '.exe')
    print(f'{exe}  ({os.path.getsize(exe) / 1e6:.1f} MB)')


if __name__ == '__main__':
    main()
