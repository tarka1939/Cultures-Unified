"""Add a DLL import to a PE32 executable (so the loader maps our hook DLL before the game starts).

  python tools/peimport.py <in.exe> <out.exe> <dll> <export>

A new section '.cx' holds a copy of the import descriptors plus one for <dll>!<export>; the import data directory
is re-pointed there and the bound-import directory cleared. Idempotent: an exe that already imports <dll> is kept.
"""
import struct, sys


def _align(v, a):
    return (v + a - 1) // a * a


def add_import(data, dll, export):
    b = bytearray(data)
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    if b[pe:pe + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    coff = pe + 4
    nsec, = struct.unpack_from('<H', b, coff + 2)
    opt_size, = struct.unpack_from('<H', b, coff + 16)
    opt = coff + 20
    if struct.unpack_from('<H', b, opt)[0] != 0x10B:
        raise ValueError('not PE32')
    sec_align, file_align = struct.unpack_from('<II', b, opt + 32)
    size_hdrs, = struct.unpack_from('<I', b, opt + 60)
    dd = opt + 96
    imp_rva, imp_size = struct.unpack_from('<II', b, dd + 8)
    secs = opt + opt_size
    sections = [struct.unpack_from('<8sIIII', b, secs + 40 * i) for i in range(nsec)]

    def rva2off(rva):
        for name, vsize, va, rsize, raw in sections:
            if va <= rva < va + max(vsize, rsize):
                return rva - va + raw
        raise ValueError(f'rva {rva:#x} not in a section')

    # existing descriptors
    descs, o = [], rva2off(imp_rva)
    while True:
        d = bytes(b[o:o + 20])
        if d == bytes(20):
            break
        name = b[rva2off(struct.unpack_from('<I', d, 12)[0]):].split(b'\0', 1)[0]
        if name.lower() == dll.lower().encode():
            return bytes(b)                      # already there
        descs.append(d); o += 20

    hdr_end = secs + 40 * (nsec + 1)
    first_raw = min(raw for *_, raw in sections if raw)
    if hdr_end > min(size_hdrs, first_raw):
        raise ValueError('no room for another section header')

    last = max(sections, key=lambda s: s[2])
    new_va = _align(last[2] + max(last[1], last[3]), sec_align)
    new_raw = _align(len(b), file_align)

    # layout inside the new section
    n = len(descs) + 2
    desc_off = 0
    ilt_off = desc_off + 20 * n
    iat_off = ilt_off + 8
    hint_off = iat_off + 8
    hint = struct.pack('<H', 0) + export.encode() + b'\0'
    hint += b'\0' * (len(hint) & 1)
    name_off = hint_off + len(hint)
    body = bytearray(name_off + len(dll) + 1)
    for i, d in enumerate(descs):
        body[20 * i:20 * i + 20] = d
    body[20 * len(descs):20 * len(descs) + 20] = struct.pack(
        '<IIIII', new_va + ilt_off, 0, 0, new_va + name_off, new_va + iat_off)
    struct.pack_into('<I', body, ilt_off, new_va + hint_off)
    struct.pack_into('<I', body, iat_off, new_va + hint_off)
    body[hint_off:hint_off + len(hint)] = hint
    body[name_off:name_off + len(dll)] = dll.encode()
    vsize = len(body)
    body += b'\0' * (_align(len(body), file_align) - len(body))

    b += b'\0' * (new_raw - len(b))
    b += body
    struct.pack_into('<8sIIIIIIHHI', b, secs + 40 * nsec, b'.cx', vsize, new_va, len(body), new_raw,
                     0, 0, 0, 0, 0xC0000040)
    struct.pack_into('<H', b, coff + 2, nsec + 1)
    struct.pack_into('<I', b, opt + 56, _align(new_va + vsize, sec_align))      # SizeOfImage
    struct.pack_into('<II', b, dd + 8, new_va + desc_off, 20 * n)              # import directory
    struct.pack_into('<II', b, dd + 8 * 11, 0, 0)                              # bound imports
    return bytes(b)


if __name__ == '__main__':
    src, dst, dll, exp = sys.argv[1:5]
    out = add_import(open(src, 'rb').read(), dll, exp)
    open(dst, 'wb').write(out)
