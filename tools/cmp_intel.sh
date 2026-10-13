#!/bin/sh
# cmp_intel.sh file.avi W H nframes : decode with Intel DLL (via iv5ref) and FFmpeg, report PSNR
set -e
D=$(dirname "$0")
T=${TMPDIR:-/tmp}/cmp_intel.$$
mkdir -p "$T"
python3 -I "$D/avi2frames.py" "$1" "$T/f.bin" "$4" >/dev/null
"$D/iv5ref/iv5ref" "$D/../IR50_32.DLL" "$T/f.bin" "$2" "$3" "$T/intel.rgb" rgb 2>&1 | grep -v "decoded" || true
"$D/../ffmpeg/ffmpeg" -v error -i "$T/intel.rgb" -f rawvideo -pix_fmt bgr24 -s "$2x$3" -r 1 -i - </dev/null 2>/dev/null || true
"$D/../ffmpeg/ffmpeg" -hide_banner -f rawvideo -pix_fmt bgr24 -s "$2x$3" -r 15 -i "$T/intel.rgb" \
    -r 15 -i "$1" -frames:v "$4" \
    -lavfi "[0]vflip,format=yuv410p,setpts=N[a];[1]format=yuv410p,setpts=N[b];[a][b]psnr" -f null - 2>&1 | grep -o "PSNR.*"
"$D/../ffmpeg/ffmpeg" -v error -y -f rawvideo -pix_fmt bgr24 -s "$2x$3" -i "$T/intel.rgb" -vf vflip -frames:v 1 -update 1 "${5:-$T}/intel_frame.png" 2>/dev/null || true
rm -rf "$T"
