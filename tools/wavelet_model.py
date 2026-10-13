#!/usr/bin/env python3
"""Fit Intel's 4-band wavelet synthesis: compares models against real decoder output.
usage: wavelet_model.py bands.raw intel.i420 ffmpeg.yuv410p W H"""
import sys
import numpy as np

W, H = int(sys.argv[4]), int(sys.argv[5])
mw, mh = (W + 1) // 2, (H + 1) // 2
cw, ch = (W + 3) // 4, (H + 3) // 4
bands = np.fromfile(sys.argv[1], dtype='<i2')
per = 4 * mw * mh + 2 * cw * ch
nf = bands.size // per
intel = np.fromfile(sys.argv[2], dtype=np.uint8)
ff = np.fromfile(sys.argv[3], dtype=np.uint8)

def get(i):
    f = bands[i * per:(i + 1) * per]
    b = [f[k * mw * mh:(k + 1) * mw * mh].reshape(mh, mw).astype(np.int64) for k in range(4)]
    yi = intel[i * W * H * 3 // 2:i * W * H * 3 // 2 + W * H].reshape(H, W).astype(int)
    fs = W * H + 2 * cw * ch
    yf = ff[i * fs:i * fs + W * H].reshape(H, W).astype(int)
    return b, yi, yf

def nb(a, axis, shift):
    """neighbour with replicate edges: shift=+1 -> a[k+1], -1 -> a[k-1]"""
    n = a.shape[axis]
    idx = np.clip(np.arange(n) + shift, 0, n - 1)
    return np.take(a, idx, axis=axis)

def synth1d(L, Hh, axis):
    """returns (even, odd) outputs * 8 along axis"""
    ev = 4 * L + 2 * (nb(Hh, axis, -1) + Hh)
    od = 2 * (L + nb(L, axis, 1)) + nb(Hh, axis, -1) - 6 * Hh + nb(Hh, axis, 1)
    return ev, od

def recompose(b, wrap16=False):
    LL, HL, LH, HH = b   # HL: vert high/horiz low (band1), LH: vert low/horiz high (band2)
    # horizontal synthesis of vertical-low and vertical-high parts
    lo_e, lo_o = synth1d(LL, LH, 1)
    hi_e, hi_o = synth1d(HL, HH, 1)
    out = np.zeros((2 * mh, 2 * mw), dtype=np.int64)
    for hx, (l, h) in enumerate(((lo_e, hi_e), (lo_o, hi_o))):
        ve, vo = synth1d(l, h, 0)
        out[0::2, hx::2] = ve
        out[1::2, hx::2] = vo
    if wrap16:
        out = ((out + 32768) & 0xffff) - 32768
    return np.clip((out >> 6) + 128, 0, 255)[:H, :W]

for i in range(min(nf, int(__import__("os").environ.get("NF", "99")))):
    b, yi, yf = get(i)
    r = recompose(b)
    r16 = recompose(b, True)
    print(f"frame {i}: model==ffmpeg {np.mean(r == yf)*100:.2f}%  model==intel {np.mean(r == yi)*100:.2f}%  "
          f"wrap16==intel {np.mean(r16 == yi)*100:.2f}%  max|ffmpeg-intel| {np.abs(yf-yi).max()}")
    if i == 0:
        d = yi - yf
        ys, xs = np.nonzero(d)
        print("  diff rows parity:", np.bincount(ys % 2), "cols parity:", np.bincount(xs % 2))
        print("  diff per column (first 12 cols):", [int(np.count_nonzero(d[:, x])) for x in range(12)])
        print("  diff per column (last 6 cols):", [int(np.count_nonzero(d[:, x])) for x in range(W-6, W)])
        print("  diff per row (first 6 / last 6):", [int(np.count_nonzero(d[y])) for y in range(6)], [int(np.count_nonzero(d[y])) for y in range(H-6, H)])
        print("  values of diff:", np.unique(d, return_counts=True))
