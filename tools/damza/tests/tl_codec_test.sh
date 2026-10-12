#!/bin/sh
# Builds and runs tools/damza/tests/tl_codec_test.cpp against the TL codec
# sources only (QtCore, no client build needed).
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
SRC="$ROOT/Telegram/SourceFiles"
OUT=${TMPDIR:-/tmp}/dzg_tl_codec_test
g++ -std=c++20 -O1 -fPIC -Wall -Wextra -I"$SRC" \
	"$SRC/damza/plugins/tl/tl_schema.cpp" \
	"$SRC/damza/plugins/tl/tl_codec.cpp" \
	"$SRC/damza/plugins/tl/tl_schema_data.cpp" \
	"$ROOT/tools/damza/tests/tl_codec_test.cpp" \
	$(pkg-config --cflags --libs Qt6Core) -o "$OUT"
"$OUT"
