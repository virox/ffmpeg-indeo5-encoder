#!/bin/sh
# rd_point.sh name file.(ivr|avi) ref.yuv W H fps : bitrate + Intel-decoder luma PSNR + frame types
D=$(cd "$(dirname "$0")/.." && pwd); n=$1; f=$2; ref=$3; w=$4; h=$5; fps=$6
T=$(mktemp -d)
python3 -I "$D/tools/avi2frames.py" "$f" "$T/f.bin" >/dev/null 2>&1 || true
if [ "${f##*.}" = ivr ]; then python3 -I -c "
import struct,sys
d=open('$f','rb').read(); j=0; o=open('$T/f.bin','wb')
while j+8<=len(d):
    l,_=struct.unpack_from('<II',d,j); o.write(struct.pack('<I',l)+d[j+8:j+8+l]); j+=8+l"; fi
kb=$(python3 -I -c "
import struct; d=open('$T/f.bin','rb').read(); j=0; t=0; n=0
while j+4<=len(d):
    l=struct.unpack_from('<I',d,j)[0]; t+=l; n+=1; j+=4+l
print(round(t*8*$fps/n/1000))")
err=$("$D/tools/iv5ref/iv5ref" "$D/IR50_32.DLL" "$T/f.bin" $w $h "$T/o.i420" I420:12 2>&1 | grep -c ICM_DECOMPRESS)
ps=$(python3 -I "$D/tools/psnr_intel.py" "$T/o.i420" "$ref" $w $h)
ty=$(python3 -I -c "
import importlib.util,collections; s=importlib.util.spec_from_file_location('s','$D/tools/iv5stat.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m)
fr=m.h.avi_frames('$f'); st={}; print(dict(collections.Counter(m.parse(p,st)['type'] for p in fr if p)))")
printf '%-26s %5s kbit/s  %s  errors=%s  %s\n' "$n" "$kb" "$ps" "$err" "$ty"
rm -rf "$T"
