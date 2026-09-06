#!/usr/bin/env python3
"""Scale the static HOME logo about the banner camera's center."""
import argparse
import json
import struct
from pathlib import Path


def scale_glb(data, scale, shift_x=0):
    magic, version, length = struct.unpack_from('<4sII', data)
    if magic != b'glTF' or version != 2 or length != len(data) or not 0 < scale <= 1:
        raise ValueError('Expected a GLB 2.0 file and a scale in (0, 1].')
    json_size, json_type = struct.unpack_from('<I4s', data, 12)
    model = json.loads(data[20:20 + json_size])
    binary_offset = 20 + json_size
    binary_size, binary_type = struct.unpack_from('<I4s', data, binary_offset)
    if json_type != b'JSON' or binary_type != b'BIN\0' or model.get('skins') or model.get('animations'):
        raise ValueError('Expected a static JSON/BIN logo.')
    if any(any(k in n for k in ('translation', 'rotation', 'scale', 'matrix')) for n in model['nodes']):
        raise ValueError('Apply node transforms before scaling the logo.')
    binary = bytearray(data[binary_offset + 8:binary_offset + 8 + binary_size])
    positions = {p['attributes']['POSITION'] for m in model['meshes'] for p in m['primitives']}
    horizontal_offset = shift_x * scale * (
        max(model['accessors'][i]['max'][0] for i in positions) -
        min(model['accessors'][i]['min'][0] for i in positions)) if shift_x else 0
    for index in positions:
        accessor = model['accessors'][index]
        if accessor['componentType'] != 5126 or accessor['type'] != 'VEC3' or 'sparse' in accessor:
            raise ValueError('Expected dense float32 positions.')
        view = model['bufferViews'][accessor['bufferView']]
        start = view.get('byteOffset', 0) + accessor.get('byteOffset', 0)
        stride = view.get('byteStride', 12)
        points = []
        for vertex in range(accessor['count']):
            offset = start + vertex * stride
            point = struct.unpack_from('<3f', binary, offset)
            point = tuple(center + (value - center) * scale for value, center in zip(point, (0, 1, 0)))
            point = (point[0] + horizontal_offset, point[1], point[2])
            struct.pack_into('<3f', binary, offset, *point)
            points.append(struct.unpack_from('<3f', binary, offset))
        accessor['min'] = [min(p[i] for p in points) for i in range(3)]
        accessor['max'] = [max(p[i] for p in points) for i in range(3)]
    encoded = json.dumps(model, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    return (struct.pack('<4sII', b'glTF', 2, total) + struct.pack('<I4s', len(encoded), b'JSON') +
            encoded + struct.pack('<I4s', len(binary), b'BIN\0') + binary)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('input', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scale', type=float, default=0.85)
    parser.add_argument('--shift-x', type=float, default=0,
                        help='Horizontal offset as a fraction of the scaled logo width.')
    args = parser.parse_args()
    args.output.write_bytes(scale_glb(args.input.read_bytes(), args.scale, args.shift_x))
