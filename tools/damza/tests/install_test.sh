#!/bin/sh
# Builds and runs tools/damza/tests/install_test.cpp: the plugin installer
# (damza/plugins/plugin_install.cpp) against a stub Manager, no client build.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE="$ROOT/tools/damza/tests"
SRC="$ROOT/Telegram/SourceFiles"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/dzg_install_test.XXXXXX")
trap 'rm -rf "$WORK"' EXIT
python3 - "$WORK" <<'PY'
import os, sys, zipfile
work = sys.argv[1]
with zipfile.ZipFile(os.path.join(work, 'nested.zip'), 'w') as z:
    z.writestr('repo-main/', '')
    z.writestr('repo-main/plugin/manifest.json', '{"id":"zipped","name":"Zipped","version":"2.0.0"}')
    z.writestr('repo-main/plugin/main.js', 'import { x } from "./lib/util.js"; dzg.log(x);')
    z.writestr('repo-main/plugin/lib/util.js', 'export const x = 1;')
    z.writestr('repo-main/plugin/.git/config', 'x')
    z.writestr('repo-main/plugin/empty.txt', '')
    z.writestr('repo-main/outside.txt', 'x')
    z.writestr('repo-main/other/manifest.json/', '')
with zipfile.ZipFile(os.path.join(work, 'single.dzg.zip'), 'w') as z:
    z.writestr('lonely.plugin.js', '// @version 1.0.0\ndzg.log(1);\n')
    z.writestr('notes.txt', 'hi')
with zipfile.ZipFile(os.path.join(work, 'evil.zip'), 'w') as z:
    z.writestr('manifest.json', '{"id":"evil"}')
    z.writestr('main.js', '1')
    z.writestr('../escape.js', '1')
os.makedirs(os.path.join(work, 'folder', 'node_modules', 'x'))
open(os.path.join(work, 'folder', 'manifest.json'), 'w').write('{"id":"folder-plugin"}')
open(os.path.join(work, 'folder', 'main.js'), 'w').write('1')
open(os.path.join(work, 'folder', 'node_modules', 'x', 'index.js'), 'w').write('1')
PY
g++ -std=c++20 -O0 -g -fPIC -Wall -Wno-unused-parameter -Wno-unused-result \
	-include "$HERE/install_stub/prelude.h" \
	-I"$HERE/install_stub" -I"$SRC" -I"$ROOT/Telegram/lib_base" \
	"$SRC/damza/plugins/plugin_install.cpp" \
	"$SRC/damza/plugins/plugin_types_embedded.cpp" \
	"$SRC/damza/plugins/builtin/builtin_files.cpp" \
	"$HERE/install_test.cpp" \
	$(pkg-config --cflags --libs Qt6Core Qt6Gui Qt6Network minizip) -o "$WORK/install_test"
"$WORK/install_test" "$WORK"
