#!/usr/bin/env bash
# Registers a locally built DamZaGram in the desktop menu for the current
# user (no root, no build): .desktop entry, D-Bus service and icons.
#
#   tools/damza/install-desktop-entry.sh [path/to/DamZaGram]
#   tools/damza/install-desktop-entry.sh --uninstall
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
data="${XDG_DATA_HOME:-$HOME/.local/share}"
id=com.damzagram.desktop
sizes=(16 32 48 64 128 256 512)

refresh() {
	command -v update-desktop-database >/dev/null \
		&& update-desktop-database -q "$data/applications" || true
	command -v gtk-update-icon-cache >/dev/null \
		&& gtk-update-icon-cache -q -t "$data/icons/hicolor" || true
}

if [[ "${1:-}" == "--uninstall" ]]; then
	rm -f "$data/applications/$id.desktop" "$data/dbus-1/services/$id.service"
	for size in "${sizes[@]}"; do
		rm -f "$data/icons/hicolor/${size}x${size}/apps/$id.png"
	done
	rm -f "$data/icons/hicolor/symbolic/apps/$id-symbolic.svg" \
		"$data/icons/hicolor/symbolic/apps/$id-attention-symbolic.svg" \
		"$data/icons/hicolor/symbolic/apps/$id-mute-symbolic.svg"
	refresh
	echo "Removed $id"
	exit 0
fi

bin="$(realpath -m "${1:-$root/out/bin/DamZaGram}")"
if [[ ! -x "$bin" ]]; then
	echo "DamZaGram binary not found: $bin" >&2
	echo "Build it first or pass the path as the first argument." >&2
	exit 1
fi

mkdir -p "$data/applications" "$data/dbus-1/services"
sed \
	-e "s|^TryExec=.*|TryExec=$bin|" \
	-e "s|^Exec=env DESKTOPINTEGRATION=1 DamZaGram |Exec=env DESKTOPINTEGRATION=1 \"$bin\" |" \
	-e "s|^Exec=DamZaGram -quit|Exec=\"$bin\" -quit|" \
	"$root/lib/xdg/$id.desktop" > "$data/applications/$id.desktop"
sed "s|@CMAKE_INSTALL_FULL_BINDIR@/DamZaGram|$bin|" \
	"$root/lib/xdg/$id.service" > "$data/dbus-1/services/$id.service"

for size in "${sizes[@]}"; do
	dir="$data/icons/hicolor/${size}x${size}/apps"
	mkdir -p "$dir"
	cp "$root/Telegram/Resources/art/icon$size.png" "$dir/$id.png"
done
symbolic="$data/icons/hicolor/symbolic/apps"
mkdir -p "$symbolic"
icons="$root/Telegram/Resources/icons"
cp "$icons/tray_monochrome.svg" "$symbolic/$id-symbolic.svg"
cp "$icons/tray_monochrome_attention.svg" "$symbolic/$id-attention-symbolic.svg"
cp "$icons/tray_monochrome_mute.svg" "$symbolic/$id-mute-symbolic.svg"

refresh
echo "Installed $id -> $bin"
