"""Check that every name referenced by Cultures 2 maps exists in the Saga data.

Usage:  python -I tools/validate_maps.py

Names are compared case-insensitively.  Reads (read-only) the C2 and Saga data0001.lib archives, scans every C2
data\\maps\\*\\map.cif for quoted-name references and compares them with the
Saga definition tables (data\\logic\\*.cif) and, for script verbs, with the
quoted strings in re/export/{saga,c2}/strings.tsv.  Always exits 0.
"""
import collections, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import libtool

BS = chr(92)
C2_LIB = r'D:\GOG Games\Cultures and Cultures 2\Cultures 2\DataX\Libs\data0001.lib'
SAGA_LIB = r'D:\Cultures_Saga\DataX\Libs\data0001.lib'
EXPORT = os.path.join(os.path.dirname(HERE), 're', 'export')
LOGIC = 'data' + BS + 'logic' + BS
INIS = 'data' + BS + 'engine2d' + BS + 'inis' + BS
MAPS = 'data' + BS + 'maps' + BS

# category -> (definition file, defining key); map house names are the EditName of
# [GfxHouse] in houses.cif (logic housetypes.cif only has numeric/debug names)
DEFS = {
    'house':   (INIS + 'houses' + BS + 'houses.cif', 'EditName'),
    'good':    (LOGIC + 'goodtypes.cif', 'name'),
    'job':     (LOGIC + 'jobtypes.cif', 'name'),
    'vehicle': (LOGIC + 'vehicletypes.cif', 'name'),
    'tribe':   (LOGIC + 'tribetypes' + BS + 'tribetypes.cif', 'name'),   # humans and animals
    'palette': (INIS + 'humans' + BS + 'randompalette.cif', 'Name'),
}
# keyword -> [(arg index among tokens after the keyword, category)]
REFS = {
    'sethouse': [(1, 'house')], 'addgoods': [(0, 'good')], 'setproducedgood': [(0, 'good')],
    'sethuman': [(1, 'tribe'), (2, 'job')], 'setanimal': [(1, 'tribe'), (2, 'job')],
    'setvehicle': [(1, 'tribe'), (2, 'vehicle')],
    'setpalette': [(1, 'palette')],
    'goal': [(0, 'verb')], 'result': [(0, 'verb')], 'condition': [(0, 'verb')],
}
SKIP = {'debuginfo'}   # free text


def tokens(line):
    return re.findall(r'"([^"]*)"|(\S+)', line)


def definitions(arc):
    out = {}
    for cat, (fn, key) in DEFS.items():
        names = set()
        name = fn
        if name in arc.files:
            for l in libtool.cif_decode(arc.read(name)).splitlines():
                m = re.match(r'\s*' + key + r'\s+"([^"]*)"', l, re.I)
                if m:
                    names.add(m.group(1).lower())
        out[cat] = names
    return out


def tsv_strings(game):
    p = os.path.join(EXPORT, game, 'strings.tsv')
    s = set()
    with open(p, encoding='utf-8', errors='replace') as f:
        for l in f:
            m = re.search(r'"(.*)"\s*$', l.rstrip('\n'))
            if m:
                s.add(m.group(1))
    return s


def main():
    c2, saga = libtool.Archive.open(C2_LIB), libtool.Archive.open(SAGA_LIB)
    defs_c2, defs_sg = definitions(c2), definitions(saga)
    sg_str, c2_str = tsv_strings('saga'), tsv_strings('c2')
    sg_str_l = {x.lower() for x in sg_str}
    dirs = sorted({n.split(BS)[2] for n in c2.names() if n.startswith(MAPS) and n.count(BS) > 2})
    with_cif = {n.split(BS)[2] for n in c2.names() if n.startswith(MAPS) and n.endswith(BS + 'map.cif')}
    refs = collections.defaultdict(lambda: collections.defaultdict(collections.Counter))  # cat->name->map->n
    unknown_kw = collections.Counter()
    for n in c2.names():
        if not (n.startswith(MAPS) and n.endswith(BS + 'map.cif')):
            continue
        mp = n.split(BS)[2]
        for l in libtool.cif_decode(c2.read(n)).splitlines():
            l = l.strip()
            if '"' not in l or l.startswith('['):
                continue
            t = tokens(l)
            kw = t[0][0] or t[0][1]
            if kw in SKIP:
                continue
            args = [q or w for q, w in t[1:]]
            if kw not in REFS:
                unknown_kw[kw] += 1
                continue
            for idx, cat in REFS[kw]:
                if idx < len(args):
                    refs[cat][args[idx]][mp] += 1

    print('C2 map folders: %d, with map.cif: %d' % (len(dirs), len(with_cif)))
    print('Folders without map.cif: ' + ', '.join(d for d in dirs if d not in with_cif))
    c9 = [n for n in c2.names() if n.startswith(MAPS + 'campaign_01_09' + BS)]
    print('campaign_01_09 files: %d; map.cif present: %s; map.dat present: %s' % (
        len(c9), MAPS + 'campaign_01_09' + BS + 'map.cif' in c2.files,
        MAPS + 'campaign_01_09' + BS + 'map.dat' in c2.files))
    if unknown_kw:
        print('Quoted-arg keywords not checked:', dict(unknown_kw))

    print('\nDefinition tables (names in C2 / Saga / in C2 only / in Saga only)')
    for cat in DEFS:
        a, b = defs_c2[cat], defs_sg[cat]
        print('  %-8s %3d / %3d / only-C2: %s / only-Saga: %s' % (
            cat, len(a), len(b), sorted(a - b) or '-', sorted(b - a) or '-'))

    def fmt(m):
        tot = sum(m.values())
        return '%dx in %s' % (tot, ', '.join('%s(%d)' % kv for kv in sorted(m.items())))

    print('\nMISSING in Saga (referenced by C2 maps)')
    anything = False
    for cat in DEFS:
        for name in sorted(refs[cat]):
            if name.lower() not in defs_sg[cat]:
                anything = True
                print('  %-8s %r: %s' % (cat, name, fmt(refs[cat][name])))
    if not anything:
        print('  (none)')

    print('\nScript verbs (goal/result/condition first arg)')
    print('  %-34s %5s %-6s %-6s' % ('verb', 'uses', 'saga', 'c2'))
    for name in sorted(refs['verb']):
        used = sum(refs['verb'][name].values())
        s = 'yes' if name in sg_str else ('case' if name.lower() in sg_str_l else 'NO')
        c = 'yes' if name in c2_str else 'NO'
        flag = '' if s != 'NO' else '   <-- missing in Saga: ' + fmt(refs['verb'][name])
        print('  %-34s %5d %-6s %-6s%s' % (name, used, s, c, flag))


if __name__ == '__main__':
    main()
