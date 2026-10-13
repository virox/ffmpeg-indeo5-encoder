# Indeo Video 5 (IV50) encoder for FFmpeg: notes

## Layout

| Path | What |
|---|---|
| `ffmpeg/` | FFmpeg git checkout (615b071) with `scripts/apply.sh` applied (not tracked in git) |
| `patches/0001-*.patch` | decoder changes: `ivi_dsp.c` synthesis fix, access key and transparency-region synthesis in `indeo5.c`/`ivi.c`/`ivi.h` |
| `patches/0002-*.patch` | build wiring: `allcodecs.c`, `Makefile`, `configure` |
| `src/libavcodec/indeo5enc.c` | the encoder (canonical copy; `scripts/apply.sh` adds it to an FFmpeg checkout, `scripts/export.sh` regenerates `src/` and `patches/` from a modified `ffmpeg/`) |
| `tools/iv5hdr.py` | dumps IV50 picture/GOP/band headers of an AVI |
| `tools/checksums.py` | checks frame/band checksums against decoded YVU9 |
| `tools/avi2frames.py` | extracts AVI video frames as `[u32 size][data]` records |
| `tools/iv5ref/iv5ref.c` | freestanding i386 loader that runs Intel's `IR50_32.DLL` decoder natively (no Wine) |
| `tools/cmp_intel.sh` | decodes an AVI with both Intel's DLL and FFmpeg and prints the PSNR between them (RGB24 path) |
| `tools/cmp_luma.py` | bit-exact luma comparison: Intel I420 output (`iv5ref … I420:12`) against FFmpeg yuv410p |
| `tools/expand_null.py`, `tools/build_dbg.sh` | align FFmpeg output with Intel's for NULL frames; build the checksum-verifying `out/ffmpeg_dbg` |
| `tools/iv5trans.py`, `tools/cmp_mask.py`, `tools/cmp_luma_masked.py` | transparency plane parser (tiled or not), Intel-decoded vs parsed mask comparison, luma comparison over opaque pixels |
| `tools/wavelet_model.py` | numpy model of the 4-band synthesis, used to fit Intel's behaviour from band dumps |
| `ENCODER_FEATURES.md` | feature/parameter audit of Intel's encoder vs ours, with roadmap |
| `re/` | RE artefacts: full Ghidra decompilation, resources, type library, Intel VfW header, help file, encoder experiment outputs (`re/enc/`) |
| `tools/iv5ref/iv5ref enc …` | runs **Intel's encoder** (incl. Quick Compress DLLs) natively; `tools/intel_enc.sh` wraps it with a stream summary |
| `tools/iv5stat.py`, `tools/ivr2avi.py`, `tools/mbstats.py`, `tools/fn.sh`, `tools/pe_resources.py` | stream statistics, raw-to-AVI, macroblock stats (`IV5_MBSTATS=1 out/ffmpeg_mbstats -v debug -i x.avi -f null - 2>&1 | tools/mbstats.py`), decompiled-function lookup, PE resource dump |
| `samples/` | Intel-encoded reference files (FFmpeg samples, plus `intro.avi` and `intro_01.avi`) |

Build (fresh checkout): `git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg && git -C ffmpeg checkout $(. scripts/base-commit; echo $BASE_COMMIT) && scripts/apply.sh ffmpeg`, then
`cd ffmpeg && ./configure --disable-x86asm --disable-doc --disable-autodetect && make -j16 ffmpeg`
(`--disable-x86asm` only because nasm isn't installed here; not needed otherwise.)

Workflow: edit the encoder/decoder in the untracked `ffmpeg/` checkout, rebuild and run `tools/regress.sh`, then run `scripts/export.sh` to refresh
`src/` and `patches/`, update the docs, and commit. Never edit `src/` or `patches/` by hand; `export.sh` overwrites them.

Use: `ffmpeg -i input.mp4 -c:v indeo5 [options] -an output.avi`. Options: `-quant`/`-quality`/`-b:v`, `-g`, `-droppable`, `-luma_bands 1|4`, `-hh_band 0|1` (default 1; scalable diagonal detail), `-tables`, `-aq_strength`, `-me_range`, `-tile_size`, `-null_frames`, `-quick` (Intel's Quick Compress profile), `-transparency none|alpha|range|first_frame` (alpha: yuva420p input; range/first_frame: bgr24 input, range set with `-transparency_lo/-transparency_hi 0xRRGGBB`), `-access_key`, `-checksum`, `-rdo` (auto: 1 offline, 0 quick; 0 disables trellis and RD modes); see `ffmpeg -h encoder=indeo5` and ENCODER_FEATURES.md. Protected clips decode with `ffmpeg -access_key N -i clip.avi …`. Full verification: `tools/regress.sh` (rebuild its checksum-verifying decoder `out/ffmpeg_dbg` with `tools/build_dbg.sh` after decoder changes).
(the pixel format is converted to yuv410p automatically). Add `-c:a pcm_s16le` for audio like the samples.

* `intro.avi` layout (640x480, basic profile): `-c:v indeo5` (default `-luma_bands 1`)
* `intro_01.avi` layout (320x240, scalable profile): `-c:v indeo5 -luma_bands 4`

## What the encoder writes

Defaults match the Intel encoder's offline (5.06) output, as seen in `samples/intro.avi`:

* key frames (type 0, GOP header, `frame_num` restarts at 0) every `-g` frames (default 15), P frames (type 1) in
  between; `-droppable 1` gives Intel's `I d P d P` (type 3) or, scalable, `I S S P` (type 2) patterns;
  `-null_frames 1` codes unchanged frames as type 4 (both profiles)
* GOP flags `0x81` (+0x08 transparency, +0x20 access key, +0x40 tiles), half-pel flag set in all band descriptors,
  version stamp `0x102C0F` (5.06 build 44)
* 1 luma band (16x16 MB, 8x8 slant), 1 band per chroma plane (4x4 MB = one 4x4 slant block); one tile per band
  unless `-tile_size`; P-frame tiles whose MBs are all empty with zero vectors are coded as a single `is_empty` bit
* picture size: standard index when it matches (640x480 → 0, 320x240 → 1, …), otherwise the 13+13-bit escape
* frame flags `0xD5`: band data sizes, MB codebook, frame checksum, 0x04 (meaning unknown, Intel always sets it), picture size
* band flags: rvmap + block codebook (static or custom, with rvmap correction pairs), MV inheritance and q_delta flags as needed
* band checksums; `glob_quant` from `-quant`/`-quality`/rate control; with AQ (`-aq_strength`, rate control) luma band 0
  sends a delta for every MB (frame flag 0x08) and all other bands inherit it (band flags 0x0C), as Intel does
* the 3 bits after the MB codebook: per-plane "no clipping needed" hint, set when the reconstruction allows it
* block data: run/level symbols via the selected rvmap, escape (`esc, run, lo6, hi`) otherwise, always terminated by EOB

Transform: the forward slant transforms are derived at init time by measuring the response of
FFmpeg's own `ff_ivi_inverse_slant_{8x8,4x4}` to impulses and inverting that matrix (Gauss-Jordan).
That makes them, by construction, the inverse of what the decoder does. Quantization picks the
level whose reconstruction (`l*q + sign*((q^1)-1)>>1`) is nearest, and DC is predicted from the previous
block exactly as the decoder rebuilds it. The encoder also reconstructs every band bit-exactly
(same `ff_ivi_*` inverse transforms) in order to write the checksums.

### Scalable profile (`-luma_bands 4`, the `intro_01.avi` layout)

* GOP header: `luma_levels = 1` (4 luma bands), 4 luma band descriptors (16x16 MB / 8x8 blocks) and a chroma
  descriptor with 8x8 MB / 4x4 blocks (4 blocks per MB); GOP trailer `0x102C0F` as in `intro_01.avi`
* luma bands are half size; band 0 = LL (2D slant, zigzag, quant matrix 1, DC prediction), band 1 = vertical
  high/horizontal low (row slant, vertical scan, matrix 2), band 2 = vertical low/horizontal high (column slant,
  horizontal scan, matrix 3), band 3 = HH (no transform, matrix 4). The non-2D bands have no DC prediction.
* the wavelet analysis is the exact inverse of the decoder's synthesis: the 1D synthesis (×8)
  `x[2n] = 4L[n] + 2(H[n-1]+H[n])`, `x[2n+1] = 2(L[n]+L[n+1]) + H[n-1] − 6H[n] + H[n+1]` is a pentadiagonal
  system once L/H are interleaved; it is LU-factored once per dimension and solved per row/column. Edge rules
  follow Intel: replicate (`L[m]=L[m-1]`, `H[-1]=H[0]`), except vertically at the bottom `H[m] = H[m-2]` (mirror).
  `+0.5` compensates the decoder's truncating `>> 6`.
* HH is encoded by default; MMX includes it, scalar omits it. `-hh_band 0` gives the previous common reconstruction for both paths (see `HH_ENCODING.md`).
* same `glob_quant` for all bands, as Intel does.

## Bitstream facts established here (not in FFmpeg's decoder or the wiki)

Established from the samples and confirmed with the Intel DLL:

1. **Band data size** (24 bits, frame flag 0x80) = whole band in bytes, *including* the band header.
2. **Picture size** (24 bits, frame flag 0x01) = `(bytes up to the end of the last band + 8) & ~3`. The gap is zero padding.
   Intel's decoder recomputes this and returns internal status 2 → `ICERR_ERROR (-100)` if it differs
   (`FUN_1003eaf0`, the `+0xe0` check near the end). An earlier `round_up(end+4, 8)` guess matched
   all sample frames but failed ~50% of frames in Intel's decoder.
3. **Band checksum** (16 bits) = sum of all reconstructed band samples (signed, before +128 and clipping) mod 2^16.
   **Frame checksum** (frame flag 0x10) = sum of the band checksums mod 2^16. Verified on 200 frames of
   `intro.avi`. The FFmpeg decoder only checks band checksums, and only in `-DDEBUG` builds.
4. **GOP header size** (16 bits) = bytes from `gop_flags` to the end of the GOP header, including its alignment and the 24-bit trailer.
5. Tile size field = bytes from the tile's `is_empty` bit through the aligned end of block data (its own
   2- or 5-byte header included), as the FFmpeg decoder already implies.
6. Plane order in the bitstream is Y, V, U (YVU9); `planes[1]` is Cr.
7. Layout constraint: chroma tiles must hold as many MBs as luma tiles (the decoder uses the luma band as
   reference for MVs/quant). With 1 luma band that forces luma MB 16 + chroma MB 4, which is what Intel uses.
8. **Scalable synthesis in Intel's decoder** (fitted from exact band data against Intel's I420 output, and confirmed
   in the MMX code at `FUN_10034340`):
   * filters as in FFmpeg's `ff_ivi_recompose53`, **but** FFmpeg has a bug in the band-1 path
     (`tmp2 = tmp1 - tmp0*6 + b1_3` must be `tmp2 = b1_3`), which made FFmpeg's decode of every Intel scalable
     stream wrong (only 74–94% of luma samples matched Intel). Fixed in `ivi_dsp.c`.
   * last row: the vertically high-passed bands are mirrored (`H[m] = H[m-2]`), not replicated;
     last column of the 32-pixel-aligned synthesis viewport: copied from the column before. Partial viewports use coded band padding. Both now done in `ff_ivi_recompose53` too.
   * **HH behavior is path-dependent:** Intel's MMX synthesis includes HH; its scalar synthesis omits it.
     The original harness experiments selected scalar, so the previous claim that MMX ignored HH was wrong.
     FFmpeg now includes HH by default (`-scalable_synthesis mmx`); `-scalable_synthesis scalar` omits it.
     With MMX explicitly selected, all 2,151 frames of `intro_01.avi` match exactly. See `SYNTHESIS_PARITY.md`.
   * the decoder has an adaptive "scalability" mechanism (band limit at `ctx+0x1d170`, driven by measured
     decode load) that can drop bands under CPU load; it was not active in these tests. The registry key
     `Software\Intel\Indeo\5.0\Decoder` (Scalability Levels, Delta frames, …) holds *encoder* defaults.
9. `IR50_32.DLL` decompresses to RGB24, I420/IYUV/YV12, YUY2 and UYVY, but rejects YVU9/IF09 output. It also loads
   "TooOldVersion"/"TooNewVersion" error bitmaps; the format-version field in the GOP trailer controls output remapping (see below).

## Earlier verification (automatic Intel CPU-path selection)

* FFmpeg (this tree) and an independent FFmpeg 7.0.2 static build decode all test files with zero warnings,
  at 320x240, 640x480, 176x144, 352x288, 333x197 (odd sizes), 1280x720.
* The `ivi.c` decoder built with `-DDEBUG` reports 0 band-checksum mismatches, so encoder and decoder reconstructions are bit-exact.
* **Scalable profile**: Intel's decoder accepts every frame (320x240, 640x480, 352x288, 176x144, 160x120,
  336x200, 320x180). With the decoder fixes, **FFmpeg's and Intel's luma output are identical** for the
  `intro_01.avi`-style streams (8 samples in 4.6 million differ by 1); 640x480, 352x288 and 320x180 are fully
  bit-exact. Intel playback quality on `intro_01.avi` content: 42.3 dB @ 2.8 Mbit/s (quant 2),
  42.1 dB @ 2.4 Mbit/s (quant 6), 41.6 dB @ 2.0 Mbit/s (quant 12); Intel's own file is 1.9 Mbit/s.
  FATE's `indeo5` test sample is not scalable, so its reference is unaffected by the decoder fix.
* **Intel `IR50_32.DLL` (Indeo 5.06)** run natively via `tools/iv5ref` decodes every frame with no error, at all
  quantizers and the sizes above. Intel and FFmpeg decodes of the new streams agree at ≈45–46 dB, the same
  as for Intel's own `intro.avi`. The remaining gap is just Intel's YUV→RGB24 conversion.
  `out/compare.png` shows source | FFmpeg decode | Intel decode side by side.
* Rate: quant 4 ≈ 3.5 Mbit/s for 640x480@15 on `intro.avi` content (the Intel original is 3.6 Mbit/s), 38 dB luma vs. that source.
  The encoder runs at ~60 fps at 640x480.

## Assumptions / open issues

* **24 bits after the GOP header** (FFmpeg: "unknown meaning"): bit 23 = GOP extension follows, bits 19–22 = format
  version (2; Intel's decoder remaps the output through lookup tables for other versions), bits 0–18 = build id
  (5.06 build 44 → `0x102C0F`, which we write; Quick Compress 5.03 `0x11263E`).
* **GOP flag 0x80** and **frame flag 0x04**: always set by Intel; Intel's decoder never uses them (frame flag 0x04 is
  stored and never read, GOP flag 0x80 never tested); copied.
* **Transparency**: Intel's RANGE method is broken in 5.06 (every pixel transparent); `-transparency range` implements
  the documented intent. Works with tiling (mask coded per tile). See ENCODER_FEATURES.md §4 F-TRANS.
* **Sizes**: Intel's decoder refuses streams whose width or height is not a multiple of 4 (`ICM_DECOMPRESS_BEGIN` -201);
  the encoder warns but still writes them (FFmpeg decodes them).
* Scalable synthesis: the reproduced rare ±1 differences belong to Intel's scalar path. The default
  FFmpeg synthesis now matches explicitly selected Intel MMX exactly on the full reference and focused
  partial-height clips. `-scalable_synthesis scalar` now reproduces the packed arithmetic exactly; see `SCALAR_PARITY.md`.
* Scalable quality: HH encoding is now enabled by default, removing the previous
  diagonal-detail loss. Coarse high-band matrices still affect quality; the old
  ~42 dB observation applied to HH-disabled encoding, not a format restriction.
* FFmpeg's decoder synthesises scalable frames with transparency only inside the bounding box of the transparency
  rectangles, as Intel's decoder does (`recompose_transp_rect()` in `ivi.c`).
* Intel's scalar and MMX synthesis paths are now explicitly testable with a trailing `mmx=0|1` in
  `iv5ref`. Automatic selection in this harness used scalar on this host. No claim about CPU selection
  on every Windows host follows from that result.
* `iv5ref -v` installs diagnostic hooks (internal status codes, band decode/skip logging) into the DLL image in memory.
* RD optimisation is implemented: trellis quantization and intra/inter/zero-motion/skip MB decisions (`-rdo`). See `RDO_RESULTS.md` for measurements, CPU cost and rate-control limitations. This is an independent implementation, not Intel's exact search algorithm.
* `tools/iv5ref` implements only the Win32 calls the decoder touches. Unimplemented calls abort with their name.
  It is a test tool and is never linked into FFmpeg.

## RDO handoff completed (2026-10-12)

Trellis quantization and MB RD decisions are now integrated in both profiles.
RC keeps quantizer brackets, searches fractional AQ in both profiles, and can
restore an RDO scale that reached zero. The added small QC cases also exposed
a pre-existing synthesis boundary mismatch: REA confirmed Intel's viewport is
rounded to 32 pixels. `ivi_dsp.c` now uses that boundary, and motion estimation
avoids padding beyond coded MBs. See `RDO_RESULTS.md` for evidence and results.

Validation: `tools/regress.sh` (73 cases) and `python3 tools/rdo_recovery.py`.

## Current parity boundary

The 5.06 VfW encoder feature roadmap is implemented. Exact Intel algorithms,
compressed bytes, defaults and every decoder edge case are not reproduced.
See `ENCODER_FEATURES.md` section 5, "Remaining differences and verification
limits", for the current outstanding items. Section 4 of that audit is the
historical implementation plan, not an active TODO list.

FFmpeg defaults to quant 6, while Intel quality 8500 maps to quant 4 offline
and 3 in QC; use `-quality 8500` for that mapping. NULL frames require
`-null_frames 1` even in quick mode. At that stage, 73 cases passed, plus the separate RDO-scale recovery test.
Later validation and timing are documented in `ENCODER_REFINEMENTS.md`.

## Scalable synthesis investigation completed (2026-10-12)

The earlier MMX/HH claim was incorrect. Default decoding now uses all four
bands, matching Intel MMX, with an explicit scalar compatibility option.
`intro_01.avi` is exact over 165,196,800 luma samples. Regression comparisons
require zero differences; scalar packed arithmetic is now reproduced
exactly as documented in `SCALAR_PARITY.md`. See `SYNTHESIS_PARITY.md` for evidence and commands.

## Subsequent HH encoding update

Scalable HH coefficients are now encoded by default, matching Intel MMX
playback. `-hh_band 0` preserves the prior common scalar/MMX reconstruction.
The 76-case regression matrix and the focused diagonal-detail check pass.
See `HH_ENCODING.md` for evidence and `THOMAS_ENCODE.md` for the full Thomas
quality/size comparison.

## RDO, rate control and scalar refinements

RDO transform caching and candidate reuse approximately halve CPU time on the
640×480 Thomas sample while preserving identical constant-quantizer output.
Debt accounting, initial quantizer search and reference allocation improve
short/low-rate accuracy. NULL packet accounting and range validation are fixed.
Scalar packed rounding is exact on the full reference; QC LL uncoded padding
now uses Intel's DC predictor and empty-tile reference semantics. See
`ENCODER_REFINEMENTS.md` and `SCALAR_PARITY.md` for evidence and commands.

Final refinement validation: 80 compatibility cases on each Intel path
(160 exact comparisons), with zero decoder errors/checksum mismatches/luma
differences; full scalar and MMX reference comparisons are exact.
