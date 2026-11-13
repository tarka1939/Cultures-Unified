"""Overview of the Ghidra export by address block: function count and a sample of referenced strings per block.
Used to draw module boundaries (MSVC lays out functions in source-file order).
  python re/blocks.py <prog> [block_size_hex] [strings_per_block]"""
import collections, os, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'export')
prog = sys.argv[1] if len(sys.argv) > 1 else 'saga'
size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x1000
per = int(sys.argv[3]) if len(sys.argv) > 3 else 4
d = os.path.join(ROOT, prog)
funcs = sorted(int(l.split('\t')[0], 16) for l in open(os.path.join(d, 'functions.tsv'), encoding='utf-8'))
strs = collections.defaultdict(list)
for line in open(os.path.join(d, 'strings.tsv'), encoding='utf-8'):
    p = line.rstrip('\n').split('\t')
    if len(p) < 3:
        continue
    text = p[2].strip('"')
    for r in p[1].split(','):
        r = r.lstrip('@')
        try:
            strs[int(r, 16) // size].append(text)
        except ValueError:
            pass
count = collections.Counter(f // size for f in funcs)
for b in sorted(set(count) | set(strs)):
    seen, out = set(), []
    for t in strs.get(b, []):
        t = t.replace('D:\\\\Dev\\\\Projects\\\\FunaticsOld\\\\ProjectD\\\\', '')
        if t not in seen and len(t) > 3:
            seen.add(t); out.append(t[:60])
    print(f'{b * size:08x} f={count.get(b, 0):3d} | ' + ' ; '.join(out[:per]))
