#!/usr/bin/env python3
"""Check rate-control accounting, startup bounds, and repeated NULL frames."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FF = ROOT / 'ffmpeg/ffmpeg'
RECORD = re.compile(r'rc: type (\d+) target ([\d.]+) bits (\d+) q (\d+) rdo ([\d.]+) debt ([-\d.]+) allocation ([\d.]+)')


def main():
    with tempfile.TemporaryDirectory(prefix='iv5-rate-control-') as directory:
        directory = Path(directory)
        for lb in [1, 4]:
            for repeated in [False, True]:
                source = 'testsrc2=size=128x96:rate=15,scroll=h=0.02'
                if repeated:
                    source += ',fps=5,fps=15'
                output = directory / f'lb{lb}-null{int(repeated)}.avi'
                encode = subprocess.run([str(FF), '-v', 'debug', '-nostats', '-y', '-f', 'lavfi',
                    '-i', source, '-frames:v', '45', '-an', '-c:v', 'indeo5', '-luma_bands', str(lb),
                    '-b:v', '40k', '-null_frames', str(int(repeated)), str(output)],
                    check=True, capture_output=True, text=True)
                records = [tuple(map(float, match)) for match in RECORD.findall(encode.stderr)]
                if len(records) != 45:
                    raise RuntimeError(f'Missing RC records: {len(records)}')
                if records[0][3] <= 9:
                    raise RuntimeError('Initial quantizer remains trapped by the swing limit')
                nulls = 0
                for index, (kind, target, bits, quant, strength, debt, allocation) in enumerate(records):
                    if not (0 <= quant <= 23):
                        raise RuntimeError('Quantizer outside configured range')
                    if kind == 4:
                        nulls += 1
                        if bits != 16:
                            raise RuntimeError('NULL frame is not accounted as two bytes')
                    if index + 1 < len(records):
                        expected = max(-6000, min(80000, debt + bits - allocation))
                        actual = records[index + 1][5]
                        if abs(actual - expected) > 1.5:
                            raise RuntimeError(f'Debt failed to repay at frame {index}: {actual} vs {expected}')
                if repeated and not nulls:
                    raise RuntimeError('Repeated-frame test produced no NULL packets')
                decode = subprocess.run([str(FF), '-v', 'error', '-i', str(output), '-f', 'null', '-'],
                    check=True, capture_output=True, text=True)
                if decode.stderr.strip():
                    raise RuntimeError(decode.stderr)
                print(f'PASS: lb={lb}, repeated={repeated}, {nulls} NULL frames; startup and debt accounting')
        invalid = subprocess.run([str(FF), '-v', 'error', '-f', 'lavfi', '-i', 'testsrc2=size=64x64',
            '-frames:v', '1', '-c:v', 'indeo5', '-rc_qmin', '10', '-rc_qmax', '5', '-f', 'null', '-'],
            capture_output=True, text=True)
        if invalid.returncode == 0 or 'rc_qmin must not exceed rc_qmax' not in invalid.stderr:
            raise RuntimeError('Invalid quantizer range was not rejected')
        print('PASS: invalid RC quantizer range rejected')


if __name__ == '__main__':
    main()
