#!/usr/bin/env python3
"""Decode the Indeo 5 transparency plane (data following the YUV bands when
GOP flag 0x08 is set), following IR50_32.DLL FUN_10050c20 / FUN_10050790.
usage: iv5trans.py stream(.avi|.ivr) W H [frames] [out.mask]
Prints the header fields; writes 1 byte per pixel (1 = opaque) masks."""
import importlib.util, os, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('s', os.path.join(here, 'iv5stat.py'))
st = importlib.util.module_from_spec(spec); spec.loader.exec_module(st)
h = st.h

class BR:
    def __init__(s, b, p=0): s.b = b; s.p = p
    def get(s, n):
        v = 0
        for k in range(n):
            byte = s.b[s.p >> 3] if (s.p >> 3) < len(s.b) else 0
            v |= ((byte >> (s.p & 7)) & 1) << k
            s.p += 1
        return v
    def align(s): s.p = (s.p + 7) & ~7

def ivi_codes(desc):
    """IVI huffman descriptor -> {(len, code_lsb_first): symbol}"""
    rows = len(desc); out = {}; pos = 0
    for i, xb in enumerate(desc):
        nl = i != rows - 1
        for j in range(1 << xb):
            if pos >= 256: break
            ln = i + xb + nl
            code = (((1 << i) - 1) << (xb + nl)) | j      # MSB-first value
            rev = int(format(code, '0%db' % ln)[::-1], 2) if ln else 0
            out[(ln, rev)] = pos; pos += 1
    return out

def read_sym(r, codes, maxlen=13):
    v = 0
    for ln in range(1, maxlen + 1):
        v |= r.get(1) << (ln - 1)
        if (ln, v) in codes:
            return codes[(ln, v)]
    raise ValueError('bad run code')

def decode_rle(data, pos_bytes, cw, ch, codes, xor_rows):
    """RLE mask of one tile: ch rows of cw pixels (cw = tile width rounded up
    to 32); returns (rows, bytes consumed)"""
    r = BR(data, pos_bytes * 8)
    color = r.get(1)
    total = cw * ch
    bits = []
    while len(bits) < total:
        sym = read_sym(r, codes)
        run = 255 if sym == 0 else sym
        bits += [color] * run
        if sym != 0:
            color ^= 1
    if len(bits) != total:
        raise ValueError('runs overshoot the tile')
    rows = [bits[y * cw:(y + 1) * cw] for y in range(ch)]
    if xor_rows:
        for y in range(1, ch):
            rows[y] = [a ^ b for a, b in zip(rows[y], rows[y - 1])]
    used = (r.p + 7) // 8 - pos_bytes
    return rows, used

def tiles(W, H, t):
    """transparency tile grid (IR50_32.DLL): tile_size pixels or the whole
    picture; the last column ends at the picture width rounded up to 32"""
    tw = W if not t or W < t else t
    th = H if not t or H < t else t
    W32 = (W + 31) & ~31
    for y0 in range(0, H, th):
        for x0 in range(0, W, tw):
            yield x0, y0, min((tw + 31) & ~31, W32 - x0), min(th, H - y0)

def parse_trans(p, start, w, h, desc_state, size_flag=True, tile=0):
    r = BR(p, start * 8)
    info = {}
    if size_flag:
        info['size'] = r.get(24)
    n = r.get(3); info['rects'] = [tuple(r.get(16) for _ in range(4)) for _ in range(n)]
    info['xor_rows'] = r.get(1)
    if r.get(1):
        cnt = r.get(4); desc_state[:] = [r.get(4) for _ in range(cnt)]
        info['desc'] = list(desc_state)
    r.align()
    codes = ivi_codes(desc_state)
    mask = [[0] * w for _ in range(h)]
    kinds = []
    run_start = r.p // 8
    for x0, y0, cw, ch in tiles(w, h, tile):
        if r.get(1):
            v = r.get(1); kinds.append('u%d' % v)
            rows = [[v] * cw for _ in range(ch)]
        else:
            if r.get(1):
                ln = r.get(8)
                if ln == 255: ln = r.get(24)
                r.align(); kinds.append('s%d' % ln)
            else:
                ln = None; r.align(); kinds.append('i')
            rows, used = decode_rle(p, r.p // 8, cw, ch, codes, info['xor_rows'])
            end = r.p // 8 + used
            if ln is not None and run_start + ln != end:
                raise ValueError('tile size %d != %d' % (ln, end - run_start))
            r.p = end * 8; run_start = end
        for y in range(ch):
            for x in range(min(cw, w - x0)):
                mask[y0 + y][x0 + x] = rows[y][x]
    info['tiles'] = ' '.join(kinds) if len(kinds) <= 12 else '%d tiles, %d coded' % (len(kinds), sum(k[0] != 'u' for k in kinds))
    info['end'] = (r.p + 7) // 8 - start
    return info, mask

if __name__ == '__main__':
    path, W, H = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    n = int(sys.argv[4]) if len(sys.argv) > 4 else 3
    out = open(sys.argv[5], 'wb') if len(sys.argv) > 5 else None
    fr = h.avi_frames(path); state = {}; desc = []
    for i, p in enumerate(fr[:n]):
        f = st.parse(p, state)
        if not state.get('gop', {}).get('gop_flags', 0) & 8:
            print(i, 'no transparency'); continue
        # find end of the bands
        b0 = f['bands'][0]
        off = next(o for o in range(4, 64) if p[o] == b0['f'] and int.from_bytes(p[o+1:o+4], 'little') == b0['ds'])
        end = off + sum(b['ds'] for b in f['bands'])
        try:
            info, mask = parse_trans(p, end, W, H, desc, tile=state.get('gop', {}).get('tile', 0))
        except Exception as e:
            print(i, 'parse error', e, p[end:end+16].hex()); continue
        opaque = sum(map(sum, mask))
        print(i, f['type'], {k: v for k, v in info.items()}, 'opaque px', opaque, 'of', W * H)
        if out:
            out.write(bytes(v for row in mask for v in row))
