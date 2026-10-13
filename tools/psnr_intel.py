#!/usr/bin/env python3
"""Luma PSNR of Intel I420 decoder output vs a yuv410p reference. usage: psnr_intel.py intel.i420 ref.yuv W H"""
import sys, numpy as np
W, H = int(sys.argv[3]), int(sys.argv[4])
a = np.fromfile(sys.argv[1], np.uint8); b = np.fromfile(sys.argv[2], np.uint8)
fa, fb = W * H * 3 // 2, W * H + 2 * ((W + 3) // 4) * ((H + 3) // 4)
n = min(a.size // fa, b.size // fb); mse = []
for i in range(n):
    d = a[i*fa:i*fa+W*H].astype(np.int32) - b[i*fb:i*fb+W*H].astype(np.int32)
    mse.append(np.mean(d * d))
mse = np.array(mse)
print(f"{n} frames  Y-PSNR avg {np.mean(10*np.log10(255**2/np.maximum(mse,1e-9))):.2f} dB  worst frame {np.min(10*np.log10(255**2/np.maximum(mse,1e-9))):.2f} dB")
