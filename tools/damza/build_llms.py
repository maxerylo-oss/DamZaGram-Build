#!/usr/bin/env python3
"""Build docs/plugins/llms-full.txt: llms.txt + the API reference + every
typings file, as one plain-text file an AI agent can load in one go.

  python3 tools/damza/build_llms.py          write the file
  python3 tools/damza/build_llms.py --check  exit 1 if it is out of date

Re-run after changing llms.txt, docs/plugins/reference.md or
docs/plugins/types/*.d.ts.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DOCS = os.path.join(ROOT, 'docs', 'plugins')
OUT = os.path.join(DOCS, 'llms-full.txt')


def read(path):
    with open(path, encoding='utf-8') as f:
        return f.read().rstrip() + '\n'


def typings_order():
    index = read(os.path.join(DOCS, 'types', 'index.d.ts'))
    names = re.findall(r'^/// <reference path="\./([^"/]+)"', index, re.M)
    rest = sorted(n for n in os.listdir(os.path.join(DOCS, 'types'))
                  if n.endswith('.d.ts') and n != 'index.d.ts' and n not in names)
    return names + rest


def build():
    parts = [read(os.path.join(ROOT, 'llms.txt'))]
    parts.append('\n\n# API reference (docs/plugins/reference.md)\n\n')
    parts.append(read(os.path.join(DOCS, 'reference.md')))
    parts.append('\n\n# Typings (docs/plugins/types/*.d.ts) — ground truth for signatures\n')
    for name in typings_order():
        parts.append('\n\n## docs/plugins/types/%s\n\n```ts\n' % name)
        parts.append(read(os.path.join(DOCS, 'types', name)))
        parts.append('```\n')
    return ''.join(parts)


def main():
    text = build()
    if '--check' in sys.argv:
        current = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else ''
        if current != text:
            print('llms-full.txt is out of date: run tools/damza/build_llms.py')
            return 1
        print('llms-full.txt is up to date')
        return 0
    with open(OUT, 'w', encoding='utf-8') as f:
        f.write(text)
    print('wrote %s (%d KB)' % (os.path.relpath(OUT, ROOT), len(text.encode()) // 1024))
    return 0


if __name__ == '__main__':
    sys.exit(main())
