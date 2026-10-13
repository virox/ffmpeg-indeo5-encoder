#!/usr/bin/env python3
"""Per-frame summary of an IV50 stream (.avi or iv5ref .ivr): frame type, flags,
GOP layout and per-band header fields.  usage: iv5stat.py file [maxframes]"""
import importlib.util, os, sys, collections
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('h', os.path.join(here, 'iv5hdr.py'))
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
FT = {0: 'I', 1: 'P', 2: 'Ps', 3: 'Pd', 4: 'N'}

def parse(p, st):
    r = h.BR(p)
    if r.get(5) != 0x1f: return {'err': 'no start code'}
    ft = r.get(3); fn = r.get(8)
    f = {'type': FT.get(ft, ft), 'num': fn, 'size': len(p)}
    if ft == 0:
        g = {}
        gf = r.get(8); g['gop_flags'] = gf
        if gf & 1: g['gop_size'] = r.get(16)
        if gf & 0x20: g['lock'] = r.get(32)
        if gf & 0x40: g['tile'] = 64 << r.get(2)
        lb = r.get(2) * 3 + 1; cb = r.get(1) * 3 + 1; psi = r.get(4)
        g['bands'] = (lb, cb)
        if psi == 15: g['size'] = (r.get(13), r.get(13))
        else: g['psi'] = psi
        g['desc'] = []
        for pl in range(2):
            for b in range(lb if pl == 0 else cb):
                hp = r.get(1); mbf = r.get(1); bkf = r.get(1); ext = r.get(1); end = r.get(2)
                g['desc'].append((hp, mbf, bkf, ext))
        if gf & 8:
            r.get(3)
            if r.get(1): g['transfill'] = r.get(24)
        r.align(); g['unk23'] = r.get(23)
        if r.get(1):
            ws = []
            while True:
                v = r.get(16); ws.append(v)
                if not v & 0x8000: break
            g['ext'] = ws
        r.align()
        st['gop'] = g
        f['gop'] = g
    if ft == 4:
        return f
    ff = r.get(8); f['ff'] = ff
    if ff & 1: f['pic_size'] = r.get(24)
    if ff & 0x10: f['cs'] = r.get(16)
    if ff & 0x20:
        f['pic_ext'] = True
        while True:
            l = r.get(8)
            for _ in range(l): r.get(8)
            if not l: break
    if ff & 0x40:
        sel = r.get(3)
        if sel == 7:
            n = r.get(4); sel = ('c', tuple(r.get(4) for _ in range(n)))
        f['mbhuff'] = sel
    f['unk3'] = r.get(3); r.align()
    g = st.get('gop')
    if not g: return f
    nb = g['bands'][0] + 2 * g['bands'][1]
    off = r.p // 8; bands = []
    for b in range(nb):
        r = h.BR(p[off:]); bf = r.get(8); bd = {'f': bf}
        if bf & 1:
            bd['empty'] = 1; bands.append(bd); off += 1; continue
        bd['ds'] = r.get(24) if ff & 0x80 else None
        if bf & 0x10:
            n = r.get(8); bd['corr'] = n; [r.get(8) for _ in range(2 * n)]
        bd['rv'] = r.get(3) if bf & 0x40 else 8
        if bf & 0x80:
            s = r.get(3)
            if s == 7:
                n = r.get(4); s = ('c', tuple(r.get(4) for _ in range(n)))
            bd['hf'] = s
        if r.get(1): bd['cs'] = r.get(16)
        bd['q'] = r.get(5)
        if bf & 0x20: bd['bext'] = True
        r.align()
        # tiles
        nt = 1
        if g.get('tile'):
            pass
        bands.append(bd)
        if bd['ds'] is None: break
        off += bd['ds']
    f['bands'] = bands
    return f

if __name__ == '__main__':
    fr = h.avi_frames(sys.argv[1])
    n = int(sys.argv[2]) if len(sys.argv) > 2 else len(fr)
    st = {}; types = collections.Counter(); sizes = collections.defaultdict(list)
    for i, p in enumerate(fr[:n]):
        if not p:
            print(f'{i:4d} (empty chunk)'); types['empty'] += 1; continue
        f = parse(p, st)
        types[f['type']] += 1; sizes[f['type']].append(f['size'])
        line = f"{i:4d} {f['type']:2s} num={f['num']:<3d} size={f['size']:<6d}"
        if 'gop' in f:
            g = f['gop']
            line += f" GOP[fl={g['gop_flags']:#04x} bands={g['bands']} {'psi=%d' % g['psi'] if 'psi' in g else 'size=%s' % (g['size'],)}"
            line += f" desc={g['desc']} unk23={g['unk23']:#08x}"
            for k in ('tile', 'lock', 'transfill', 'ext'):
                if k in g: line += f" {k}={g[k]}"
            line += ']'
        if 'ff' in f:
            line += f" ff={f['ff']:#04x} mbh={f.get('mbhuff')}"
            if f.get('unk3'): line += f" unk3={f['unk3']}"
            for b in f.get('bands', []):
                if b.get('empty'): line += ' [empty]'; continue
                line += f" [{b['f']:#04x} q{b['q']} rv{b['rv']} h{b.get('hf', 'd')}{' c%d' % b['corr'] if 'corr' in b else ''} {b['ds']}]"
        print(line)
    print('frame types:', dict(types), ' avg sizes:', {k: sum(v) // len(v) for k, v in sizes.items()})

def summary(path, fps=15):
    fr = h.avi_frames(path); st = {}
    seq = ''; qs = collections.Counter(); gfl = set(); ffl = collections.Counter(); bfl = collections.Counter()
    gops = set(); tot = 0; extra = set()
    for p in fr:
        if not p: seq += '_'; continue
        f = parse(p, st); tot += len(p)
        seq += {'I': 'I', 'P': 'P', 'Ps': 'S', 'Pd': 'd', 'N': 'N'}.get(f['type'], '?')
        if 'gop' in f:
            g = f['gop']; gfl.add(g['gop_flags'])
            gops.add((g['bands'], tuple(g['desc']), g.get('tile'), g.get('psi', g.get('size')), hex(g['unk23'])))
            for k in ('lock', 'transfill', 'ext'):
                if k in g: extra.add(f'{k}={g[k]}')
        if 'ff' in f:
            ffl[hex(f['ff'])] += 1
            for b in f.get('bands', []):
                if 'q' in b: qs[b['q']] += 1
                bfl[hex(b['f'])] += 1
    print(f'  frames={len(fr)} kbit/s={tot * 8 * fps / max(1, len(fr)) / 1000:.0f} types={seq}')
    print(f'  gop_flags={sorted(hex(x) for x in gfl)} gops={gops} {" ".join(sorted(extra))}')
    print(f'  frame_flags={dict(ffl)} band_flags={dict(bfl)}')
    print(f'  band quant histogram={dict(sorted(qs.items()))}')
