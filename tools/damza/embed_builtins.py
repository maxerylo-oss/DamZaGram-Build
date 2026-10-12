#!/usr/bin/env python3
"""Embed the built-in plugins into the client.

  tools/damza/embed_builtins.py [--check]

Packs every file under plugins/builtin/<id>/ into the generated source
Telegram/SourceFiles/damza/plugins/builtin/builtin_files.cpp. On start the
client installs them into tdata/plugins/<id>/ (see api/api_builtin.cpp).

Each plugin must have a manifest.json with "id" equal to the folder name,
a "version" and "builtin": true. Text files are stored as raw string
chunks below 16000 bytes (MSVC limits a single literal), binary files as
escaped byte strings. The output is deterministic: run the script after
every change in plugins/builtin/ and commit the result.

--check: don't write, exit with 1 if the generated file is out of date.
"""
import json, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, 'plugins', 'builtin')
OUT = os.path.join(ROOT, 'Telegram', 'SourceFiles', 'damza', 'plugins', 'builtin', 'builtin_files.cpp')
CHUNK = 12000          # bytes per text literal, MSVC limit is ~16380
BINARY_CHUNK = 2048    # bytes per escaped binary literal
SKIP_DIRS = {'node_modules', '__pycache__', '.git'}
SKIP_FILES = re.compile(r'(^\.|\.test\.js$|~$|\.swp$)')
ID = re.compile(r'^[a-z0-9][a-z0-9-]*$')


def fail(message):
    print('embed_builtins: ' + message, file=sys.stderr)
    sys.exit(1)


def collect():
    if not os.path.isdir(SRC):
        return []
    plugins = []
    for plugin_id in sorted(os.listdir(SRC)):
        folder = os.path.join(SRC, plugin_id)
        if not os.path.isdir(folder) or plugin_id.startswith('.'):
            continue
        if not ID.match(plugin_id):
            fail('bad plugin folder name: %s' % plugin_id)
        manifest_path = os.path.join(folder, 'manifest.json')
        if not os.path.isfile(manifest_path):
            fail('%s: manifest.json is missing' % plugin_id)
        try:
            manifest = json.load(open(manifest_path, encoding='utf-8'))
        except ValueError as e:
            fail('%s/manifest.json: %s' % (plugin_id, e))
        if manifest.get('id') != plugin_id:
            fail('%s/manifest.json: "id" must be "%s"' % (plugin_id, plugin_id))
        if manifest.get('builtin') is not True:
            fail('%s/manifest.json: "builtin": true is required' % plugin_id)
        if not re.match(r'^\d+\.\d+\.\d+', str(manifest.get('version', ''))):
            fail('%s/manifest.json: "version" must be semver' % plugin_id)
        main = manifest.get('main', 'main.js')
        if not os.path.isfile(os.path.join(folder, main)):
            fail('%s: entry file %s is missing' % (plugin_id, main))
        files = []
        for base, dirs, names in os.walk(folder):
            dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith('.'))
            for name in sorted(names):
                if SKIP_FILES.search(name):
                    continue
                full = os.path.join(base, name)
                relative = os.path.relpath(full, SRC).replace(os.sep, '/')
                files.append((relative, open(full, 'rb').read()))
        # The manifest goes last: a half-written install stays "outdated".
        files.sort(key=lambda f: (f[0].endswith('/manifest.json'), f[0]))
        plugins.append((plugin_id, manifest, files))
    return plugins


def text_chunks(text):
    """Split at line ends, each chunk <= CHUNK bytes in UTF-8."""
    chunks, current, size = [], [], 0
    for line in text.splitlines(keepends=True):
        encoded = len(line.encode('utf-8'))
        while encoded > CHUNK:
            # A huge single line: split by characters.
            cut, total = 0, 0
            for i, ch in enumerate(line):
                total += len(ch.encode('utf-8'))
                if total > CHUNK:
                    break
                cut = i + 1
            if current:
                chunks.append(''.join(current))
                current, size = [], 0
            chunks.append(line[:cut])
            line = line[cut:]
            encoded = len(line.encode('utf-8'))
        if size + encoded > CHUNK and current:
            chunks.append(''.join(current))
            current, size = [], 0
        current.append(line)
        size += encoded
    if current:
        chunks.append(''.join(current))
    return chunks or ['']


def delimiter_for(text):
    for i in range(100):
        delimiter = 'DZGB' if i == 0 else 'DZGB%d' % i
        if (')' + delimiter + '"') not in text:
            return delimiter
    fail('no raw string delimiter fits')


def escape_bytes(data):
    return ''.join('\\%03o' % b for b in data)


def literals(data):
    try:
        text = data.decode('utf-8')
        if '\0' in text:
            raise UnicodeDecodeError('utf-8', data, 0, 1, 'NUL')
    except UnicodeDecodeError:
        return ['"%s"' % escape_bytes(data[i:i + BINARY_CHUNK])
                for i in range(0, len(data), BINARY_CHUNK)] or ['""']
    text = text.replace('\r\n', '\n')
    delimiter = delimiter_for(text)
    return ['R"%s(%s)%s"' % (delimiter, chunk, delimiter) for chunk in text_chunks(text)]


def generate(plugins):
    out = [
        '// This is the source code of DamZaGram for Desktop,',
        '// a fork of AyuGram for Desktop.',
        '//',
        '// Generated by tools/damza/embed_builtins.py from plugins/builtin/.',
        '// Do not edit: change the plugin and run the script again.',
        '#include "damza/plugins/builtin/builtin_files.h"',
        '',
        'namespace Damza::Plugins::Builtin {',
        'namespace {',
        '',
    ]
    entries = []
    index = 0
    for plugin_id, manifest, files in plugins:
        for relative, data in files:
            name = 'kFile%d' % index
            index += 1
            out.append('// %s' % relative)
            out.append('const char *const %s[] = {' % name)
            for literal in literals(data):
                out.append(literal + ',')
            out.append('nullptr,')
            out.append('};')
            out.append('')
            entries.append('\t\t{ "%s", %s },' % (relative, name))
    out += [
        '} // namespace',
        '',
        'const std::vector<File> &Files() {',
        '\tstatic const auto result = std::vector<File>{',
    ]
    out += entries
    out += [
        '\t};',
        '\treturn result;',
        '}',
        '',
        '} // namespace Damza::Plugins::Builtin',
        '',
    ]
    return '\n'.join(out)


def main():
    plugins = collect()
    content = generate(plugins)
    old = open(OUT, encoding='utf-8').read() if os.path.exists(OUT) else None
    if '--check' in sys.argv:
        if old != content:
            print('builtin_files.cpp is out of date, run tools/damza/embed_builtins.py')
            return 1
        print('builtin_files.cpp is up to date')
        return 0
    if old != content:
        os.makedirs(os.path.dirname(OUT), exist_ok=True)
        with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
            f.write(content)
    total = sum(len(d) for _, _, files in plugins for _, d in files)
    for plugin_id, manifest, files in plugins:
        print('%-18s %-8s %d file(s)' % (plugin_id, manifest['version'], len(files)))
    print('%d plugin(s), %d bytes -> %s%s' % (
        len(plugins), total, os.path.relpath(OUT, ROOT), '' if old != content else ' (unchanged)'))
    return 0


if __name__ == '__main__':
    sys.exit(main())
