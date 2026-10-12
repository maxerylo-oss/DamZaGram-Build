#!/usr/bin/env python3
"""Extracts the "Create plugin" main.js template from plugin_install.cpp and
runs it with scaffold.test.js in tools/damza/sandbox.py."""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
SOURCE = os.path.join(ROOT, 'Telegram', 'SourceFiles', 'damza', 'plugins', 'plugin_install.cpp')

def main():
    text = open(SOURCE, encoding='utf-8').read()
    match = re.search(r'R"DZG_TEMPLATE\((.*?)\)DZG_TEMPLATE"', text, re.S)
    if not match:
        sys.exit('template not found in plugin_install.cpp')
    code = (match.group(1)
        .replace('%NAME%', 'Test plugin')
        .replace('%AUTHOR%', '@wetakush')
        .replace('%COMMAND%', 'my_plugin'))
    with tempfile.TemporaryDirectory() as tmp:
        main_js = os.path.join(tmp, 'main.js')
        open(main_js, 'w', encoding='utf-8').write(code)
        return subprocess.run([
            sys.executable,
            os.path.join(ROOT, 'tools', 'damza', 'sandbox.py'),
            main_js,
            os.path.join(HERE, 'scaffold.test.js'),
        ]).returncode

if __name__ == '__main__':
    sys.exit(main())
