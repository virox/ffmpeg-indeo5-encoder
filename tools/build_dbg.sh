#!/bin/sh
# build_dbg.sh: build out/ffmpeg_dbg, the FFmpeg binary whose ivi.c is compiled
# with -DDEBUG (band checksum verification, "checksum mismatch" messages), used
# by regress.sh; then restore the normal build.
set -e
D=$(cd "$(dirname "$0")/.." && pwd); cd "$D/ffmpeg"
make -j16 ffmpeg >/dev/null
cmd=$(make -s -n -B libavcodec/ivi.o | grep -- '-o libavcodec/ivi.o' | head -1)
[ -n "$cmd" ] || { echo "cannot find the ivi.o compile command"; exit 1; }
eval "$cmd -DDEBUG"
make -j16 ffmpeg >/dev/null
cp ffmpeg "$D/out/ffmpeg_dbg"
touch libavcodec/ivi.c
make -j16 ffmpeg >/dev/null
echo "built $D/out/ffmpeg_dbg"
