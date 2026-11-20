#!/usr/bin/env python3
"""Build an SD-card UI language pack for InkOptimum from a translation yaml.

  make_language_pack.py --yaml german.yaml --keys lib/I18n/I18nKeys.h --output de.lang

Copy the result to the SD card as /.inkmod/languages/<code>.lang.
The yaml has the same format as lib/I18n/translations/*.yaml (`_language_name: "Deutsch"`
and `STR_KEY: "text"` lines). Missing keys fall back to English on the device.
The pack only works with the firmware whose I18nKeys.h it was built against (key hash).
The UI fonts must contain the glyphs of the language (Latin and Cyrillic are built in;
other scripts need an SD fallback font).
"""
import argparse
import pathlib
import re
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from gen_i18n import parse_yaml_file  # noqa: E402

MAX_FILE = 96 * 1024
MISSING = 0xFFFFFFFF


def read_keys(header: pathlib.Path) -> tuple[list[str], int]:
    text = header.read_text(encoding='utf-8')
    body = text.split('enum class StrId : uint16_t {', 1)[1].split('// Sentinel', 1)[0]
    keys = re.findall(r'^\s*(STR_[A-Z0-9_]+),', body, re.M)
    m = re.search(r'I18N_KEY_HASH\s*=\s*0x([0-9A-Fa-f]+)u', text)
    if not m:
        raise SystemExit('I18nKeys.h has no I18N_KEY_HASH: regenerate it with gen_i18n.py')
    return keys, int(m.group(1), 16)


def key_hash(keys: list[str]) -> int:
    h = 0x811C9DC5
    for key in keys:
        for b in (key + '\n').encode('utf-8'):
            h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


def main() -> None:
    p = argparse.ArgumentParser()
    p.add_argument('--yaml', required=True)
    p.add_argument('--keys', required=True, help='lib/I18n/I18nKeys.h of the target firmware')
    p.add_argument('--output', required=True)
    a = p.parse_args()

    keys, header_hash = read_keys(pathlib.Path(a.keys))
    if key_hash(keys) != header_hash:
        raise SystemExit('key list does not match I18N_KEY_HASH in the header')
    data = parse_yaml_file(a.yaml)
    name = data.get('_language_name', '').strip()
    if not name or len(name.encode('utf-8')) > 40:
        raise SystemExit('_language_name is required (up to 40 bytes)')

    blob = bytearray()
    offsets = []
    translated = 0
    for key in keys:
        value = data.get(key)
        if value is None:
            offsets.append(MISSING)
            continue
        offsets.append(len(blob))
        blob += value.encode('utf-8') + b'\0'
        translated += 1
    if not blob:
        blob = bytearray(b'\0')

    name_b = name.encode('utf-8')
    out = bytearray(b'ILNG')
    out += struct.pack('<BBHIHH', 1, 0, len(keys), header_hash, len(name_b), 0)
    out += name_b
    out += b''.join(struct.pack('<I', o) for o in offsets)
    out += blob
    if len(out) > MAX_FILE:
        raise SystemExit(f'pack is {len(out)} bytes, limit {MAX_FILE}')
    pathlib.Path(a.output).write_bytes(out)
    print(f'wrote {a.output}: {translated}/{len(keys)} strings, {len(out)} bytes')


if __name__ == '__main__':
    main()
