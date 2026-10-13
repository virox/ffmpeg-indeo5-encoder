#!/usr/bin/env python3
"""Wrap iv5ref encoder output (.ivr: [u32 len][u32 flags][data]) into an IV50 AVI.
usage: ivr2avi.py in.ivr out.avi width height [fps]"""
import struct, sys

def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')

def lst(tag, data):
    return b'LIST' + struct.pack('<I', len(data) + 4) + tag + data

src, dst, w, h = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
fps = int(sys.argv[5]) if len(sys.argv) > 5 else 15
d = open(src, 'rb').read(); frames = []; j = 0
while j + 8 <= len(d):
    l, fl = struct.unpack_from('<II', d, j); frames.append((d[j + 8:j + 8 + l], fl)); j += 8 + l
n = len(frames); maxs = max(len(f) for f, _ in frames)
avih = struct.pack('<14I', 1000000 // fps, 0, 0, 0x10, n, 0, 1, maxs, w, h, 0, 0, 0, 0)
strh = struct.pack('<4s4sIHHIIIIIIIIhhhh', b'vids', b'IV50', 0, 0, 0, 0, 1, fps, 0, n, maxs, 0xffffffff, 0, 0, 0, w, h)
strf = struct.pack('<IiiHH4sIiiII', 40, w, h, 1, 24, b'IV50', w * h * 3, 0, 0, 0, 0)
hdrl = lst(b'hdrl', chunk(b'avih', avih) + lst(b'strl', chunk(b'strh', strh) + chunk(b'strf', strf)))
movi = b''; idx = b''; off = 4
for f, fl in frames:
    c = chunk(b'00dc', f)
    idx += struct.pack('<4sIII', b'00dc', 0x10 if fl & 0x10 else 0, off, len(f))
    movi += c; off += len(c)
body = b'AVI ' + hdrl + lst(b'movi', movi) + chunk(b'idx1', idx)
open(dst, 'wb').write(b'RIFF' + struct.pack('<I', len(body)) + body)
print(n, 'frames ->', dst)
