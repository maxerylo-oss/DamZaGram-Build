#!/usr/bin/env bash
# Run the freshly built DamZaGram.
#   tools/damza/run-dev.sh            -> uses ../DamZaGram-data next to the repo if it exists,
#                                        otherwise the default data folder (~/.local/share/DamZaGramDesktop)
#   DZG_WORKDIR=/path tools/damza/run-dev.sh   -> a custom data folder
# Extra arguments are passed to the client.
set -e
root="$(cd "$(dirname "$0")/../.." && pwd)"
bin="$root/out/bin/DamZaGram"
[ -x "$bin" ] || { echo "Build first: cmake --build out --target Telegram"; exit 1; }
workdir="${DZG_WORKDIR:-$(dirname "$root")/DamZaGram-data}"
if [ -d "$workdir" ]; then
	exec "$bin" -workdir "$workdir" "$@"
fi
exec "$bin" "$@"
