# RDO completion (2026-10-12)

Claude's handoff contained a working trellis quantizer and a persistent RDO
strength adjustment, but macroblock modes were still selected with SAD.
The encoder now evaluates intra, searched inter, zero-vector inter and
zero-vector skip using reconstructed distortion and estimated bit costs from
all bands that inherit the decision. DC and differential MV predictors follow
coding order and restart at tile boundaries. Every rate-control trial starts
from the same saved motion-search candidates.

`-rdo -1` (default) selects strength 1 offline and 0 in Quick Compress.
`-rdo 0` disables both trellis and mode RDO; positive values up to 10 set the
strength. Quick Compress can explicitly enable RDO with `-quick 1 -rdo 1`.
Higher strength usually reduces size and quality at a fixed quantizer.

Corrections made during completion:

* Trellis DC distortion uses the incoming DC predictor, before reconstruction.
* Escape symbol bounds are checked before accessing the code-length array.
* Mode comparisons use a common distortion multiplier for intra and inter.
* RC quantizer trials retain a bracket instead of bouncing across the target.
* RC fractional MB quantization uses a bounded search in both profiles.
* The persistent RDO scale can recover after reaching zero. Its reduction
  remains restricted to the configured minimum quantizer.
* The SAD intra estimate respects Quick Compress's 8x8 MB layout.
* Motion references are restricted to padding covered by coded MBs.

## Decoder evidence and edge correction

The added 100x60 scalable Quick Compress test exposed an existing right-edge
mismatch. The saved pre-continuation executable also fails this comparison
with RDO disabled (maximum luma difference 126). It was not a sandbox error.

REA/Ghidra analysis of `IR50_32.DLL` (SHA-256
`56760e0ea8c8709f4a0c34bee7289a87188aedd8bddfd05f8f62bee2f3f91238`):

* `FUN_10034040` rounds the horizontal synthesis viewport to 32 pixels;
  evidence `ev_425294c0004aca7e07513aa1507c5eb0b9a30212247c2fcc39fcb01195883ba6`.
* `FUN_10034340` applies the right-edge replacement at that viewport boundary;
  evidence `ev_ed7ebb9985c47e5d47b804f5f9e7360d5dc327937a5b9aa38be9e9d3cc2e4d5b`.

`ff_ivi_recompose53()` now uses that boundary while writing visible pixels.
The encoder also avoids motion references into allocation padding beyond the
coded MBs. The new 100x60 QC/RDO stream compares exactly with Intel over all
90,000 luma samples. The boundary explanation is supported by static code;
the combined correction is validated by decoder output. This does not claim
that every old QC stream has identical padding semantics in both decoders.

The original REA snapshot is preserved; additional evidence is saved in
`re/ir50_32.rdo.rea-snapshot`.

## Rate versus quality

Source: first 90 frames of the local Thomas AVI, scaled to 320x240 yuv410p,
then encoded at 15 fps, default GOP 15. PSNR is computed from aggregate luma
MSE against that raw reference using this tree's FFmpeg decoder. Bitrates
count video payload only. Each clip is six seconds; these measurements are
examples, not a general rate-accuracy guarantee. Synthetic compatibility
clips are checked independently against Intel's decoder.

| Profile | Target kbit/s | RDO off: actual / Y-PSNR | RDO on: actual / Y-PSNR |
|---|---:|---:|---:|
| Basic | 150 | 207.52 / 32.405 dB | 119.11 / 32.572 dB |
| Basic | 300 | 269.35 / 34.066 dB | 269.33 / 37.103 dB |
| Basic | 600 | 595.85 / 35.305 dB | 580.74 / 39.545 dB |
| Basic | 900 | 908.58 / 39.146 dB | 911.62 / 41.070 dB |
| Scalable | 150 | 238.43 / 32.382 dB | 114.97 / 32.307 dB |
| Scalable | 300 | 284.40 / 33.550 dB | 275.74 / 36.446 dB |
| Scalable | 600 | 596.74 / 36.764 dB | 588.30 / 38.795 dB |
| Scalable | 900 | 919.96 / 38.447 dB | 919.47 / 39.658 dB |

At the 600 kbit/s target, basic-profile RDO improved luma PSNR from 35.305
to 39.545 dB while producing 580.74 kbit/s. Claude's handoff produced
563.77 kbit/s / 38.812 dB on the same source. Scalable RDO produced
588.30 kbit/s / 38.795 dB, versus the handoff's 556.05 / 38.505 dB.

Limitations: short low-rate clips still undershoot (150 kbit/s requested,
115–119 produced with RDO), and targets below what the largest quantizer can
achieve can overshoot. Offline RDO costs substantial CPU time, particularly
when RC needs several trials. Quick Compress retains RDO off by default.
Trellis distortion is a basis-energy approximation; scalable mode distortion
is measured per band rather than through joint wavelet synthesis. Entropy
lengths come from a first pass; final tables are selected again. This is an
independent RD implementation, not a reconstruction of Intel's exact search.

Raw measurements: `out/work/rdo-benchmark.json`,
`out/work/rdo-benchmark-claude.json`. The benchmark script and pre-continuation
encoder snapshot/executable are retained under `out/work/`.

## Validation

`tools/regress.sh` covers 73 option combinations, including RDO off, weak and
strong strengths, explicit QC/RDO, AQ, RC, tiling, transparency, access keys,
NULL and droppable frames, and partial MBs. It checks decoder errors, band
checksums, Intel luma output and transparency masks, and returns failure status
when a case fails. `python3 tools/rdo_recovery.py` separately verifies that a
flat-to-noise scene change takes the RDO scale to zero and then restores it.
The test fixes the minimum quantizer at 6 to reliably exercise that boundary.

The compatibility log is `out/work/rdo-regress-complete.log`. Those historical tests used automatic Intel CPU-path selection and allowed a one-level difference.
The later synthesis investigation explicitly selects MMX and requires zero differences; see
`SYNTHESIS_PARITY.md`. RDO benchmark results are unaffected because those benchmark encodes used HH as zero.

## Subsequent HH update

The scalable benchmarks above used the former HH-disabled encoder. Scalable
encoding now retains HH by default; their figures remain historical results,
not measurements of the current default. See `HH_ENCODING.md` and the Thomas
episode comparison in `THOMAS_ENCODE.md`. Basic-profile behavior is unchanged.

## Subsequent speed and rate-control update

`ENCODER_REFINEMENTS.md` supersedes the earlier speed and RC accuracy figures
with transform/candidate reuse, corrected debt accounting and initial reference
allocation. Constant-quantizer output is preserved; current rate-controlled
output changes deliberately. Exact scalar rounding and QC padding are recorded
in `SCALAR_PARITY.md`.
