#!/usr/bin/env python3
"""Create HUD test saves from a fresh MAP01 save made by this engine."""
import argparse
import copy
import hashlib
import json
import zipfile
from pathlib import Path

HEARTS = (3, 6, 9, 12, 16)


def create_saves(base, output):
    with zipfile.ZipFile(base) as archive:
        if archive.testzip():
            raise ValueError('Corrupt base save')
        original = {name: archive.read(name) for name in archive.namelist()}
    info = json.loads(original['info.json'])
    snapshot = json.loads(original['map01.map.json'])
    if info['Current Map'] != 'MAP01' or snapshot['numplayers'] != 1:
        raise ValueError('Expected a single-player MAP01 save')
    player = snapshot['players'][0]
    actor = snapshot['objects'][player['mo']]
    if actor['classtype'] != 'ZeldaPlayer' or actor['pos'] != [-1216.0, -3328.0, 0.0]:
        raise ValueError('Expected Link at the initial spawn')
    if actor['class:PlayerPawn']['MaxHealth'] != 24 or player['cheats'] or snapshot['frozenstate']:
        raise ValueError('Expected a fresh, unfrozen save without cheats')
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for hearts in HEARTS:
        title = f'{hearts:02} Hearts'
        name = f'lod-test-{hearts:02}-hearts.zds'
        path = output / name
        if path.exists():
            raise FileExistsError(path)
        entries = original.copy()
        metadata = dict(info, Title=title)
        level = copy.deepcopy(snapshot)
        player = level['players'][0]
        actor = level['objects'][player['mo']]
        player['health'] = actor['health'] = hearts * 8
        actor['class:PlayerPawn']['MaxHealth'] = hearts * 8
        actor['class:PlayerPawn']['MugShotMaxHealth'] = hearts * 8
        player['playername'] = player['userinfo']['name'] = 'Link'
        entries['info.json'] = json.dumps(metadata, ensure_ascii=False).encode()
        entries['map01.map.json'] = json.dumps(level, ensure_ascii=False).encode()
        with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for key, data in entries.items():
                archive.writestr(key, data)
        with zipfile.ZipFile(path) as archive:
            assert archive.testzip() is None
        files.append(path)
    (output/'SHA256SUMS.txt').write_text(''.join(f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
    return files


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    for path in create_saves(args.base, args.output):
        print(path)
