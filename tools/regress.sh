#!/bin/sh
# regress.sh: encode test clips with many option combinations and verify
# FFmpeg decode/checksums and Intel's selected path (exact luma).
# IV5_SYNTHESIS=scalar selects the scalar path; default is mmx.
D=$(cd "$(dirname "$0")/.." && pwd); T=${TMPDIR:-/tmp}/iv5regress.$$; mkdir -p "$T"
F=$D/ffmpeg/ffmpeg; fail=0
S=${IV5_SYNTHESIS:-mmx}
case "$S" in mmx) CPU=1;; scalar) CPU=0;; *) echo "invalid IV5_SYNTHESIS: $S"; exit 2;; esac
echo "synthesis: $S"
run() {
  src="$1"; w=$2; h=$3; shift 3
  "$F" -v error -y $src -c:v indeo5 "$@" "$T/o.avi" 2>"$T/enc.err" || { echo "ENCODE FAIL [$*]"; fail=1; return; }
  er=$("$F" -v error -scalable_synthesis "$S" -i "$T/o.avi" -f null - 2>&1 | wc -l)
  cs=$("$D/out/ffmpeg_dbg" -v debug -scalable_synthesis "$S" -i "$T/o.avi" -f null - 2>&1 | grep -c "checksum mismatch")
  python3 -I "$D/tools/avi2frames.py" "$T/o.avi" "$T/o.bin" >/dev/null
  ie=$("$D/tools/iv5ref/iv5ref" "$D/IR50_32.DLL" "$T/o.bin" $w $h "$T/o.i420" I420:12 mmx=$CPU 2>&1 | grep -c ICM_DECOMPRESS)
  # FFmpeg outputs no picture for NULL frames, Intel repeats the last one
  "$F" -v error -y -scalable_synthesis "$S" -i "$T/o.avi" -fps_mode passthrough -f rawvideo -pix_fmt yuv410p "$T/o0.yuv"
  python3 -I "$D/tools/expand_null.py" "$T/o.avi" "$T/o0.yuv" "$T/o.yuv" $w $h
  if true; then
    case "$*" in
    *-transparency*)   # Intel does not draw transparent pixels: compare opaque ones
      python3 -I "$D/tools/iv5trans.py" "$T/o.avi" $w $h 100000 "$T/o.mask" >/dev/null
      "$D/tools/iv5ref/iv5ref" "$D/IR50_32.DLL" "$T/o.bin" $w $h "$T/o.i420" I420:12 "$T/o.imask" mmx=$CPU >/dev/null 2>&1
      mk=$(python3 -I "$D/tools/cmp_mask.py" "$T/o.imask" "$T/o.mask" $w $h | grep -o "[-0-9]*$")
      [ "$mk" = 0 ] || { echo "     Intel decoder mask differs: $mk"; ie=mask; }
      ex=$(python3 -I "$D/tools/cmp_luma_masked.py" "$T/o.i420" "$T/o.yuv" "$T/o.mask" $w $h | grep -o "[0-9]*$");;
    *)
      ex=$(python3 -I "$D/tools/cmp_luma.py" "$T/o.i420" "$T/o.yuv" $w $h | head -1 | grep -o "max |diff| [0-9]*" | grep -o "[0-9]*$");;
    esac
  fi
  st=OK; { [ "$er" != 0 ] || [ "$cs" != 0 ] || [ "$ie" != 0 ]; } && { st=FAIL; fail=1; }
  case "$ex" in 0) ;; *) st=FAIL; fail=1;; esac
  printf '%-4s %-9s %-60s size=%-8s ff_err=%s csum=%s intel_err=%s maxdiff=%s\n' $st "${w}x$h" "$*" $(stat -c%s "$T/o.avi") $er $cs $ie "$ex"
}
S320="-f lavfi -i testsrc2=size=320x240:rate=15,scroll=h=0.011:v=0.004 -t 3"
S640="-f lavfi -i testsrc2=size=640x480:rate=15,scroll=h=0.007 -t 2"
SST="-f lavfi -i testsrc2=size=320x240:rate=15,scroll=h=0.01,fps=5,fps=15 -t 3"
for lb in 1 4; do
  for opts in "-g 1" "" "-droppable 1" "-quant 0" "-quant 23" "-quality 8500" "-tables 0" "-tables 1" \
              "-aq_strength 1.5" "-b:v 400k" "-b:v 1500k -aq_strength 1" "-tile_size 64" "-tile_size 128 -b:v 600k" \
              "-tile_size 256 -droppable 1 -aq_strength 1" "-quick 1" "-me_range 60" "-checksum 0" "-g 5 -droppable 1 -tile_size 64"; do
    run "$S320" 320 240 -luma_bands $lb $opts
  done
done
# Scalable HH compatibility switch and diagonal detail at a partial MB edge.
run "$S320" 320 240 -luma_bands 4 -hh_band 0
run "$S320" 320 240 -luma_bands 4 -hh_band 0 -quick 1 -checksum 1
run "-f lavfi -i testsrc2=size=100x60:rate=15,scroll=h=0.02 -t 1" 100 60 -luma_bands 4 -hh_band 1 -tile_size 64 -quant 0
# Explicit RDO strengths, including the original greedy path and QC override.
for lb in 1 4; do
  run "$S320" 320 240 -luma_bands $lb -rdo 0
  run "$S320" 320 240 -luma_bands $lb -rdo 0.1 -tile_size 64 -aq_strength 1
  run "$S320" 320 240 -luma_bands $lb -rdo 10 -droppable 1 -b:v 400k
  run "$S320" 320 240 -luma_bands $lb -quick 1 -rdo 1 -checksum 1 -tile_size 64
done
# Partial MBs and the 8x8 scalable QC layout exercise the mode-cost bounds.
for lb in 1 4; do
  run "-f lavfi -i testsrc2=size=100x60:rate=15,scroll=h=0.02 -t 1" 100 60 -luma_bands $lb -quick 1 -rdo 1
done
# QC LL guard samples beyond coded MBs (176 / 2 is not aligned to 16).
S176="-f lavfi -i testsrc2=size=176x144:rate=15,scroll=h=0.02 -t 2"
run "$S176" 176 144 -luma_bands 4 -quick 1 -checksum 1
run "$S176" 176 144 -luma_bands 4 -quick 1 -checksum 1 -tile_size 64 -droppable 1 -hh_band 0
run "-f lavfi -i testsrc2=size=176x144:rate=15,fps=5,fps=15 -t 2" 176 144 -luma_bands 4 -quick 1 -null_frames 1 -tile_size 64
run "$S640" 640 480 -luma_bands 1
# Quick Compress profile with checksums (they are off by default, as in IR50_QC)
run "$S320" 320 240 -quick 1 -checksum 1 -droppable 1 -quality 8500
run "$S320" 320 240 -quick 1 -checksum 1 -luma_bands 4 -droppable 1 -tile_size 64
run "$S640" 640 480 -luma_bands 4 -tile_size 64
run "$SST" 320 240 -null_frames 1
run "$SST" 320 240 -null_frames 1 -quick 1 -droppable 1 -tile_size 64
SSN="-f lavfi -i testsrc2=size=320x240:rate=15,scroll=h=0.01,fps=5,fps=15,trim=start_frame=1,setpts=PTS-STARTPTS -t 3"
run "$SSN" 320 240 -null_frames 1 -luma_bands 4
run "$SSN" 320 240 -null_frames 1 -luma_bands 4 -droppable 1 -tile_size 64 -b:v 500k
run "-f lavfi -i testsrc2=size=176x144:rate=15,scroll=h=0.02 -t 3" 176 144 -aq_strength 1 -tile_size 64
run "-f lavfi -i mandelbrot=size=352x288:rate=15,rotate=a=t*2 -t 3" 352 288 -b:v 800k
SA="-f lavfi -i testsrc2=s=320x240:r=15:d=2,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lt(hypot(X-160-60*sin(T*2),Y-120),50),255,0)'"
run "$SA" 320 240 -transparency 1
run "$SA" 320 240 -transparency 1 -b:v 500k -droppable 1
run "$SA" 320 240 -transparency alpha -luma_bands 4 -droppable 1
# colour keying (bgr24): blue backdrop, frame 0 shows only the backdrop
SK="-f lavfi -i color=c=0x1020E0:s=320x240:r=15:d=2,format=rgb24,noise=alls=8:allf=t[bg];testsrc2=s=160x160:r=15:d=2,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)*0.3':a='if(lt(hypot(X-80,Y-80),70),255,0)'[fg];[bg][fg]overlay=x='80+60*sin(t*2)':y=40:enable='gte(n,1)',format=bgr24[out0]"
run "$SK" 320 240 -transparency first_frame
run "$SK" 320 240 -transparency first_frame -droppable 1 -b:v 400k
run "$SK" 320 240 -transparency first_frame -luma_bands 4
run "$SK" 320 240 -transparency range -transparency_lo 0x000060 -transparency_hi 0x5050FF
run "$SK" 320 240 -transparency range -transparency_lo 0x000060 -transparency_hi 0x5050FF -luma_bands 4 -aq_strength 1
# transparency with tiling, including partial tiles and pictures narrower than a tile
sa() { echo "-f lavfi -i testsrc2=s=$1x$2:r=15:d=2,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lt(hypot(X-W/2-W/5*sin(T*2),Y-H/2),H/4),255,0)'"; }
run "$(sa 176 144)" 176 144 -transparency alpha -quick 1 -luma_bands 4 -tile_size 64 -checksum 1
run "$(sa 320 240)" 320 240 -transparency alpha -tile_size 64
run "$(sa 336 200)" 336 200 -transparency alpha -tile_size 64 -droppable 1
run "$(sa 352 288)" 352 288 -transparency alpha -tile_size 128 -luma_bands 4
run "$(sa 160 120)" 160 120 -transparency alpha -tile_size 256 -luma_bands 4 -droppable 1
run "$SK" 320 240 -transparency first_frame -tile_size 128 -luma_bands 4 -droppable 1
run "$SK" 320 240 -transparency range -transparency_lo 0x000060 -transparency_hi 0x5050FF -tile_size 64 -b:v 400k
# access key: Intel decoder needs the key; FFmpeg uses -access_key
akey() {
  "$F" -v error -y $S320 -c:v indeo5 "$@" "$T/k.avi" || { echo "ENCODE FAIL [$*]"; fail=1; return; }
  k=$(echo "$*" | sed 's/.*-access_key \([^ ]*\).*/\1/')
  er=$("$F" -v error -access_key $k -scalable_synthesis "$S" -i "$T/k.avi" -f null - 2>&1 | wc -l)
  python3 -I "$D/tools/avi2frames.py" "$T/k.avi" "$T/k.bin" >/dev/null
  ie=$("$D/tools/iv5ref/iv5ref" "$D/IR50_32.DLL" "$T/k.bin" 320 240 "$T/k.i420" I420:12 key=$k mmx=$CPU 2>&1 | grep -c ICM_DECOMPRESS)
  "$F" -v error -y -access_key $k -scalable_synthesis "$S" -i "$T/k.avi" -f rawvideo -pix_fmt yuv410p "$T/k.yuv"
  ex=$(python3 -I "$D/tools/cmp_luma.py" "$T/k.i420" "$T/k.yuv" 320 240 | head -1 | grep -o "max |diff| [0-9]*" | grep -o "[0-9]*$")
  st=OK; { [ "$er" != 0 ] || [ "$ie" != 0 ] || [ "$ex" != 0 ]; } && { st=FAIL; fail=1; }
  printf '%-4s %-9s %-60s ff_err=%s intel_err=%s maxdiff=%s\n' $st 320x240 "$*" $er $ie "$ex"
}
akey -access_key 0x12345678
akey -luma_bands 4 -access_key 0
akey -access_key 4294967295 -tile_size 64 -b:v 500k
rm -rf "$T"; echo "overall: $([ $fail = 0 ] && echo PASS || echo FAIL)"

exit "$fail"
