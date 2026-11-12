"""Round-trip tests against the real game data (read-only on the sources).

  python tests/test_roundtrip.py [--full]
Default: every .cif in every archive decodes and re-encodes byte-identically, and every .c2m rewrites
byte-identically. --full also rewrites the big data0001.lib files to a temp dir and compares hashes.
"""
import glob, hashlib, os, sys, tempfile
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
import libtool

GOG = 'D:/GOG Games'
LIBS = {
    'c2': f'{GOG}/Cultures and Cultures 2/Cultures 2/DataX/Libs/data0001.lib',
    'nl': f'{GOG}/Northland and 8th Wonder of the World/Northland/DataX/Libs/data0001.lib',
    'w8': f'{GOG}/Northland and 8th Wonder of the World/8th Wonder of the World/DataX/Libs/data0001.lib',
    'sg': 'D:/Cultures_Saga/DataX/Libs/data0001.lib',
}
C2M = glob.glob('D:/Cultures_Saga/DataX/UserCampaigns/*/*.c2m')


def sha(p):
    h = hashlib.sha1()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 22), b''):
            h.update(chunk)
    return h.hexdigest()


def cif_check(a, label):
    ok = bad = 0
    for k in a.names():
        if not k.lower().endswith('.cif'):
            continue
        b = a.read(k)
        t = libtool.cif_decode(b)
        if t is None:
            continue
        if libtool.cif_encode(t) == b:
            ok += 1
        else:
            bad += 1
            if bad <= 3:
                print(f'  [{label}] cif mismatch: {k}')
    return ok, bad


def main():
    full = '--full' in sys.argv
    fails = 0
    for label, p in LIBS.items():
        a = libtool.Archive.open(p)
        ok, bad = cif_check(a, label)
        print(f'{label}: {len(a.files)} files, cif round-trip ok={ok} bad={bad}')
        fails += bad
        if full:
            with tempfile.TemporaryDirectory() as td:
                out = os.path.join(td, 'x.lib'); a.save(out)
                same = sha(out) == sha(p)
                print(f'   archive rewrite identical: {same}'); fails += not same
    with tempfile.TemporaryDirectory() as td:
        same_all = True; cok = cbad = 0
        for p in C2M:
            a = libtool.Archive.open(p)
            o, b = cif_check(a, os.path.basename(p)); cok += o; cbad += b
            out = os.path.join(td, 'x.c2m'); a.save(out)
            if open(out, 'rb').read() != open(p, 'rb').read():
                same_all = False; print('  c2m rewrite differs:', os.path.basename(p))
        print(f'c2m: {len(C2M)} archives rewrite identical: {same_all}; cif ok={cok} bad={cbad}')
        fails += (not same_all) + cbad
    print('PASS' if not fails else f'FAIL ({fails})')
    sys.exit(1 if fails else 0)


if __name__ == '__main__':
    main()
