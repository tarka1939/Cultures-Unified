"""Disassemble a range of a PE (x86) with capstone.

  python re/disasm.py <exe> <va-hex> <count-bytes> [grep-substring]
"""
import os, re, sys
import capstone
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe


def dis(path, va, n):
    p = pe.PE(path)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    return list(md.disasm(p.read(va, n), va))


if __name__ == '__main__':
    exe, va, n = sys.argv[1], int(sys.argv[2], 16), int(sys.argv[3], 0)
    g = sys.argv[4].lower() if len(sys.argv) > 4 else None
    for i in dis(exe, va, n):
        line = f'{i.address:08x}  {i.bytes.hex():20s} {i.mnemonic} {i.op_str}'
        if not g or re.search(g, line.lower()):
            print(line)
