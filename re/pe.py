"""Tiny PE helper: map virtual addresses to file bytes (read-only).

  python re/pe.py <exe> <va-hex> <count> [fmt]     fmt: struct format per item (default '<3i')
"""
import struct, sys


class PE:
    def __init__(self, path):
        self.b = open(path, 'rb').read()
        e = struct.unpack_from('<I', self.b, 0x3c)[0]
        nsec = struct.unpack_from('<H', self.b, e + 6)[0]
        optsz = struct.unpack_from('<H', self.b, e + 20)[0]
        self.base = struct.unpack_from('<I', self.b, e + 24 + 28)[0]
        self.secs = []
        s = e + 24 + optsz
        for i in range(nsec):
            name = self.b[s:s + 8].rstrip(b'\0').decode()
            vsz, va, rsz, rp = struct.unpack_from('<4I', self.b, s + 8)
            self.secs.append((name, va, max(vsz, rsz), rp, rsz))
            s += 40

    def off(self, va):
        rva = va - self.base
        for name, sva, sz, rp, rsz in self.secs:
            if sva <= rva < sva + sz:
                if rva - sva >= rsz:
                    return None  # bss
                return rp + rva - sva
        raise ValueError(hex(va))

    def read(self, va, n):
        o = self.off(va)
        return None if o is None else self.b[o:o + n]


if __name__ == '__main__':
    pe = PE(sys.argv[1]); va = int(sys.argv[2], 16); n = int(sys.argv[3])
    fmt = sys.argv[4] if len(sys.argv) > 4 else '<3i'
    sz = struct.calcsize(fmt)
    for i in range(n):
        print(hex(va + i * sz), struct.unpack(fmt, pe.read(va + i * sz, sz)))
