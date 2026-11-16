#!/bin/sh
# One-command Linux install of Wololoader:
#
#   curl -fsSL https://wololoader.vercel.app/install.sh | sh
#   curl -fsSL https://wololoader.vercel.app/install.sh | sh -s -- --no-examples "/path/to/game"
#
# Downloads the latest wololoader-linux-setup.sh from the GitHub releases and runs it with the given
# options (see `sh wololoader-linux-setup.sh --help`). The setup reads its files from its own file,
# so it is saved first instead of piped; questions are read from the terminal.
set -eu
URL="https://github.com/Vitrie32/wololoader/releases/latest/download/wololoader-linux-setup.sh"
TMP=$(mktemp "${TMPDIR:-/tmp}/wololoader-setup.XXXXXX")
trap 'rm -f "$TMP"' EXIT
echo "Downloading $URL"
if command -v curl >/dev/null 2>&1; then
    curl -fL --progress-bar -o "$TMP" "$URL"
elif command -v wget >/dev/null 2>&1; then
    wget -q --show-progress -O "$TMP" "$URL"
else
    echo "Needs curl or wget." >&2
    exit 1
fi
if [ -r /dev/tty ] && { true </dev/tty; } 2>/dev/null; then
    sh "$TMP" "$@" </dev/tty
else
    sh "$TMP" "$@"
fi
