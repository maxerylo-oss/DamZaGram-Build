#!/usr/bin/env python3
"""Run the sandbox tests of the built-in plugins.

  tools/damza/tests/builtins/run.py [plugin-id ...]

For every plugins/builtin/<id>/ runs tools/damza/sandbox.py with
  --setup=_setup.js            mocks of the client-facing API
  --setup=<defaults>           the manifest's setting defaults
and tools/damza/tests/builtins/<id>.test.js. Exit code is non-zero if any
test fails or a plugin has no test.
"""
import json, os, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(HERE))))
BUILTIN = os.path.join(ROOT, 'plugins', 'builtin')
SANDBOX = os.path.join(ROOT, 'tools', 'damza', 'sandbox.py')


def main():
    wanted = sys.argv[1:]
    ids = sorted(d for d in os.listdir(BUILTIN) if os.path.isdir(os.path.join(BUILTIN, d)))
    if wanted:
        ids = [i for i in ids if i in wanted]
    failed = []
    with tempfile.TemporaryDirectory() as tmp:
        for plugin_id in ids:
            folder = os.path.join(BUILTIN, plugin_id)
            manifest = json.load(open(os.path.join(folder, 'manifest.json'), encoding='utf-8'))
            test = os.path.join(HERE, plugin_id + '.test.js')
            if not os.path.isfile(test):
                print('MISSING TEST %s' % plugin_id)
                failed.append(plugin_id)
                continue
            defaults = {s['key']: s.get('default') for s in manifest.get('settings', []) if 'key' in s}
            setup = os.path.join(tmp, plugin_id + '.defaults.js')
            with open(setup, 'w', encoding='utf-8') as f:
                f.write('Object.assign(sandbox.settings, %s);\n' % json.dumps(defaults, ensure_ascii=False))
                f.write('mock.pluginDir = %s;\n' % json.dumps(folder))
                f.write('mock.manifest = %s;\n' % json.dumps(manifest, ensure_ascii=False))
            result = subprocess.run(
                [sys.executable, SANDBOX, os.path.join(folder, manifest.get('main', 'main.js')), test,
                 '--setup=' + os.path.join(HERE, '_setup.js'), '--setup=' + setup],
                capture_output=True, text=True)
            ok = result.returncode == 0 and 'SANDBOX OK' in result.stdout
            print('%s %s' % ('PASS' if ok else 'FAIL', plugin_id))
            if not ok or '-v' in os.environ.get('DZG_TEST_FLAGS', ''):
                sys.stdout.write(result.stdout)
                sys.stdout.write(result.stderr)
            if not ok:
                failed.append(plugin_id)
    if failed:
        print('failed: ' + ', '.join(failed))
        return 1
    print('all %d built-in plugin tests passed' % len(ids))
    return 0


if __name__ == '__main__':
    sys.exit(main())
