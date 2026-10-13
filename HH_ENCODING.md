# Scalable HH encoding

The encoder now retains and codes the diagonal high-frequency (HH) luma
coefficients from its existing wavelet analysis. The existing band transform,
quantization, entropy coding, reconstructed references and joint MB RDO already
support this band; the previous implementation explicitly cleared its source.

`-luma_bands 4` now enables HH by default. `-hh_band 0` restores the earlier
behavior. The option has no effect on the basic (`-luma_bands 1`) profile.

Intel's MMX decoder and FFmpeg's default synthesis include HH. Intel's scalar
path omits it, so scalar playback of an HH-enabled stream loses diagonal detail.
Use `-hh_band 0` when common scalar/MMX reconstruction is needed. This is the
original DLL's CPU-path distinction, not a new bitstream extension.

## Checks

A 30-frame scrolling testsrc2 clip at 320×240, 15 fps, quantizer 4 and RDO 1:

| HH | AVI bytes | Aggregate luma PSNR vs encoder input |
| --- | ---: | ---: |
| Disabled | 225,650 | 39.483 dB |
| Enabled | 240,302 | 43.426 dB |

The enabled stream matches Intel MMX exactly across all 2,304,000 luma samples;
scalar decoding also matches on this focused clip. Comparing MMX and scalar
output of the enabled stream finds 167,969 differing samples (maximum 79),
confirming useful nonzero HH. The disabled stream has identical MMX/scalar
output. These quality figures use aligned raw frames, not framesync-filter
comparisons. Bitrate and quality gains depend on the source and settings.

Reproduce the enabled encode and decoder check:

```sh
ffmpeg/ffmpeg -v error -y -f lavfi \
  -i 'testsrc2=size=320x240:rate=15,scroll=h=0.013:v=0.007' \
  -frames:v 30 -c:v indeo5 -luma_bands 4 -hh_band 1 -quant 4 \
  out/work/hh-on.avi
python3 tools/scalable_parity.py out/work/hh-on.avi 320 240
python3 tools/scalable_parity.py out/work/hh-on.avi 320 240 \
  --path scalar
```

The regression matrix adds explicit HH-disabled offline/Quick Compress cases
and an HH-enabled, quantizer-zero, tiled partial-MB case. All existing scalable
cases now exercise HH-enabled encoding, including inter/droppable frames,
rate control, RDO, transparency, checksums and access keys.

The complete 76-case matrix passes with no decoder errors or checksum
mismatches and exact Intel MMX luma. Log: `out/work/hh-regress.log`.

`python3 tools/hh_encoding.py` checks a moving diagonal sinusoid, asserts
that default and explicit HH enable produce identical AVI bytes, verifies useful
HH and the disabled scalar/MMX equivalence, and checks Intel decoder parity.
It passes; diagonal luma PSNR is 30.722 dB enabled versus 22.375 dB disabled.
