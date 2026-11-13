"""Compile loader/cx.c (which includes render.c) -> cx.dll (32-bit) with the installed MSVC (found via vswhere).

  python loader/make.py [out_dir]          (default: loader/bin)
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))


def vcvars():
    vswhere = os.path.expandvars(r'%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe')
    inst = subprocess.check_output([vswhere, '-latest', '-products', '*', '-requires',
                                    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property',
                                    'installationPath'], text=True).strip()
    return os.path.join(inst, 'VC', 'Auxiliary', 'Build', 'vcvars32.bat')


def build(out_dir=None):
    out_dir = out_dir or os.path.join(HERE, 'bin')
    os.makedirs(out_dir, exist_ok=True)
    src, dll = os.path.join(HERE, 'cx.c'), os.path.join(out_dir, 'cx.dll')
    newest = max(os.path.getmtime(os.path.join(HERE, n)) for n in os.listdir(HERE) if n.endswith(('.c', '.h')))
    if os.path.exists(dll) and os.path.getmtime(dll) >= newest:
        return dll
    bat = os.path.join(out_dir, 'make.bat')
    with open(bat, 'w') as f:
        f.write(f'@call "{vcvars()}" >nul\n')
        f.write(f'cl /nologo /O1 /MT /W3 /LD "{src}" /Fe"{dll}" /Fo"{os.path.join(out_dir, 'cx.obj')}" '
                f'/link /NOLOGO /DYNAMICBASE:NO kernel32.lib user32.lib gdi32.lib\n')
    r = subprocess.run(['cmd', '/c', bat], capture_output=True, text=True)
    if r.returncode or not os.path.exists(dll):
        sys.exit(f'cx.dll build failed:\n{r.stdout}\n{r.stderr}')
    return dll


if __name__ == '__main__':
    print(build(sys.argv[1] if len(sys.argv) > 1 else None))
