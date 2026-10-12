#!/usr/bin/env python3
"""Compile C++ files of the client with -fsyntax-only, using the exact flags
from out/compile_commands.json. Fast (seconds per file), safe to run in
parallel with other checks, never touches the build directory.

  tools/damza/check.py Telegram/SourceFiles/damza/plugins/api/api_x.cpp [...]

Files missing from compile_commands.json (new files) reuse the flags of
damza/plugins/plugin_bridge.cpp. Exit code is non-zero if any file fails.
"""
import json, os, shlex, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DB = os.path.join(ROOT, 'out', 'compile_commands.json')
REFERENCE = os.path.join(ROOT, 'Telegram', 'SourceFiles', 'damza', 'plugins', 'plugin_bridge.cpp')

def command_for(db, path):
    for entry in db:
        if os.path.abspath(entry['file']) == path:
            return entry, False
    for entry in db:
        if os.path.abspath(entry['file']) == REFERENCE:
            return entry, True
    raise SystemExit('reference file not found in compile_commands.json')

def main():
    files = [os.path.abspath(f) for f in sys.argv[1:]]
    if not files:
        print(__doc__)
        return 2
    db = json.load(open(DB))
    failed = 0
    for path in files:
        entry, borrowed = command_for(db, path)
        args = shlex.split(entry['command']) if 'command' in entry else list(entry['arguments'])
        out = []
        skip = False
        for i, a in enumerate(args):
            if skip:
                skip = False
                continue
            if a in ('-o', '-MF', '-MT'):
                skip = True
                continue
            if a in ('-MD', '-c'):
                continue
            if a.startswith('-DLANG_KEYS_SUBSET='):
                continue
            if os.path.abspath(os.path.join(entry['directory'], a)) == os.path.abspath(entry['file']):
                continue
            out.append(a)
        if out and out[0] == 'ccache':
            out = out[1:]
        out += ['-fsyntax-only', path]
        result = subprocess.run(out, cwd=entry['directory'])
        status = 'OK' if result.returncode == 0 else 'FAILED'
        print('%s %s%s' % (status, os.path.relpath(path, ROOT), ' (borrowed flags)' if borrowed else ''))
        failed += result.returncode != 0
    return 1 if failed else 0

if __name__ == '__main__':
    sys.exit(main())
