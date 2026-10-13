#!/usr/bin/env python3
"""Insert a copy of the previous picture for every NULL frame (type 4), so that
FFmpeg's output (no picture for NULL frames) lines up with Intel's decoder
(which repeats the last picture).
usage: expand_null.py stream.avi ffmpeg.yuv410p out.yuv410p W H"""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('h', os.path.join(here, 'iv5hdr.py'))
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
W, H = int(sys.argv[4]), int(sys.argv[5])
fs = W * H + 2 * ((W + 3) // 4) * ((H + 3) // 4)
src = open(sys.argv[2], 'rb').read(); out = open(sys.argv[3], 'wb')
k = 0; last = None
for p in h.avi_frames(sys.argv[1]):
    if (p[0] >> 5) & 7 == 4 and last is not None:
        out.write(last); continue
    last = src[k * fs:(k + 1) * fs]; k += 1
    out.write(last)
