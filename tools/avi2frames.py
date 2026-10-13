#!/usr/bin/env python3
"""Extract video frames of an AVI into [u32 size][data] records for iv5ref.
usage: avi2frames.py in.avi out.bin [max_frames]"""
import importlib.util, os, struct, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('h', os.path.join(here, 'iv5hdr.py'))
h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h)
fr = [f for f in h.avi_frames(sys.argv[1])]
if len(sys.argv) > 3: fr = fr[:int(sys.argv[3])]
with open(sys.argv[2], 'wb') as o:
    for f in fr: o.write(struct.pack('<I', len(f)) + f)
print(len(fr), 'frames')
