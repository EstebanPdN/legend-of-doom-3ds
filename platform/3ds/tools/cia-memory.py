#!/usr/bin/env python3
"""Configure HOME metadata and verify memory modes in the packaged CIA."""
import struct
import sys
from pathlib import Path

NEW_3DS = 0x1000


def mark_new3ds(data):
    if len(data) != 0x36C0 or data[:4] != b'SMDH':
        raise ValueError('Invalid SMDH')
    out = bytearray(data)
    flags = struct.unpack_from('<I', out, 0x2028)[0]
    struct.pack_into('<I', out, 0x2028, flags | NEW_3DS)
    return out


def verify(data):
    def part(offset, size):
        if offset < 0 or offset + size > len(data):
            raise ValueError('Truncated CIA')
        return data[offset:offset + size]

    header = struct.unpack('<IHHIIIIQ', part(0, 32))
    head, cert, ticket, tmd = header[0], header[3], header[4], header[5]
    ncch = sum((size + 63) & ~63 for size in (head, cert, ticket, tmd))
    if part(ncch + 0x100, 4) != b'NCCH':
        raise ValueError('Missing executable NCCH')
    exheader = ncch + 0x200
    flags = part(exheader + 0x20C, 4)
    if flags[1] & 15 != 1 or flags[2] >> 4 != 2:
        raise ValueError('Expected New 3DS 124 MiB / legacy 96 MiB memory modes')
    exefs = ncch + struct.unpack('<I', part(ncch + 0x1A0, 4))[0] * 512
    for i in range(10):
        name, offset, size = struct.unpack('<8sII', part(exefs + i * 16, 16))
        if name.rstrip(b'\0') == b'icon':
            smdh = part(exefs + 512 + offset, size)
            if bytes(mark_new3ds(smdh)) != smdh:
                raise ValueError('Missing New 3DS-only HOME flag')
            return
    raise ValueError('Missing SMDH in executable')


if __name__ == '__main__':
    mode, filename = sys.argv[1:]
    path = Path(filename)
    if mode == 'smdh':
        path.write_bytes(mark_new3ds(path.read_bytes()))
    elif mode == 'verify':
        verify(path.read_bytes())
        print('CIA memory: New 3DS 124 MiB, legacy fallback 96 MiB, HOME New 3DS-only verified.')
    else:
        raise SystemExit('Use smdh or verify')
