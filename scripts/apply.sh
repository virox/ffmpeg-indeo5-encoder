#!/bin/sh
# apply.sh [FFMPEG_DIR]: add the Indeo 5 encoder to an FFmpeg checkout at BASE_COMMIT.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
F=${1:-ffmpeg}
. "$R/scripts/base-commit"
[ -d "$F/libavcodec" ] || { echo "usage: $0 FFMPEG_DIR" >&2; exit 2; }
cur=$(git -C "$F" rev-parse HEAD 2>/dev/null || true)
[ "$cur" = "$BASE_COMMIT" ] || echo "warning: $F is at ${cur:-unknown}, patches are made against $BASE_COMMIT" >&2
for p in "$R"/patches/*.patch; do git -C "$F" apply "$p"; done
cp "$R/src/libavcodec/indeo5enc.c" "$F/libavcodec/indeo5enc.c"
echo "applied to $F"
