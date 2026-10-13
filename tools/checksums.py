#!/usr/bin/env python3
"""Verify IV50 frame/band checksums against decoded YVU9 frames.
usage: checksums.py file.avi decoded.yuv width height [nframes]
Assumes the basic profile (1 band per plane, no tiling)."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('h', os.path.join(here, 'iv5hdr.py'))
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)

def parse(p):
    r = h.BR(p); r.get(16); gf = r.get(8)
    gsz = r.get(16)
    r = h.BR(p); r.p = (2 + gsz) * 8
    ff = r.get(8); r.get(24) if ff & 1 else None
    fcs = r.get(16) if ff & 0x10 else None
    off = 2 + gsz
    # skip rest of picture header
    if ff & 0x40:
        if r.get(3) == 7:
            n = r.get(4); [r.get(4) for _ in range(n)]
    r.get(3); r.align(); off = r.p // 8
    bands = []
    for b in range(3):
        r = h.BR(p[off:]); fl = r.get(8); ds = r.get(24)
        if fl & 0x10:
            n = r.get(8); [r.get(8) for _ in range(2 * n)]
        if fl & 0x40: r.get(3)
        if fl & 0x80:
            if r.get(3) == 7:
                n = r.get(4); [r.get(4) for _ in range(n)]
        bands.append(r.get(16) if r.get(1) else None)
        off += ds
    return fcs, bands

avi, yuv, W, H = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
n = int(sys.argv[5]) if len(sys.argv) > 5 else 1 << 30
cw, ch = (W + 3) >> 2, (H + 3) >> 2
fs = W * H + 2 * cw * ch
raw = open(yuv, 'rb').read()
bad = 0; frames = h.avi_frames(avi)
for i, p in enumerate(frames[:n]):
    f = raw[i * fs:(i + 1) * fs]
    Y, U, V = f[:W*H], f[W*H:W*H+cw*ch], f[W*H+cw*ch:]
    calc = [sum(x - 128 for x in pl) & 0xffff for pl in (Y, V, U)]
    fcs, bcs = parse(p)
    ok = bcs == calc and fcs == sum(calc) & 0xffff
    bad += not ok
    if not ok:
        print(f"frame {i}: MISMATCH hdr frame={fcs} bands={bcs} calc={calc}")
print(f"{min(n, len(frames))} frames checked, {bad} mismatches")
