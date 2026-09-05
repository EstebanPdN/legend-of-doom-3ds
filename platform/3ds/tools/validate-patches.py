#!/usr/bin/env python3
"""Reject inconsistent unified-diff hunk sizes before git can discard lines."""
import re
import sys
from pathlib import Path

HEADER = re.compile(r'^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@')


def validate(text):
    errors = []
    expected = actual = None
    start = 0

    def finish():
        if expected is not None and actual != expected:
            errors.append(f'line {start}: hunk declares {expected}, contains {actual}')

    for number, line in enumerate(text.splitlines(), 1):
        match = HEADER.match(line)
        if match or line.startswith('diff --git '):
            finish()
            expected = actual = None
        if match:
            expected = [int(match[2] or 1), int(match[4] or 1)]
            actual = [0, 0]
            start = number
        elif actual is not None:
            if line.startswith(' '):
                actual[0] += 1
                actual[1] += 1
            elif line.startswith('-'):
                actual[0] += 1
            elif line.startswith('+'):
                actual[1] += 1
            elif not line:
                if actual != expected:
                    actual[0] += 1
                    actual[1] += 1
            elif line.startswith('\\ No newline at end of file'):
                continue
            else:
                finish()
                expected = actual = None
    finish()
    return errors


def main():
    paths = [Path(p) for p in sys.argv[1:]]
    if not paths:
        paths = sorted(Path(__file__).resolve().parents[1].joinpath('patches').glob('*.patch'))
    failed = False
    for path in paths:
        for error in validate(path.read_text()):
            print(f'{path}: {error}', file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
