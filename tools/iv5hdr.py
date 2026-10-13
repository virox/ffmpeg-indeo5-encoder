#!/usr/bin/env python3
"""Dump Indeo5 picture/GOP/band headers of frames in an AVI (debug aid)."""
import struct, sys

def avi_frames(path):
    d = open(path, 'rb').read()
    if path.endswith('.ivr'):          # iv5ref enc output: [u32 len][u32 flags][data]
        out, j = [], 0
        while j + 8 <= len(d):
            l = struct.unpack('<I', d[j:j+4])[0]
            out.append(d[j+8:j+8+l]); j += 8 + l
        return out
    i = d.find(b'movi')
    out = []
    j = i + 4
    while j + 8 <= len(d):
        tag = d[j:j+4]; sz = struct.unpack('<I', d[j+4:j+8])[0]
        if tag == b'LIST':
            j += 12; continue
        if tag[2:] in (b'dc', b'db'):
            out.append(d[j+8:j+8+sz])
        if tag == b'idx1': break
        j += 8 + sz + (sz & 1)
    return out

class BR:
    def __init__(s, b): s.b = b; s.p = 0
    def get(s, n):
        v = 0
        for k in range(n):
            byte = s.b[s.p >> 3]
            v |= ((byte >> (s.p & 7)) & 1) << k
            s.p += 1
        return v
    def align(s): s.p = (s.p + 7) & ~7

def dump(pkt, idx):
    r = BR(pkt)
    sc = r.get(5); ft = r.get(3); fn = r.get(8)
    print(f"frame {idx}: size={len(pkt)} start={sc:#x} type={ft} num={fn}")
    if ft == 0:
        gf = r.get(8); print(f"  gop_flags={gf:#04x}")
        if gf & 1: print("  gop_hdr_size", r.get(16))
        if gf & 0x20: print("  lock", hex(r.get(32)))
        if gf & 0x40: print("  tile", 64 << r.get(2))
        lb = r.get(2); cb = r.get(1); psi = r.get(4)
        print(f"  luma_levels={lb} chroma_levels={cb} pic_size_idx={psi}")
        if psi == 15: print("  h,w", r.get(13), r.get(13))
        for p in range(2):
            nb = (lb*3+1) if p == 0 else (cb*3+1)
            for b in range(nb):
                print(f"  p{p}b{b}: halfpel={r.get(1)} mbflag={r.get(1)} blkflag={r.get(1)} ext={r.get(1)} end={r.get(2)}")
        if gf & 8:
            print("  align3", r.get(3)); t = r.get(1)
            if t: print("  transp", hex(r.get(24)))
        r.align()
        unk = r.get(23); ext = r.get(1)
        print(f"  unk23={unk:#08x} ({unk:023b}) ext={ext}")
        if ext:
            while True:
                v = r.get(16); print("   ext word", hex(v))
                if not v & 0x8000: break
        r.align()
    if ft != 4:
        ff = r.get(8); print(f"  frame_flags={ff:#04x}")
        if ff & 1: print("  pic_hdr_size", r.get(24))
        if ff & 0x10: print("  checksum", hex(r.get(16)))
        if ff & 0x20: print("  EXT!")
        if ff & 0x40:
            sel = r.get(3); print("  mb huff sel", sel)
            if sel == 7:
                n = r.get(4); print("   rows", [r.get(4) for _ in range(n)])
        print("  unk3", r.get(3))
        r.align()
        bf = r.get(8); print(f"  band0 flags={bf:#04x} at byte {r.p//8 - 1}")
        if ff & 0x80: print("  band data_size", r.get(24))
        if bf & 0x10:
            n = r.get(8); print("  corr", [r.get(8) for _ in range(2*n)])
        if bf & 0x40: print("  rvmap", r.get(3))
        if bf & 0x80:
            sel = r.get(3); print("  blk huff sel", sel)
            if sel == 7:
                n = r.get(4); print("   rows", [r.get(4) for _ in range(n)])
        cs = r.get(1)
        if cs: print("  band checksum", hex(r.get(16)))
        print("  glob_quant", r.get(5))
        r.align()
        e = r.get(1); print("  tile0 empty", e)
        if not e:
            if r.get(1):
                l = r.get(8)
                if l == 255: l = r.get(24)
                print("  tile0 size", l)

if __name__ == '__main__':
    fr = avi_frames(sys.argv[1])
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    for i, f in enumerate(fr[:n]):
        if f: dump(f, i)
