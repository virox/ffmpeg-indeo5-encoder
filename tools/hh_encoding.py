#!/usr/bin/env python3
"""Check that scalable HH is coded by default and improves diagonal detail."""
from pathlib import Path
import subprocess
import tempfile
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
FF = ROOT / 'ffmpeg/ffmpeg'


def run(args):
    return subprocess.run([str(x) for x in args], check=True, capture_output=True).stdout


def main():
    width, height, frames = 128, 96, 16
    size = width * height * 9 // 8
    source = "nullsrc=s=128x96:r=15,geq=lum='128+64*sin(X*1.7+Y*1.3+N*0.2)':cb=128:cr=128"
    with tempfile.TemporaryDirectory(prefix='iv5-hh-') as folder:
        folder = Path(folder)
        raw = folder / 'source.yuv'
        run([FF, '-v', 'error', '-f', 'lavfi', '-i', source, '-frames:v', frames,
             '-pix_fmt', 'yuv410p', '-f', 'rawvideo', raw])
        reference = np.fromfile(raw, np.uint8).reshape(frames, size)[:, :width*height]
        clips, decoded = {}, {}
        for name, options in [('default', []), ('on', ['-hh_band', '1']), ('off', ['-hh_band', '0'])]:
            clip = clips[name] = folder / (name + '.avi')
            run([FF, '-v', 'error', '-f', 'rawvideo', '-pixel_format', 'yuv410p',
                 '-video_size', f'{width}x{height}', '-framerate', '15', '-i', raw,
                 '-c:v', 'indeo5', '-luma_bands', '4', '-quant', '4', *options, clip])
            for path in ['mmx', 'scalar']:
                data = run([FF, '-v', 'error', '-scalable_synthesis', path, '-i', clip,
                            '-fps_mode', 'passthrough', '-pix_fmt', 'yuv410p', '-f', 'rawvideo', '-'])
                decoded[name, path] = np.frombuffer(data, np.uint8).reshape(frames, size)[:, :width*height]
        assert clips['default'].read_bytes() == clips['on'].read_bytes(), 'HH default differs from explicit enable'
        assert np.array_equal(decoded['off', 'mmx'], decoded['off', 'scalar']), 'Disabled HH changes synthesis'
        assert not np.array_equal(decoded['on', 'mmx'], decoded['on', 'scalar']), 'Enabled HH contains no useful detail'
        mse = {name: np.mean((decoded[name, 'mmx'].astype(float) - reference)**2) for name in ['on', 'off']}
        assert mse['on'] < mse['off'], 'HH fails to improve diagonal detail'
        for path in ['mmx', 'scalar']:
            result = run(['python3', ROOT / 'tools/scalable_parity.py', clips['on'], width, height,
                          '--path', path, '--tolerance', '0'])
            print(result.decode().strip())
        print(f"PASS: default HH is enabled; diagonal luma PSNR on={10*np.log10(255**2/mse['on']):.3f}, off={10*np.log10(255**2/mse['off']):.3f} dB")


if __name__ == '__main__':
    main()
