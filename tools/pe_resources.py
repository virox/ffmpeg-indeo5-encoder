#!/usr/bin/env python3
"""Dump string tables and dialog templates (with control texts/IDs) from a PE file."""
import struct, sys

d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
opt = pe + 24
rsrc_rva, rsrc_size = struct.unpack_from('<II', d, opt + 96 + 2 * 8)
secs = []
for i in range(nsec):
    s = opt + struct.unpack_from('<H', d, pe + 20)[0] + 40 * i
    va, vs = struct.unpack_from('<II', d, s + 12)[0], 0
    vsize, va, rsize, roff = struct.unpack_from('<IIII', d, s + 8)
    secs.append((va, vsize, roff))
def off(rva):
    for va, vs, ro in secs:
        if va <= rva < va + vs: return rva - va + ro
    raise ValueError(hex(rva))
base = off(rsrc_rva)
TYPES = {2: 'BITMAP', 4: 'MENU', 5: 'DIALOG', 6: 'STRING', 16: 'VERSION', 3: 'ICON', 14: 'GROUP_ICON'}

def name(o):
    l = struct.unpack_from('<H', d, base + o)[0]
    return d[base + o + 2:base + o + 2 + 2 * l].decode('utf-16le')

def walk(o, depth, path, out):
    nn, ni = struct.unpack_from('<HH', d, base + o + 12)
    for k in range(nn + ni):
        nid, sub = struct.unpack_from('<II', d, base + o + 16 + 8 * k)
        key = name(nid & 0x7fffffff) if nid & 0x80000000 else nid
        if sub & 0x80000000:
            walk(sub & 0x7fffffff, depth + 1, path + [key], out)
        else:
            rva, size = struct.unpack_from('<II', d, base + sub)
            out.append((path + [key], d[off(rva):off(rva) + size]))

res = []
walk(0, 0, [], res)

def sz(b, p):
    """read sz_Or_Ord at p, returns (value, newp)"""
    w = struct.unpack_from('<H', b, p)[0]
    if w == 0xffff: return ('#%d' % struct.unpack_from('<H', b, p + 2)[0], p + 4)
    e = p
    while struct.unpack_from('<H', b, e)[0]: e += 2
    return (b[p:e].decode('utf-16le'), e + 2)

def dialog(b):
    out = []
    if struct.unpack_from('<HH', b, 0) == (1, 0xffff):  # DLGTEMPLATEEX
        helpid, exst, style, n, x, y, cx, cy = struct.unpack_from('<IIIHhhhh', b, 4)
        p = 26
        menu, p = sz(b, p); cls, p = sz(b, p); title, p = sz(b, p)
        if style & 0x40:
            p += 6; face, p = sz(b, p)
        out.append(f'  DIALOGEX "{title}" {cx}x{cy}')
        for i in range(n):
            p = (p + 3) & ~3
            hid, exs, st, x, y, cx, cy, cid = struct.unpack_from('<IIIhhhhI', b, p); p += 24
            c, p = sz(b, p); t, p = sz(b, p)
            ex = struct.unpack_from('<H', b, p)[0]; p += 2 + ex
            out.append(f'    ctl id={cid:<5} class={c:<14} text="{t}"')
    else:
        style, exst, n, x, y, cx, cy = struct.unpack_from('<IIHhhhh', b, 0)
        p = 18
        menu, p = sz(b, p); cls, p = sz(b, p); title, p = sz(b, p)
        if style & 0x40:
            p += 2; face, p = sz(b, p)
        out.append(f'  DIALOG "{title}" {cx}x{cy}')
        for i in range(n):
            p = (p + 3) & ~3
            st, exs, x, y, cx, cy, cid = struct.unpack_from('<IIhhhhH', b, p); p += 18
            c, p = sz(b, p); t, p = sz(b, p)
            ex = struct.unpack_from('<H', b, p)[0]; p += 2 + ex
            out.append(f'    ctl id={cid:<5} class={c:<14} text="{t}"')
    return out

for path, data in res:
    t = TYPES.get(path[0], path[0])
    if t == 'STRING':
        bid = path[1]; p = 0
        for i in range(16):
            l = struct.unpack_from('<H', data, p)[0]; p += 2
            if l:
                s = data[p:p + 2 * l].decode('utf-16le'); p += 2 * l
                print(f'STRING {(bid - 1) * 16 + i}: {s!r}')
    elif t == 'DIALOG':
        print(f'DIALOG {path[1]}')
        try:
            for l in dialog(data): print(l)
        except Exception as e:
            print('  (parse error)', e)
    elif t == 'VERSION':
        print('VERSION', data.decode('utf-16le', 'ignore').replace('\x00', ' ')[:400])
    else:
        print(t, path[1:], len(data), 'bytes')
