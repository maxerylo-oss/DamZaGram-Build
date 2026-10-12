#!/usr/bin/env bash
# Builds an Arch Linux package from the DamZaGram binary in out/.
#   tools/damza/make-arch-package.sh            -> dist/damzagram-<ver>-<rel>-x86_64.pkg.tar.zst
#   DZG_BUILD_DIR=/path/to/build tools/damza/make-arch-package.sh
# Install: sudo pacman -U dist/damzagram-*.pkg.tar.zst
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build="${DZG_BUILD_DIR:-$root/out}"
if [[ ! -x "$build/bin/DamZaGram" ]]; then
	echo "Build the client first: cmake --build out --target Telegram" >&2
	exit 1
fi
mkdir -p "$root/dist"
cd "$root/packaging/arch"
DZG_BUILD_DIR="$build" \
BUILDDIR="$build/pkg-build" \
PKGDEST="$root/dist" \
SRCDEST="$build/pkg-build" \
	makepkg -f --nodeps --noconfirm
ls -lh "$root/dist"/damzagram-*.pkg.tar.zst
