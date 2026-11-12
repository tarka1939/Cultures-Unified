"""Cultures 2-engine .bmd sprite banks (read, write, render).

Layout (all u32 LE):
  header : 0x3F4, 0, 0, nframes, pixel_bytes, nrows, nrows, unk, 0
  section: 0x3E9, 0, size  frames  : nframes * {type, dx, dy, w, h, first_row}
  section: 0x3E9, 0, size  pixels  : RLE rows
  section: 0x3E9, 0, size  rows    : nrows * u32: (x_indent << 22) | offset into pixels; 0xFFFFFFFF = empty row
Frame type 4 rows: byte n; n==0 end of row; n<0x80 -> n pixels follow (2 bytes each: palette index, alpha/shade);
n>0x80 -> skip n-0x80 transparent pixels. (Other types: 1-byte pixels; see decode_row.)

CLI:
  bmd.py info <file.bmd>
  bmd.py png <file.bmd> <palette.pcx|-> <outdir>      render every frame (grey ramp if palette '-')
"""
import os, struct, sys

MAGIC, SECT = 0x3F4, 0x3E9


class Bmd:
    def __init__(self, frames=None, unk=0):
        self.frames = frames or []   # list of dict(type, dx, dy, w, h, rows=[bytes,...])
        self.unk = unk

    @classmethod
    def parse(cls, b):
        hdr = struct.unpack_from('<9I', b, 0)
        if hdr[0] != MAGIC:
            raise ValueError('not a bmd')
        p, secs = 36, []
        while p < len(b):
            t, _, sz = struct.unpack_from('<3I', b, p)
            secs.append(b[p + 12:p + 12 + sz]); p += 12 + sz
        if len(secs) < 3:          # empty bank (e.g. *_s.bmd stubs)
            return cls([], hdr[7])
        ftab, pix, rtab = secs[:3]
        rows = struct.unpack(f'<{len(rtab) // 4}I', rtab)
        frames = []
        for i in range(len(ftab) // 24):
            t, dx, dy, w, h, r0 = struct.unpack_from('<6i', ftab, i * 24)
            fr = []
            for r in range(r0, r0 + h):
                v = rows[r]
                if v == 0xFFFFFFFF:
                    fr.append(None)                       # empty row
                else:
                    fr.append((v >> 22, _row_bytes(pix, v & 0x3FFFFF, t)))  # (x indent, encoded bytes)
            frames.append(dict(type=t, dx=dx, dy=dy, w=w, h=h, rows=fr))
        return cls(frames, hdr[7])

    def build(self):
        ftab, pix, rtab = bytearray(), bytearray(), bytearray()
        nrows = 0
        for f in self.frames:
            ftab += struct.pack('<6i', f['type'], f['dx'], f['dy'], f['w'], f['h'], nrows)
            for r in f['rows']:
                if r is None:
                    rtab += struct.pack('<I', 0xFFFFFFFF)
                else:
                    rtab += struct.pack('<I', (r[0] << 22) | len(pix)); pix += r[1]
                nrows += 1
        out = bytearray(struct.pack('<9I', MAGIC, 0, 0, len(self.frames), len(pix), nrows, nrows, self.unk, 0))
        for s in (ftab, pix, rtab):
            out += struct.pack('<3I', SECT, 0, len(s)) + s
        return bytes(out)


def _row_bytes(pix, off, t):
    """Return the raw encoded bytes of one row starting at off (up to and including the 0 terminator)."""
    bpp = 2 if t == 4 else 1
    p = off
    while True:
        n = pix[p]; p += 1
        if n == 0:
            return bytes(pix[off:p])
        if n < 0x80:
            p += n * bpp


def decode_row(row, t, w):
    """-> list of (index, alpha) or None for transparent."""
    out, p, bpp = [], 0, (2 if t == 4 else 1)
    while p < len(row):
        n = row[p]; p += 1
        if n == 0:
            break
        if n < 0x80:
            for _ in range(n):
                out.append((row[p], row[p + 1] if bpp == 2 else 255)); p += bpp
        else:
            out.extend([None] * (n - 0x80))
    return (out + [None] * w)[:w]


def read_pcx_palette(b):
    if len(b) >= 769 and b[-769] == 12:
        pal = b[-768:]
        return [tuple(pal[i * 3:i * 3 + 3]) for i in range(256)]
    return None


def render(f, pal):
    """-> RGBA bytes (w*h*4)."""
    w, h = f['w'], f['h']
    out = bytearray(w * h * 4)
    for y, row in enumerate(f['rows']):
        if row is None:
            continue
        for x, px in enumerate(decode_row(row[1], f['type'], w - row[0]), row[0]):
            if px is None:
                continue
            i, a = px
            r, g, bl = pal[i] if pal else (i, i, i)
            o = (y * w + x) * 4
            out[o:o + 4] = bytes((r, g, bl, a if f['type'] == 4 else 255))
    return bytes(out)


def write_png(path, w, h, rgba):
    import zlib
    raw = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    with open(path, 'wb') as fo:
        fo.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                 + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    cmd = sys.argv[1]
    bm = Bmd.parse(open(sys.argv[2], 'rb').read())
    if cmd == 'info':
        print('frames', len(bm.frames), 'unk', bm.unk)
        for i, f in enumerate(bm.frames):
            print(i, f['type'], f['dx'], f['dy'], f['w'], f['h'])
    elif cmd == 'png':
        pal = None if sys.argv[3] == '-' else read_pcx_palette(open(sys.argv[3], 'rb').read())
        os.makedirs(sys.argv[4], exist_ok=True)
        for i, f in enumerate(bm.frames):
            if f['w'] and f['h']:
                write_png(os.path.join(sys.argv[4], f'{i:02d}.png'), f['w'], f['h'], render(f, pal))
