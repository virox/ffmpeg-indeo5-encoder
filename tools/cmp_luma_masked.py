#!/usr/bin/env python3
"""Like cmp_luma.py but only over opaque pixels (mask: 1 byte/pixel from iv5trans.py).
usage: cmp_luma_masked.py intel.i420 ffmpeg.yuv410p mask W H"""
import sys
import numpy as np
W, H = int(sys.argv[4]), int(sys.argv[5])
a = np.fromfile(sys.argv[1], np.uint8); b = np.fromfile(sys.argv[2], np.uint8); m = np.fromfile(sys.argv[3], np.uint8)
fa, fb = W * H * 3 // 2, W * H + 2 * ((W + 3) // 4) * ((H + 3) // 4)
n = min(a.size // fa, b.size // fb, m.size // (W * H)); worst = 0
for i in range(n):
    ya = a[i*fa:i*fa+W*H].astype(int); yb = b[i*fb:i*fb+W*H].astype(int); mm = m[i*W*H:(i+1)*W*H] > 0
    if mm.any():
        worst = max(worst, int(np.abs(ya[mm] - yb[mm]).max()))
print(f"{n} frames: opaque-pixel max |diff| {worst}")
