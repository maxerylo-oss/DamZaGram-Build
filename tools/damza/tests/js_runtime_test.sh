#!/bin/sh
# Builds and runs tools/damza/tests/js_runtime_test.cpp: Js::Runtime
# (damza/plugins/plugin_js.cpp) against QtCore and QuickJS-ng only.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
HERE="$ROOT/tools/damza/tests"
SRC="$ROOT/Telegram/SourceFiles"
QJS="$ROOT/Telegram/ThirdParty/quickjs-ng"
OUT=${TMPDIR:-/tmp}/dzg_js_runtime_test
mkdir -p "$OUT"
for name in dtoa libregexp libunicode quickjs; do
	if [ ! -f "$OUT/$name.o" ] || [ "$QJS/$name.c" -nt "$OUT/$name.o" ]; then
		gcc -std=gnu11 -O2 -w -D_GNU_SOURCE -c "$QJS/$name.c" -o "$OUT/$name.o"
	fi
done
g++ -std=c++20 -O1 -g -fPIC -Wall -Wno-unused-parameter \
	-include "$HERE/js_stub/prelude.h" \
	-I"$SRC" -I"$QJS" \
	"$SRC/damza/plugins/plugin_js.cpp" \
	"$HERE/js_runtime_test.cpp" \
	"$OUT/dtoa.o" "$OUT/libregexp.o" "$OUT/libunicode.o" "$OUT/quickjs.o" \
	$(pkg-config --cflags --libs Qt6Core) -lm -ldl -lpthread -o "$OUT/test"
"$OUT/test"
