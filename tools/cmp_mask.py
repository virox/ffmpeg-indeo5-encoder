#!/usr/bin/env python3
"""Compare the transparency masks Intel's decoder returns (iv5ref ... I420:12 maskfile:
per frame u32 + 1 bpp rows, DWORD aligned, MSB first) with masks from iv5trans.py.
usage: cmp_mask.py intel.mask ours.mask W H   -> prints total mismatching pixels"""
import sys
import numpy as np
W, H = int(sys.argv[3]), int(sys.argv[4]); st = ((W + 31) // 32) * 4; fs = 4 + st * H
d = np.fromfile(sys.argv[1], np.uint8); m = np.fromfile(sys.argv[2], np.uint8).reshape(-1, H, W)
n = min(len(d) // fs, len(m)); bad = 0 if n == len(m) else -1
for i in range(n):
    bits = np.unpackbits(d[i*fs+4:(i+1)*fs].reshape(H, st), axis=1, bitorder='big')[:, :W]
    bad += int((bits != m[i]).sum())
print(f"{n} frames: mask mismatches {bad}")
