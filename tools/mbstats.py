#!/usr/bin/env python3
"""Aggregate MBSTAT lines (from the instrumented decoder) into per-band statistics."""
import re, sys, collections
st = collections.defaultdict(lambda: collections.Counter()); mv = collections.defaultdict(list)
for line in sys.stdin:
    m = re.search(r'MBSTAT ft=(\d) p=(\d) b=(\d) type=(\d) cbp=(\d+) q=(-?\d+) mv=(-?\d+),(-?\d+) empty=(\d) inh=(\d) qdp=(\d) hp=(\d)', line)
    if not m: continue
    ft, p, b, t, cbp, q, mx, my, emp, inh, qdp, hp = map(int, m.groups())
    k = ('I' if ft == 0 else 'inter', 'Y%d' % b if p == 0 else 'C')
    c = st[k]; c['mbs'] += 1; c['type%d' % t] += 1; c['empty'] += emp; c['cbp0'] += cbp == 0
    c['qdelta!=0'] += q != 0; c['qdp'] |= qdp; c['inherit'] |= inh; c['halfpel'] |= hp
    if t and not inh:
        mv[k].append((mx, my)); c['odd_mv'] += (mx & 1) or (my & 1)
for k in sorted(st):
    c = st[k]; v = mv[k]
    rng = (min(x for x, _ in v), max(x for x, _ in v), min(y for _, y in v), max(y for _, y in v)) if v else None
    print(f"  {k[0]:5s} {k[1]:3s} MBs={c['mbs']:<6d} intra={c['type0']:<6d} inter={c['type1']:<6d} empty={c['empty']:<6d} "
          f"cbp0={c['cbp0']:<6d} qdelta!=0={c['qdelta!=0']:<5d} qdp={c['qdp']} inherit={c['inherit']} halfpel={c['halfpel']} "
          f"odd_mv={c['odd_mv']} mv_range(x,y)={rng}")
