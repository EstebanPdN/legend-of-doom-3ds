#!/usr/bin/env python3
"""Validate CGFX dictionary lookups, optionally repairing their byte-based trees."""
import argparse
import struct
from pathlib import Path


class BannerError(ValueError):
    pass


def dictionaries(data):
    if len(data) < 28 or data[:4] != b'CGFX' or data[4:6] != b'\xff\xfe':
        raise BannerError('Expected a little-endian CGFX.')
    if struct.unpack_from('<I', data, 12)[0] != len(data) or len(data) > 0x80000:
        raise BannerError('Invalid CGFX size or HOME banner size limit exceeded.')
    start = struct.unpack_from('<H', data, 6)[0]
    if data[start:start + 4] != b'DATA':
        raise BannerError('Missing DATA block.')
    end = start + struct.unpack_from('<I', data, start + 4)[0]
    if end > len(data):
        raise BannerError('DATA block exceeds the file.')
    dictionaries = []
    offset = start
    while True:
        offset = data.find(b'DICT', offset, end)
        if offset < 0:
            break
        if offset + 12 > end:
            raise BannerError('Truncated DICT header.')
        size, count = struct.unpack_from('<II', data, offset + 4)
        if size != 12 + (count + 1) * 16 or offset + size > end:
            raise BannerError(f'Invalid DICT at {offset:#x}.')
        names, nodes = [], []
        for index in range(count + 1):
            address = offset + 12 + index * 16
            bit, left, right, name_offset, object_offset = struct.unpack_from('<IHHii', data, address)
            if left > count or right > count:
                raise BannerError(f'Invalid child in DICT {offset:#x}.')
            name = b''
            if index:
                name_address = address + 8 + name_offset
                object_address = address + 12 + object_offset
                if not name_offset or not start <= name_address < end or not object_offset or not start <= object_address < end:
                    raise BannerError(f'Invalid reference in DICT {offset:#x}.')
                stop = data.find(b'\0', name_address, end)
                if stop < 0:
                    raise BannerError('Unterminated dictionary name.')
                name = data[name_address:stop]
                if not name or name in names:
                    raise BannerError('Empty or duplicate dictionary name.')
            names.append(name)
            nodes.append((bit, left, right))
        dictionaries.append((offset, names, nodes))
        offset += size
    if not dictionaries:
        raise BannerError('No CGFX dictionaries found.')
    return dictionaries


def key_bit(key, bit):
    return (key[bit // 8] >> (bit % 8)) & 1 if bit // 8 < len(key) else 0


def lookup(nodes, key):
    parent, current = 0, nodes[0][1]
    for _ in range(len(nodes) + 1):
        if nodes[parent][0] <= nodes[current][0]:
            return current
        parent = current
        current = nodes[current][1 + key_bit(key, nodes[current][0])]
    raise BannerError('Dictionary contains a traversal cycle.')


def build_tree(names):
    nodes = [[0xffffffff, 0, 0]] + [[0, 0, 0] for _ in names[1:]]
    for index in sorted(range(1, len(names)), key=lambda i: -len(names[i])):
        key = names[index]
        other = names[lookup(nodes, key)]
        bits = [8 * i + (a ^ b).bit_length() - 1 for i, (a, b) in
                enumerate(zip(key.ljust(max(len(key), len(other)), b'\0'),
                              other.ljust(max(len(key), len(other)), b'\0'))) if a != b]
        if not bits:
            raise BannerError('Dictionary contains duplicate keys.')
        bit = max(bits)
        parent, current = 0, nodes[0][1]
        while nodes[parent][0] > nodes[current][0] and nodes[current][0] > bit:
            parent = current
            current = nodes[current][1 + key_bit(key, nodes[current][0])]
        nodes[index] = [bit, index, index]
        nodes[index][1 + (1 - key_bit(key, bit))] = current
        nodes[parent][1 + key_bit(key, nodes[parent][0])] = index
    return nodes


def validate(data):
    tables = dictionaries(data)
    failures = []
    for offset, names, nodes in tables:
        for index, key in enumerate(names[1:], 1):
            if lookup(nodes, key) != index:
                failures.append(f'DICT {offset:#x}: unresolved key {key.decode("utf-8", errors="replace")!r}')
    if failures:
        raise BannerError('\n'.join(failures))
    return len(tables), sum(len(names) - 1 for _, names, _ in tables)


def repair(data):
    output = bytearray(data)
    for offset, names, nodes in dictionaries(data):
        if all(lookup(nodes, key) == i for i, key in enumerate(names[1:], 1)):
            continue
        for index, node in enumerate(build_tree(names)):
            struct.pack_into('<IHH', output, offset + 12 + index * 16, *node)
    validate(output)
    return bytes(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('banner', type=Path)
    parser.add_argument('--repair', type=Path, metavar='OUTPUT')
    args = parser.parse_args()
    try:
        original = args.banner.read_bytes()
        data = repair(original) if args.repair else original
        tables, keys = validate(data)
        if args.repair:
            args.repair.write_bytes(data)
        print(f'Banner: {tables} dictionaries, {keys} keys verified.')
    except (BannerError, OSError, struct.error) as error:
        parser.exit(1, str(error) + '\n')
