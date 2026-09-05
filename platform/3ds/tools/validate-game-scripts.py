#!/usr/bin/env python3
"""Run this source tree's native GZDoom compiler without entering gameplay."""
import argparse
import subprocess
import tempfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', required=True, type=Path)
    parser.add_argument('--iwad', required=True, type=Path)
    parser.add_argument('--mod', required=True, type=Path)
    args = parser.parse_args()
    for path in (args.engine, args.iwad, args.mod):
        if not path.is_file():
            parser.error(f'File not found: {path}')
    with tempfile.TemporaryDirectory(prefix='lod-script-check-') as temporary:
        directory = Path(temporary)
        log = directory / 'parser.log'
        command = [str(args.engine.resolve()), '-iwad', str(args.iwad.resolve()),
                   '-file', str(args.mod.resolve()), '-noautoload', '-nosound', '-norun',
                   '-config', str(directory / 'check.ini'), '-errorlog', str(log)]
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        except subprocess.TimeoutExpired:
            print(log.read_text(errors='replace') if log.exists() else 'No parser log')
            print('Script compiler timed out')
            return 1
        # D_DoomMain returns 1337 after successful -norun initialization.
        if result.returncode != 1337 % 256:
            print(log.read_text(errors='replace') if log.exists() else result.stderr)
            return 1
    print('Game scripts: native compiler initialization passed (-norun)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
