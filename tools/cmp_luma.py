#!/usr/bin/env python3
"""Compare luma of Intel I420 output vs FFmpeg yuv410p output.
usage: cmp_luma.py intel.i420 ffmpeg.yuv410p W H"""
import sys
a = open(sys.argv[1], 'rb').read(); b = open(sys.argv[2], 'rb').read()
W, H = int(sys.argv[3]), int(sys.argv[4])
fa = W * H * 3 // 2; fb = W * H + 2 * ((W + 3) // 4) * ((H + 3) // 4)
n = min(len(a) // fa, len(b) // fb)
tot = exact = 0; hist = {}
worst = (0, -1)
for i in range(n):
    A = a[i*fa:i*fa+W*H]; B = b[i*fb:i*fb+W*H]
    d = [x - y for x, y in zip(A, B)]
    e = sum(1 for v in d if v == 0)
    exact += e; tot += len(d)
    for v in d: hist[v] = hist.get(v, 0) + 1
    m = max(abs(v) for v in d)
    if m > worst[0]: worst = (m, i)
print(f"{n} frames: {100*exact/tot:.3f}% luma samples identical, max |diff| {worst[0]} (frame {worst[1]})")
print("diff histogram:", sorted(hist.items(), key=lambda kv: -kv[1])[:9])
