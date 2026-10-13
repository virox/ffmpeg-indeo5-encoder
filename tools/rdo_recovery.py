#!/usr/bin/env python3
"""Check that rate control can restore RDO after disabling it at the minimum quantizer."""
import pathlib
import random
import re
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
FFMPEG = ROOT / 'ffmpeg/ffmpeg'


def main():
    rng = random.Random(5)
    with tempfile.TemporaryDirectory(prefix='iv5-rdo-recovery-') as directory:
        directory = pathlib.Path(directory)
        source = directory / 'source.yuv'
        output = directory / 'encoded.avi'
        # Flat frames exhaust useful bits even at the minimum quantizer. Random frames then
        # require higher quantizers, giving the RDO strength room to recover.
        with source.open('wb') as stream:
            stream.write(bytes([128]) * (45 * 4608))
            for _ in range(45):
                stream.write(bytes(rng.randrange(256) for _ in range(4096)))
                stream.write(bytes([128]) * 512)
        encode = subprocess.run([
            str(FFMPEG), '-v', 'debug', '-nostats', '-y', '-f', 'rawvideo',
            '-pix_fmt', 'yuv410p', '-s', '64x64', '-r', '15', '-i', str(source),
            '-an', '-c:v', 'indeo5', '-quant', '6', '-rc_qmin', '6',
            '-b:v', '100k', str(output),
        ], check=True, capture_output=True, text=True)
        strengths = [float(value) for value in re.findall(
            r'rc: type \d target \S+ bits \d+ q \d+ rdo ([\d.]+)', encode.stderr)]
        if len(strengths) != 90 or 0 not in strengths:
            raise RuntimeError('RDO did not reach zero during the 90-frame sequence')
        first_zero = strengths.index(0)
        if not any(value > 0 for value in strengths[first_zero + 1:]):
            raise RuntimeError('RDO stayed at zero after quantizer headroom returned')
        decode = subprocess.run([
            str(FFMPEG), '-v', 'error', '-i', str(output), '-f', 'null', '-',
        ], check=True, capture_output=True, text=True)
        if decode.stderr.strip():
            raise RuntimeError(decode.stderr)
    print('OK   RDO strength reaches zero and recovers after scene change')


if __name__ == '__main__':
    main()
