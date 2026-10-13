#!/bin/sh
# export.sh [FFMPEG_DIR]: regenerate src/ and patches/ (decoder files only) from a modified FFmpeg checkout.
set -e
R=$(cd "$(dirname "$0")/.." && pwd)
F=${1:-$R/ffmpeg}
cp "$F/libavcodec/indeo5enc.c" "$R/src/libavcodec/indeo5enc.c"
git -C "$F" diff -- libavcodec/indeo5.c libavcodec/ivi.c libavcodec/ivi.h libavcodec/ivi_dsp.c libavcodec/ivi_dsp.h \
  > "$R/patches/0001-indeo-decoder-synthesis-access-key-transparency.patch"
echo "exported from $F"
