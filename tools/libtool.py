"""Cultures 2-engine container and text formats (Cultures 2, Northland, 8th Wonder, Saga).

  .lib / .c2m archive  : read, write (byte-exact round trip when order is preserved)
  .cif text table      : decode -> INI-like text, encode text -> .cif (0xFD03 variant; 0x41 = Cultures 1 read-only)

CLI:
  libtool.py <archive> ls [substr...]
  libtool.py <archive> cat [substr...]                 decoded .cif / raw text to stdout
  libtool.py <archive> extract <outdir> [substr...]
  libtool.py pack <srcdir> <archive>                   directory tree -> archive (paths relative to srcdir)
Format notes: header u32 1, u32 first_file_index (= ndirs+1), u32 nfiles, u32 1, u32 0x5C ('\\'), u8 0;
  dirs: u32 len, name (cp1250, ends with '\\'), u32 depth;  files: u32 len, name, u32 offset, u32 size; then data.
"""
import os, struct, sys

ENC = 'cp1250'
BS = '\\'


# ---------------------------------------------------------------- archives
class Archive:
    def __init__(self, dirs=None, files=None, path=None):
        self.dirs = dirs or []          # [(name, depth)]
        self.files = files or {}        # name -> (offset, size) when loaded, or bytes when built
        self.path = path

    @classmethod
    def open(cls, path):
        with open(path, 'rb') as f:
            hdr = f.read(21)
            one, first, nfiles, one2, sep = struct.unpack('<5I', hdr[:20])
            if (one, one2, sep, hdr[20]) != (1, 1, 0x5C, 0):
                raise ValueError(f'{path}: not a Cultures 2-engine archive')
            dirs = []
            for _ in range(first - 1):
                n = struct.unpack('<I', f.read(4))[0]
                name = f.read(n).decode(ENC)
                dirs.append((name, struct.unpack('<I', f.read(4))[0]))
            files = {}
            for _ in range(nfiles):
                n = struct.unpack('<I', f.read(4))[0]
                name = f.read(n).decode(ENC)
                files[name] = struct.unpack('<II', f.read(8))
        return cls(dirs, files, path)

    def read(self, name):
        v = self.files[name]
        if isinstance(v, (bytes, bytearray)):
            return bytes(v)
        with open(self.path, 'rb') as f:
            f.seek(v[0])
            return f.read(v[1])

    def names(self):
        return list(self.files)

    def find(self, *subs):
        return [k for k in self.files if all(s.lower() in k.lower() for s in subs)]

    def set(self, name, data):
        """Add/replace a file; registers missing parent directories (depth = number of path components,
        e.g. 'data\\' = 1, as in the original archives)."""
        self.files[name] = bytes(data)
        known = {d for d, _ in self.dirs}
        parts = name.split(BS)[:-1]
        for i in range(1, len(parts) + 1):
            d = BS.join(parts[:i]) + BS
            if d not in known:
                self.dirs.append((d, i)); known.add(d)

    def save(self, path):
        names = list(self.files)
        enc = [n.encode(ENC) for n in names]
        hdr = bytearray(struct.pack('<5IB', 1, len(self.dirs) + 1, len(names), 1, 0x5C, 0))
        for d, depth in self.dirs:
            b = d.encode(ENC)
            hdr += struct.pack('<I', len(b)) + b + struct.pack('<I', depth)
        table_len = sum(4 + len(b) + 8 for b in enc)
        data_off = len(hdr) + table_len
        sizes = [len(self.read(n)) for n in names] if any(not isinstance(v, (bytes, bytearray)) for v in self.files.values()) else [len(self.files[n]) for n in names]
        # keep original data order if loaded from disk (byte-exact rewrite), else table order
        order = sorted(range(len(names)), key=lambda i: self.files[names[i]][0]) \
            if all(isinstance(v, tuple) for v in self.files.values()) else list(range(len(names)))
        offsets = [0] * len(names)
        o = data_off
        for i in order:
            offsets[i] = o; o += sizes[i]
        tmp = path + '.tmp'
        with open(tmp, 'wb') as f:
            f.write(hdr)
            for b, off, sz in zip(enc, offsets, sizes):
                f.write(struct.pack('<I', len(b)) + b + struct.pack('<II', off, sz))
            for i in order:
                f.write(self.read(names[i]))
        os.replace(tmp, path)


# ---------------------------------------------------------------- .cif
def _xor(buf, decode):
    out = bytearray(len(buf)); c, d = 71, 126
    for i, b in enumerate(buf):
        out[i] = (((b - 1) & 0xFF) ^ c) if decode else (((b ^ c) + 1) & 0xFF)
        c = (c + d) & 0xFF; d = (d + 33) & 0xFF
    return bytes(out)


def _entries_to_text(idx, txt):
    lines = []
    for i in range(len(idx) // 4):
        o = struct.unpack_from('<I', idx, 4 * i)[0]
        s = txt[o:txt.index(b'\0', o)]
        if s[:1] == b'\x01':
            s = b'[' + s[1:] + b']'
        elif s[:1] == b'\x02':
            s = s[1:]
        else:
            raise ValueError(f'cif entry {i}: unexpected type byte {s[:1]!r}')
        lines.append(s.replace(b'\n', b'\\n').decode(ENC))
    return '\n'.join(lines)


def cif_decode(b):
    """-> text, or None if not a cif."""
    if b[:2] == b'\xfd\x03':
        tsz = struct.unpack_from('<I', b, 24)[0]
        isz = struct.unpack_from('<I', b, 36)[0]
        idx = _xor(b[40:40 + isz], True)
        p = 40 + isz + 13
        return _entries_to_text(idx, _xor(b[p:p + tsz], True))
    if b[:2] == b'\x41\x00':
        isz = struct.unpack_from('<I', b, 16)[0]
        idx = _xor(b[20:20 + isz], True)
        p = 20 + isz + 6
        tsz = struct.unpack_from('<I', b, p)[0]
        return _entries_to_text(idx, _xor(b[p + 4:p + 4 + tsz], True))
    return None


def cif_encode(text):
    """INI-like text -> .cif bytes (Cultures 2-engine variant)."""
    ents = []
    for line in text.split('\n'):
        # '\n' escapes mean a real newline only in string-table lines; elsewhere (e.g. paths like
        # ...\creatures\norne01.pcx) a backslash-n is literal.
        if line.startswith(('string ', 'stringn ')):
            line = line.replace('\\n', '\n')
        raw = line.encode(ENC)
        ents.append(b'\x01' + raw[1:-1] if raw[:1] == b'[' and raw[-1:] == b']' else b'\x02' + raw)
    idx, txt = bytearray(), bytearray()
    for e in ents:
        idx += struct.pack('<I', len(txt)); txt += e + b'\0'
    n = len(ents)
    return (b'\xfd\x03' + b'\0' * 6 + struct.pack('<I', 1) + struct.pack('<III', n, n, n)
            + struct.pack('<III', len(txt), 1001, 0) + struct.pack('<I', len(idx)) + _xor(idx, False)
            + b'\x01' + struct.pack('<III', 1001, 0, len(txt)) + _xor(txt, False))


# ---------------------------------------------------------------- CLI
def _main(argv):
    if argv[0] == 'pack':
        src, dst = argv[1], argv[2]
        a = Archive()
        for d, _, fs in sorted(os.walk(src)):
            for fn in sorted(fs):
                full = os.path.join(d, fn)
                a.set(os.path.relpath(full, src).replace(os.sep, BS), open(full, 'rb').read())
        a.save(dst); return
    a = Archive.open(argv[0]); cmd = argv[1]
    if cmd == 'ls':
        for k in a.find(*argv[2:]):
            print(k, a.files[k][1])
    elif cmd == 'cat':
        out = sys.stdout
        for k in a.find(*argv[2:]):
            b = a.read(k)
            t = cif_decode(b) if k.lower().endswith('.cif') else None
            out.write(f'===== {k}\n{t if t is not None else b.decode(ENC, "replace")}\n')
    elif cmd == 'extract':
        for k in a.find(*argv[3:]):
            dst = os.path.join(argv[2], *k.split(BS))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            open(dst, 'wb').write(a.read(k))


# ---- backwards-compatible helpers used by earlier scripts
def index(path):
    return Archive.open(path).files


def read(path, ent):
    with open(path, 'rb') as f:
        f.seek(ent[0]); return f.read(ent[1])


if __name__ == '__main__':
    _main(sys.argv[1:])
