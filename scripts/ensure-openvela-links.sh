#!/usr/bin/env bash
# Keep the generated local link aligned with the contest manifest.

set -euo pipefail

if [ "$#" -ne 1 ]; then
  echo "usage: $0 <openvela-root>" >&2
  exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
CONTEST_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
OPENVELA_ROOT="$(cd "$1" && pwd)"
SOURCE="$CONTEST_ROOT/app/velaguard"
DESTINATION="$OPENVELA_ROOT/packages/demos/contest2026_004_hello_app"

if [ ! -d "$SOURCE" ]; then
  echo "Contest app source directory is missing: $SOURCE" >&2
  exit 1
fi

mkdir -p "$(dirname "$DESTINATION")"

if [ -e "$DESTINATION" ] && [ ! -L "$DESTINATION" ]; then
  echo "Refusing to replace non-symlink path: $DESTINATION" >&2
  exit 1
fi

if [ -L "$DESTINATION" ] &&
   [ "$(readlink -f "$DESTINATION")" = "$(readlink -f "$SOURCE")" ]; then
  echo "Contest app openvela link is ready."
  exit 0
fi

ln -sfn "$SOURCE" "$DESTINATION"
echo "Linked contest app: $DESTINATION -> $SOURCE"
