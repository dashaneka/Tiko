#!/usr/bin/env bash
set -euo pipefail

APP_ID="io.github.rajnish.Tiko"
BIN_DIR="${HOME}/.local/bin"
APPS_DIR="${HOME}/.local/share/applications"
ICON_DIR="${HOME}/.local/share/icons/hicolor/scalable/apps"

cd "$(dirname "$0")"

make clean && make

mkdir -p "${BIN_DIR}" "${APPS_DIR}" "${ICON_DIR}"
install -m 755 tiko "${BIN_DIR}/tiko"
install -m 644 "data/${APP_ID}.svg" "${ICON_DIR}/${APP_ID}.svg"
sed "s|^Exec=tiko|Exec=${BIN_DIR}/tiko|" "data/${APP_ID}.desktop" > "${APPS_DIR}/${APP_ID}.desktop"
chmod 644 "${APPS_DIR}/${APP_ID}.desktop"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "${APPS_DIR}" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f -t "${HOME}/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi

echo "Installed Tiko to ${BIN_DIR}/tiko"
