#!/bin/sh
# apply.sh [FFMPEG_DIR]: add the Indeo 5 encoder to an FFmpeg checkout. Safe to re-run.
#  - patches/*.patch  decoder fixes (made against BASE_COMMIT, may need rebasing on newer FFmpeg)
#  - src/             encoder source, copied in
#  - registration     inserted after the indeo5 decoder lines, so it survives upstream churn
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
F=${1:-ffmpeg}
. "$R/scripts/base-commit"
[ -d "$F/libavcodec" ] || { echo "usage: $0 FFMPEG_DIR" >&2; exit 2; }
cur=$(git -C "$F" rev-parse HEAD 2>/dev/null || true)
[ "$cur" = "$BASE_COMMIT" ] || echo "warning: $F is at ${cur:-unknown}, patches are made against $BASE_COMMIT" >&2

for p in "$R"/patches/*.patch; do
  if git -C "$F" apply --reverse --check "$p" 2>/dev/null; then echo "already applied: ${p##*/}"
  else git -C "$F" apply "$p"; fi
done
cp "$R/src/libavcodec/indeo5enc.c" "$F/libavcodec/indeo5enc.c"

# insert_after FILE ANCHOR_REGEX NEW_LINE: add NEW_LINE below the anchor unless present
insert_after() {
  grep -qxF "$3" "$1" && return 0
  grep -q "$2" "$1" || { echo "error: anchor '$2' not found in $1" >&2; exit 1; }
  sed -i "/$2/a\\
$3" "$1"
}
insert_after "$F/libavcodec/allcodecs.c" '^extern const FFCodec ff_indeo5_decoder;' \
  'extern const FFCodec ff_indeo5_encoder;'
insert_after "$F/libavcodec/Makefile" '^OBJS-\$(CONFIG_INDEO5_DECODER)' \
  'OBJS-$(CONFIG_INDEO5_ENCODER)          += indeo5enc.o ivi.o'
insert_after "$F/configure" '^indeo5_decoder_select=' 'indeo5_encoder_select="ividsp"'
echo "applied to $F"
