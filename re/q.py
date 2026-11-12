"""Query the Ghidra text exports in re/export/<prog>/.

  python re/q.py <prog> s <regex>          strings matching regex, with referencing functions
  python re/q.py <prog> f <addr|name>      decompiled function (entry address like 00401000 or name)
  python re/q.py <prog> x <addr|name>      callers and callees of a function
  python re/q.py <prog> sf <regex>         decompile every function referencing a matching string
  python re/q.py <prog> g <regex>          grep decompiled code, print function headers + matching lines
  python re/q.py <prog> i <regex>          imports matching regex, with referencing functions
prog: saga | w8 | c2
"""
import os, re, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'export')


def load(prog):
    d = os.path.join(ROOT, prog)
    funcs = {}
    for line in open(os.path.join(d, 'functions.tsv'), encoding='utf-8'):
        p = line.rstrip('\n').split('\t')
        funcs[p[0]] = dict(name=p[1], size=int(p[2]), ncallers=int(p[3]), callees=[c for c in p[4].split(',') if c])
    return d, funcs


def decomp_index(d):
    out, cur, buf = {}, None, []
    for line in open(os.path.join(d, 'decomp.c'), encoding='utf-8'):
        if line.startswith('//@@ '):
            if cur: out[cur] = ''.join(buf)
            cur = line.split()[1]; buf = []
        else:
            buf.append(line)
    if cur: out[cur] = ''.join(buf)
    return out


def resolve(funcs, key):
    k = key.lower().lstrip('0x').zfill(8) if re.fullmatch(r'(0x)?[0-9a-fA-F]+', key) else None
    if k and k in funcs: return k
    for a, f in funcs.items():
        if f['name'] == key: return a
    sys.exit(f'no function {key}')


def main(prog, cmd, arg):
    d, funcs = load(prog)
    if cmd in ('s', 'sf', 'i'):
        rx = re.compile(arg, re.I)
        fn = 'imports.tsv' if cmd == 'i' else 'strings.tsv'
        hits = set()
        for line in open(os.path.join(d, fn), encoding='utf-8'):
            if rx.search(line.split('\t')[-1] if cmd != 'i' else line.split('\t')[0]):
                print(line.rstrip())
                for r in line.split('\t')[1].split(','):
                    if r and not r.startswith('@'): hits.add(r)
        if cmd == 'sf':
            dc = decomp_index(d)
            for a in sorted(hits):
                print(f'\n//@@ {a} {funcs.get(a, {}).get("name")}\n{dc.get(a, "?")}')
    elif cmd == 'f':
        a = resolve(funcs, arg); print(f'//@@ {a} {funcs[a]["name"]}'); print(decomp_index(d).get(a, '?'))
    elif cmd == 'x':
        a = resolve(funcs, arg)
        print('callers:', ' '.join(f'{c}:{funcs[c]["name"]}' for c, f in funcs.items() if a in f['callees']))
        print('callees:', ' '.join(f'{c}:{funcs.get(c, {}).get("name")}' for c in funcs[a]['callees']))
    elif cmd == 'g':
        rx = re.compile(arg)
        for a, body in decomp_index(d).items():
            m = [l for l in body.split('\n') if rx.search(l)]
            if m:
                print(f'//@@ {a} {funcs.get(a, {}).get("name")}')
                for l in m[:8]: print('   ', l.strip())


if __name__ == '__main__':
    if len(sys.argv) < 4: sys.exit(__doc__)
    main(*sys.argv[1:4])
