"""Validate English translations against German sources.

Usage:
  python tools/check_translation.py                 # check everything that has a translation
  python tools/check_translation.py <rel path>...   # e.g. campaign04/_campaign_04_01/briefings.txt
Paths are relative to work/source_ger and translations/eng (same layout).

Checks (ERROR = must fix, WARN = review):
  strings.ini / *.ini : same sections, same string ids in same order, '@' prefix kept,
                        same count of \\n, no stray quotes
  briefings.txt       : same [blockstart:x]/[blockend:x] sequence, identical <...> tags per block
                        (order and content), nothing left in German, line length
  all                 : encodable in cp1252, leftover German words/umlauts
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'work', 'source_ger')
DST = os.path.join(ROOT, 'translations', 'eng')
MAXLINE = 160
GERMAN = re.compile(r'[äöüÄÖÜß]|\b(und|der|die|das|nicht|ist|mit|ein|eine|euch|wir|ihr|sich|auf|für|dem|den|zu|von|noch|auch|aber|wenn|dass|habt|sind)\b')


# Strings added on purpose at the end of a file: rel path -> count. saga001: the 'Cultures 2' campaign button (cx.dll).
EXTRA_ENTRIES = {'global/saga001.ini': 2}

def parse_ini(text):
    """-> list of (section, id, value_string)"""
    out, sect, cur = [], None, None
    for n, line in enumerate(text.split('\n'), 1):
        s = line.strip()
        if not s:
            continue
        if s.startswith('['):
            sect, cur = s, None
            out.append((sect, None, None, n))
            continue
        m = re.match(r'stringn\s+(-?\d+)\s+"(.*)"\s*$', s)
        if m:
            cur = int(m.group(1)); out.append((sect, cur, m.group(2), n)); continue
        m = re.match(r'string\s+"(.*)"\s*$', s)
        if m and cur is not None:
            cur += 1; out.append((sect, cur, m.group(1), n)); continue
        out.append((sect, 'RAW', s, n))
    return out


def check_ini(src, dst, rel, errs, warns):
    a, b = parse_ini(src), parse_ini(dst)
    ka = [(x[0], x[1], x[2] if x[1] in (None, 'RAW') else None) for x in a]
    kb = [(x[0], x[1], x[2] if x[1] in (None, 'RAW') else None) for x in b]
    extra = EXTRA_ENTRIES.get(rel, 0)
    if extra and len(kb) == len(ka) + extra and kb[:len(ka)] == ka:
        kb, b = kb[:len(ka)], b[:len(ka)]      # entries we add on purpose (not in the German original)
    if ka != kb:
        for i, (x, y) in enumerate(zip(ka, kb)):
            if x != y:
                errs.append(f'{rel}: structure differs at entry {i}: ger={x} eng={y}'); break
        else:
            errs.append(f'{rel}: entry count differs ger={len(ka)} eng={len(kb)}')
        return
    for (s, i, gv, _), (_, _, ev, n) in zip(a, b):
        if i in (None, 'RAW'):
            continue
        if gv.startswith('@') != ev.startswith('@'):
            errs.append(f'{rel}:{n}: "@" prefix mismatch for id {i}')
        if gv.count('\\n') != ev.count('\\n'):
            warns.append(f'{rel}:{n}: \\n count differs for id {i} ({gv.count(chr(92)+"n")} vs {ev.count(chr(92)+"n")})')
        if '"' in ev:
            errs.append(f'{rel}:{n}: stray double quote inside string id {i}')
        if gv.strip() and not ev.strip():
            errs.append(f'{rel}:{n}: empty translation for id {i}')
        if gv == ev and GERMAN.search(gv):
            warns.append(f'{rel}:{n}: id {i} looks untranslated: {ev[:60]}')


BLOCK = re.compile(r'\[blockstart:([^\]]*)\](.*?)\[blockend:\1\]', re.S)
TAG = re.compile(r'<[^>]*>')


def check_briefings(src, dst, rel, errs, warns):
    ba, bb = BLOCK.findall(src), BLOCK.findall(dst)
    na, nb = [x[0] for x in ba], [x[0] for x in bb]
    if na != nb:
        errs.append(f'{rel}: block sequence differs.\n   ger={na}\n   eng={nb}')
        return
    for (name, ga), (_, ea) in zip(ba, bb):
        ta, tb = TAG.findall(ga), TAG.findall(ea)
        if ta != tb:
            errs.append(f'{rel}: block [{name}] tags differ: ger={ta} eng={tb}')
        if ga.strip() and not ea.strip():
            errs.append(f'{rel}: block [{name}] is empty in English')
    outside_a = BLOCK.sub('', src).strip()
    outside_b = BLOCK.sub('', dst).strip()
    if outside_a != outside_b:
        warns.append(f'{rel}: text outside blocks changed')
    limit = max(MAXLINE, max(len(l) for l in src.split('\n')))
    for n, line in enumerate(dst.split('\n'), 1):
        if len(line) > limit and '<' not in line:
            warns.append(f'{rel}:{n}: line ({len(line)} chars) longer than any German source line ({limit}) - review fit')


def check(rel):
    errs, warns = [], []
    sp, dp = os.path.join(SRC, rel), os.path.join(DST, rel)
    if not os.path.exists(dp):
        return None, None
    src = open(sp, encoding='utf-8').read()
    dst = open(dp, encoding='utf-8').read()
    try:
        dst.encode('cp1252')
    except UnicodeEncodeError as e:
        errs.append(f'{rel}: not cp1252-encodable: {dst[e.start:e.end]!r} at {e.start} (use plain ASCII quotes/dashes)')
    if rel.endswith('.ini'):
        check_ini(src, dst, rel, errs, warns)
    elif rel.endswith('briefings.txt'):
        check_briefings(src, dst, rel, errs, warns)
    for n, line in enumerate(dst.split('\n'), 1):
        body = TAG.sub('', line)
        m = re.search(r'[äöüÄÖÜß]', body)
        if m:
            warns.append(f'{rel}:{n}: umlaut left (fine only inside a kept proper name): {body.strip()[:70]}')
    return errs, warns


def main():
    rels = sys.argv[1:]
    if not rels:
        for d, _, fs in os.walk(SRC):
            for f in fs:
                rels.append(os.path.relpath(os.path.join(d, f), SRC).replace(os.sep, '/'))
    total_e = total_w = done = missing = 0
    for rel in sorted(rels):
        e, w = check(rel)
        if e is None:
            missing += 1; continue
        done += 1
        for x in e: print('ERROR', x)
        for x in w: print('WARN ', x)
        total_e += len(e); total_w += len(w)
    print(f'\nchecked {done} files, {missing} not yet translated, {total_e} errors, {total_w} warnings')
    sys.exit(1 if total_e else 0)


if __name__ == '__main__':
    main()
