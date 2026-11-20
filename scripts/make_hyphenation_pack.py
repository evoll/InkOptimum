#!/usr/bin/env python3
"""Wrap a hypher/Typst `.bin` hyphenation trie into an SD-card pack for InkOptimum.

Copy the result to the SD card as /.inkmod/hyphenation/hyph-<primary tag>.trie
(e.g. hyph-nl.trie). The pack is used for languages that are not built into the
firmware; built-in languages (en, fr, de, ru, es, it, pl, sv) always win.

Usage:
  make_hyphenation_pack.py --input hyph-nl.bin --output hyph-nl.trie \
      --script latin [--min-prefix 2] [--min-suffix 2]
Scripts: latin, cyrillic (the letter classes the hyphenator knows about).
"""
import argparse
import pathlib

MAX_FILE = 160 * 1024


def build(blob: bytes, script: str, min_prefix: int, min_suffix: int) -> bytes:
    if len(blob) < 5:
        raise SystemExit('trie too small')
    root = int.from_bytes(blob[:4], 'big')
    if root < 4 or root >= len(blob):
        raise SystemExit(f'bad root address {root} for blob of {len(blob)} bytes')
    if not (0 <= min_prefix < 256 and 0 <= min_suffix < 256):
        raise SystemExit('min prefix/suffix out of range')
    header = b'IHYP' + bytes([{'latin': 0, 'cyrillic': 1}[script], min_prefix, min_suffix, 0])
    out = header + blob
    if len(out) > MAX_FILE:
        raise SystemExit(f'pack is {len(out)} bytes, limit {MAX_FILE}')
    return out


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--input', required=True)
    p.add_argument('--output', required=True)
    p.add_argument('--script', choices=['latin', 'cyrillic'], default='latin')
    p.add_argument('--min-prefix', type=int, default=0, help='0 = firmware default')
    p.add_argument('--min-suffix', type=int, default=0, help='0 = firmware default')
    a = p.parse_args()
    data = build(pathlib.Path(a.input).read_bytes(), a.script, a.min_prefix, a.min_suffix)
    pathlib.Path(a.output).write_bytes(data)
    print(f'wrote {a.output} ({len(data)} bytes)')


if __name__ == '__main__':
    main()
