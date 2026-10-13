#!/usr/bin/env python3
"""Compare an IV50 clip with Intel's explicitly selected synthesis path.

Usage: python3 tools/scalable_parity.py clip.avi W H [--path mmx|scalar]
       [--tolerance N]
Both paths are exact by default. Use --path scalar for Intel's HH-omitting
paired arithmetic; --tolerance permits an explicit diagnostic margin.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def run(command):
    result = subprocess.run([str(value) for value in command], capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.decode(errors='replace'))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('clip', type=Path)
    parser.add_argument('width', type=int)
    parser.add_argument('height', type=int)
    parser.add_argument('--path', choices=['mmx', 'scalar'], default='mmx')
    parser.add_argument('--tolerance', type=int, default=0)
    args = parser.parse_args()
    w, h = args.width, args.height
    if w <= 0 or h <= 0 or w % 4 or h % 4 or args.tolerance < 0:
        parser.error('Intel requires positive dimensions divisible by 4; tolerance must be nonnegative')
    with tempfile.TemporaryDirectory(prefix='iv5-scalable-parity-') as directory:
        directory = Path(directory)
        frames = directory / 'frames.bin'
        intel = directory / 'intel.i420'
        ff0 = directory / 'ff0.yuv'
        ff = directory / 'ff.yuv'
        run(['python3', ROOT / 'tools/avi2frames.py', args.clip, frames])
        result = run([ROOT / 'tools/iv5ref/iv5ref', ROOT / 'IR50_32.DLL',
                      frames, w, h, intel, 'I420:12',
                      'mmx=' + ('1' if args.path == 'mmx' else '0')])
        if b'ICM_DECOMPRESS ->' in result.stderr or b'ICM_DECOMPRESS_BEGIN ->' in result.stderr:
            raise RuntimeError(result.stderr.decode(errors='replace'))
        result = run([ROOT / 'ffmpeg/ffmpeg', '-v', 'error', '-y',
                      '-scalable_synthesis', args.path, '-i', args.clip,
                      '-fps_mode', 'passthrough', '-an', '-pix_fmt', 'yuv410p', '-f', 'rawvideo', ff0])
        if result.stderr.strip():
            raise RuntimeError(result.stderr.decode(errors='replace'))
        run(['python3', ROOT / 'tools/expand_null.py', args.clip, ff0, ff, w, h])
        fa, fb = w * h * 3 // 2, w * h + 2 * (w // 4) * (h // 4)
        na, ra = divmod(intel.stat().st_size, fa)
        nb, rb = divmod(ff.stat().st_size, fb)
        if not na or na != nb or ra or rb:
            raise RuntimeError(f'Invalid frame counts: Intel={na}, FFmpeg={nb}, remainders={ra}/{rb}')
        a = np.memmap(intel, np.uint8, 'r', shape=(na, fa))
        b = np.memmap(ff, np.uint8, 'r', shape=(nb, fb))
        different = maximum = 0
        first = None
        for start in range(0, na, 20):
            delta = a[start:start + 20, :w*h].astype(np.int16) - b[start:start + 20, :w*h]
            different += int(np.count_nonzero(delta))
            maximum = max(maximum, int(np.max(np.abs(delta))))
            if first is None and np.any(np.abs(delta) > args.tolerance):
                frame, pixel = np.argwhere(np.abs(delta) > args.tolerance)[0]
                first = (start + int(frame), int(pixel) // w, int(pixel) % w, int(delta[frame, pixel]))
        del a, b
    print(f'{args.path}: {na} frames, {na*w*h} luma samples, {different} differ, max |diff| {maximum}')
    if first is not None:
        raise SystemExit(f'FAIL: first difference beyond tolerance (frame, y, x, delta)={first}')
    print('PASS')


if __name__ == '__main__':
    main()
