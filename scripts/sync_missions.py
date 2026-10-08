#!/usr/bin/env python3
"""Compile the canonical engineering mission register for the shared Web/Desktop UI.

Run after editing docs/FUTURE_MISSIONS.md; --check asserts the packaged UI is current.
Only uncompleted checkbox missions are published; completion still requires evidence.
"""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'docs' / 'FUTURE_MISSIONS.md'
OUTPUT = ROOT / 'projects' / 'web' / 'js' / 'mission_data.js'


def build():
    priority = None
    missions = []
    for line in SOURCE.read_text(encoding='utf-8').splitlines():
        section = re.match(r'^## (P[012])\s+—\s+(.+)$', line)
        if section:
            priority = section.group(1)
            continue
        if line.startswith('## '):
            priority = None
            continue
        item = re.match(r'^- \[ \] \*\*(.+?)\*\*\s*(.*)$', line)
        plain = re.match(r'^- \[ \] (.+)$', line)
        if priority and (item or plain):
            title = (item.group(1) if item else plain.group(1)).rstrip('. ')
            explanation = item.group(2).strip().lstrip('. ').strip() if item else ''
            missions.append({'id': f'{priority}-{len(missions)+1:03}', 'priority': priority,
                             'title': title, 'details': explanation, 'status': 'open'})
    if not missions or not {item['priority'] for item in missions} == {'P0', 'P1', 'P2'}:
        raise ValueError('Mission register lacks one or more priority groups')
    payload = json.dumps(missions, ensure_ascii=False, indent=2)
    return '// GENERATED from docs/FUTURE_MISSIONS.md by scripts/sync_missions.py. Do not edit.\n' + \
           'const ENGCODER_OPEN_MISSIONS = Object.freeze(' + payload + ');\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    result = build()
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding='utf-8') != result:
            parser.error('Mission UI is out of sync; run scripts/sync_missions.py')
        print(f'PASS mission source sync ({result.count("\"status\": \"open\"")} open missions)')
    else:
        OUTPUT.write_text(result, encoding='utf-8')
        print(f'Updated {OUTPUT.relative_to(ROOT)}')
