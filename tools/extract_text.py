"""Extract translatable German text (Cultures Saga) and an official German/English parallel corpus.

Usage:
  python tools/extract_text.py --saga D:/Cultures_Saga --gog "D:/GOG Games" --out work

Produces (all UTF-8):
  work/source_ger/global/saga001.ini
  work/source_ger/campaign04/<map>/strings.ini | briefings.txt | <nnnn>.hlt (only .hlt with inline prose)
  work/source_ger/usercampaigns/Campaign00|01/<c2m stem>/...
  work/parallel/global/<table>.{ger,eng}.ini            (Saga German vs official GOG English)
  work/parallel/maps/<map>/<file>.{ger,eng}.<ext>        (campaign 2/3 + scenario maps)
  work/manifest.json   work path -> {container, internal path, kind}
"""
import argparse, json, os, re, sys, glob
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import libtool

BS = chr(92)
ENC = 'cp1252'


def decode(path, data):
    if path.lower().endswith('.cif'):
        return libtool.cif_decode(data)
    return data.decode(ENC)


def has_prose(hlt_text):
    for line in hlt_text.splitlines():
        s = re.sub(r'<[^>]*>', '', line).strip()
        if s:
            return True
    return False


def out_name(internal):
    base = internal.split(BS)[-1]
    return base[:-4] + '.ini' if base.lower().endswith('.cif') else base


def want(internal):
    kl = internal.lower()
    return kl.endswith(('strings.cif', 'briefings.txt', '.hlt'))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--saga', required=True)
    ap.add_argument('--gog', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = a.out
    manifest = {}

    def write(rel, text, container, internal, kind):
        p = os.path.join(out, *rel.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'w', encoding='utf-8', newline='\n') as f:
            f.write(text.replace('\r\n', '\n'))
        manifest[rel] = {'container': container, 'internal': internal, 'kind': kind}

    saga_lib = os.path.join(a.saga, 'DataX', 'Libs', 'data0001.lib')
    sg = libtool.index(saga_lib)

    # --- translatable: global + campaign 4
    for k, v in sg.items():
        kl = k.lower()
        if kl == BS.join(['data', 'text', 'ger', 'strings', 'saga', 'saga001.cif']):
            write('source_ger/global/saga001.ini', decode(k, libtool.read(saga_lib, v)), 'saga_lib', k, 'cif')
        elif kl.startswith(BS.join(['data', 'maps', '_campaign_04'])) and BS + 'ger' + BS in kl and want(k):
            text = decode(k, libtool.read(saga_lib, v))
            if kl.endswith('.hlt') and not has_prose(text):
                continue
            mp = k.split(BS)[2]
            write(f'source_ger/campaign04/{mp}/{out_name(k)}', text, 'saga_lib', k,
                  'cif' if kl.endswith('.cif') else 'text')

    # --- translatable: user campaigns (.c2m)
    for c2m in sorted(glob.glob(os.path.join(a.saga, 'DataX', 'UserCampaigns', '*', '*.c2m'))):
        camp = os.path.basename(os.path.dirname(c2m))
        stem = os.path.splitext(os.path.basename(c2m))[0]
        files = libtool.index(c2m)
        for k, v in files.items():
            kl = k.lower()
            if BS + 'ger' + BS in kl and want(k):
                text = decode(k, libtool.read(c2m, v))
                if kl.endswith('.hlt') and not has_prose(text):
                    continue
                write(f'source_ger/usercampaigns/{camp}/{stem}/{out_name(k)}', text,
                      os.path.relpath(c2m, a.saga), k, 'cif' if kl.endswith('.cif') else 'text')

    # --- parallel corpus (official English)
    eng_libs = [os.path.join(a.gog, 'Northland and 8th Wonder of the World', g, 'DataX', 'Libs', 'data0001.lib')
                for g in ('8th Wonder of the World', 'Northland')]
    eng = {}
    for lib in eng_libs:
        for k, v in libtool.index(lib).items():
            eng.setdefault(k.lower(), (lib, v))
    for k, v in sg.items():
        kl = k.lower()
        if BS + 'ger' + BS not in kl or kl.startswith(BS.join(['data', 'maps', '_campaign_04'])):
            continue
        ek = kl.replace(BS + 'ger' + BS, BS + 'eng' + BS)
        if ek not in eng:
            continue
        is_global = kl.startswith(BS.join(['data', 'text', 'ger', 'strings'])) and kl.endswith('.cif')
        is_map = kl.startswith(BS.join(['data', 'maps'])) and want(k) and not kl.endswith('.hlt')
        if not (is_global or is_map):
            continue
        g_txt = decode(k, libtool.read(saga_lib, v))
        elib, ev = eng[ek]
        e_txt = decode(k, libtool.read(elib, ev))
        name = out_name(k)
        stem, ext = os.path.splitext(name)
        rel = f'parallel/global/{stem}' if is_global else f'parallel/maps/{k.split(BS)[2]}/{stem}'
        write(f'{rel}.ger{ext}', g_txt, 'saga_lib', k, 'parallel')
        write(f'{rel}.eng{ext}', e_txt, os.path.relpath(elib, a.gog), ek, 'parallel')

    with open(os.path.join(out, 'manifest.json'), 'w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=1, ensure_ascii=False)
    n_src = sum(1 for r in manifest if r.startswith('source_ger/'))
    n_par = sum(1 for r in manifest if r.startswith('parallel/'))
    print(f'source files: {n_src}  parallel files: {n_par}')


if __name__ == '__main__':
    main()
