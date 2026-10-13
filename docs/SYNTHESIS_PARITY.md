# Scalable synthesis parity (2026-10-12)

## Findings

The previous notes incorrectly described the native harness's output as Intel
MMX output. On this host, the unmodified harness selects Intel's scalar path.
The two paths have different behavior:

| Intel path | HH band | FFmpeg selection | Status |
|---|---|---|---|
| MMX, `FUN_10034340` | included | `-scalable_synthesis mmx` (default) | exact on the full scalable reference sample and focused edge clips |
| Scalar, `FUN_100314e0` | omitted | `-scalable_synthesis scalar` | exact packed arithmetic; see `SCALAR_PARITY.md` |

The synthetic reproductions had one differing sample at 320x240 and three at
176x144, all of magnitude 1; the 336x200 and 320x180 clips were already exact.
The differences were not exclusively on the last row. Capturing the inputs to
Intel's synthesis dispatcher and FFmpeg's reconstructed bands established
that all 1,536,000 band samples in the 20-frame 320x240 reproduction matched
exactly after normalizing Intel's scalar storage (4x scale, 0x4000 bias and
interleaved groups of sixteen words). The discrepancy therefore arose during
scalar synthesis rather than band reconstruction or motion estimation.

The scalar assembly uses paired values in 32-bit arithmetic; MMX uses separate
16-bit lanes. The carry/rounding mechanism is now reproduced with the original unsigned
operation order, guard biases and packed clipping. Both paths are exact on
the full reference; see `SCALAR_PARITY.md` for the later implementation.

Explicitly selecting Intel's MMX path eliminates those reproduced differences.
With HH synthesis restored, all 2,151 frames of `samples/intro_01.avi` match
Intel MMX exactly: **165,196,800 luma samples, zero differences**. Omitting HH
while comparing against true MMX output instead produces differences in
2.589% of that sample's luma pixels, with a maximum difference of 75.

The original scalar comparison found 56 one-level differences out of
165,196,800 samples. The subsequent packed arithmetic implementation eliminates
all of them. Expanded checks also fixed Quick Compress LL guard padding.

## Changes

* The FFmpeg Indeo 5 decoder now includes HH by default, matching Intel MMX.
* `-scalable_synthesis scalar`, placed before `-i`, preserves the previous
  HH-omitting synthesis for comparison with Intel's scalar path.
* `tools/iv5ref/iv5ref` accepts a trailing `mmx=0` or `mmx=1`. Omission retains
  the DLL's automatic selection. The override updates the decoder instance's
  CPU setting before Intel copies it into its global selector, so reconstruction
  and synthesis use a consistent path. It modifies the loaded image only,
  verifies the expected hook prologue, and never writes the original DLL.
* `tools/regress.sh` explicitly selects `mmx=1` by default, or `mmx=0`
  with `IV5_SYNTHESIS=scalar`, and requires a maximum luma difference of
  **zero** on either path, including protected and transparent streams.
* `tools/scalable_parity.py` compares complete clips, checks frame counts and
  decoder errors, expands NULL frames and compares luma in bounded chunks.
* The encoder now writes useful HH by default in the scalable profile, as
  Intel's original encoder does. `-hh_band 0` retains the previous common
  reconstruction for scalar/MMX playback. Scalar decoders omit diagonal
  detail from streams with HH enabled. See `HH_ENCODING.md`.

The filter's existing mirrored bottom boundary and 32-pixel synthesis viewport
boundary are retained. Focused clips with partial band heights now compare
exactly to the explicitly selected MMX path.

## Evidence

Target: `IR50_32.DLL`, SHA-256
`56760e0ea8c8709f4a0c34bee7289a87188aedd8bddfd05f8f62bee2f3f91238`.

REA/Ghidra observations:

* MMX synthesis assembly, `0x10034340`:
  `ev_46bf5bd586a578390a7d2ba252f9458c268040dfe1d66a5315df0a81c9b674e7`.
* Scalar synthesis assembly/dispatch, `0x100314e0`:
  `ev_9a663c180a3c624312f5c483ec06a855879dbf427662a6c83b3369befca728c2`.
* CPU-selector initialization, `0x10034f70`:
  `ev_82aa44fb6840f8de87591cb80f58896adc2a5e423ecd5c603e2a88dcdc204fb0`.

Static analysis establishes the branches and selector copy; actual output and
captured bands establish the runtime comparisons. The scalar decompilation has
an indirect dispatch that does not expose the full inner loop as pseudocode.
The CPU-selection cause on arbitrary Windows hosts was not investigated.
Additional evidence is retained in `re/ir50_32.synthesis.rea-snapshot`.
Temporary band-capture sources and outputs are retained under `out/work/`;
production FFmpeg contains no band-dump instrumentation.

## Reproduction and validation

```sh
python3 tools/scalable_parity.py samples/intro_01.avi 320 240
python3 tools/scalable_parity.py samples/intro_01.avi 320 240 --path scalar
sh tools/regress.sh
python3 tools/rdo_recovery.py
```

Focused edge clips: 320x240, 176x144, 336x200 and 320x180, 20 frames each,
`testsrc2` with scrolling, quant 6, scalable profile, RDO disabled.
Results and reproduction script: `out/work/check_bottom.py`,
`out/work/bottom-reference-parity.log`, `out/work/bottom-scalar-reference.log`.
The complete compatibility log is `out/work/synthesis-regress.log`.

Limits: exactness is established for these clips and the compatibility suite,
not for every possible stream, color conversion or CPU path. Scalar packed
arithmetic quirks remain a separate compatibility detail. Previously reported
bottom-edge differences are superseded by explicitly selected MMX comparisons;
old QC allocation-padding behavior remains a separate investigation topic.

## Later refinements

`SCALAR_PARITY.md` documents exact scalar synthesis and the QC LL DC guard
fix. `ENCODER_REFINEMENTS.md` records RDO speed and improved rate control.
The expanded suite can test either path: `sh tools/regress.sh` for MMX or
`IV5_SYNTHESIS=scalar sh tools/regress.sh` for scalar, both with zero tolerance.
