#!/usr/bin/env python3
"""Run a DamZaGram plugin outside the client, against a mocked native bridge.

  tools/damza/sandbox.py path/to/plugin/main.js [test.js] [--script] [--setup=setup.js]

Loads the real JS prelude and every API module fragment (extracted from the
C++ sources), then the plugin (as an ES module unless --script), then the
optional test file. In the test, `sandbox` gives access to recorded native
calls, dispatch(), event(), settle(), runTimers(), settings and storage.
--setup runs a script after the API is built and before the plugin, e.g. to
stub API functions of modules that are not written yet.
A test passes if it doesn't throw. Exit code is non-zero on any error.
"""
import os, re, subprocess, sys, tempfile, glob

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'Telegram', 'SourceFiles', 'damza', 'plugins')
QJS = os.path.join(ROOT, 'out-qjs', 'qjs')

def extract(path, tag):
    text = open(path, encoding='utf-8').read()
    return re.findall(r'R"%s\((.*?)\)%s"' % (tag, tag), text, re.S)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    as_script = '--script' in sys.argv
    setups = [a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--setup=')]
    if not args:
        print(__doc__)
        return 2
    plugin = os.path.abspath(args[0])
    test = os.path.abspath(args[1]) if len(args) > 1 else None
    parts = [open(os.path.join(ROOT, 'tools', 'damza', 'sandbox_mock.js'), encoding='utf-8').read()]
    parts += extract(os.path.join(SRC, 'plugin_prelude.cpp'), 'DZG_PRELUDE')
    for path in sorted(glob.glob(os.path.join(SRC, 'api', '*.cpp'))):
        name = os.path.splitext(os.path.basename(path))[0]
        for code in extract(path, 'DZG_JS'):
            parts.append('// module %s\n(function (native, dzg, internal) {\n%s\n})(globalThis.__native, globalThis.dzg, globalThis.__dzgInternal);' % (name, code))
    parts.append('globalThis.__sandboxDispatch = globalThis.__dzgDispatch; delete globalThis.__native; delete globalThis.__dzgInternal;')
    for setup in setups:
        parts.append(open(setup, encoding='utf-8').read())
    with tempfile.TemporaryDirectory() as tmp:
        boot = os.path.join(tmp, 'boot.js')
        open(boot, 'w', encoding='utf-8').write('\n;\n'.join(parts))
        entry = os.path.join(tmp, 'entry.mjs')
        lines = []
        if as_script:
            lines.append('import * as std from "qjs:std"; std.loadScript(%r);' % plugin)
        else:
            lines.append('import %r;' % plugin)
        if test:
            lines.append('import %r;' % test)
        lines.append('print("SANDBOX OK");')
        open(entry, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
        result = subprocess.run([QJS, '--std', '-I', boot, '-m', entry])
        return result.returncode

if __name__ == '__main__':
    sys.exit(main())
