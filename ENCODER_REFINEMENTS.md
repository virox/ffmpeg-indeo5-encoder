# Encoder refinements

## RDO speed

Repeated transforms are cached per frame for intra, zero-motion and searched
motion candidates. The DC predictor is applied after retrieving the cached
transform, so it still follows the selected coding path. Reference/vector
changes invalidate the relevant cache. Exact zero matrix entries are skipped
in their original summation order. Forced skip candidates need no transform.
The selected candidate's levels and DC state are saved instead of quantizing
it again. A candidate stops once its accumulated cost cannot beat the winner.

The speed changes alone produced **byte-identical AVI files in all 12 cases**
covering both profiles, quantizers 0/6, QC with RDO, intra-only, tiling,
droppable frames, AQ and rate-control trials. The final constant-quantizer
Thomas sample also remains byte-identical after the RC changes.

On 270 frames sampled across Thomas at 640×480, 15 fps, scalable HH enabled,
quantizer 4, RDO 1, GOP 45:

| Version | CPU seconds | Wall seconds | AVI bytes |
| --- | ---: | ---: | ---: |
| Before | 37.87 | 37.94 | 3,137,098 |
| After | 19.23 | 19.22 | 3,137,098 |

Approximately **1.97× faster** with identical output. This is a local sample
measurement, not a guaranteed speedup for every input. The transform caches
cost about 12 bytes per padded band sample (roughly 4 MiB at 640×480), plus
small validity/vector tables. Previously produced Thomas output is unchanged.

## Rate-control accuracy

* Debt is now accounted against the allocation before debt correction.
  Repayment therefore reduces outstanding debt, instead of preserving it and
  depressing subsequent budgets indefinitely.
* The first intra and first inter frame search the configured quantizer range;
  the usual swing limits apply after their quantizers are established. This
  avoids large startup overshoots on short, low-rate clips.
* The initial key/P allocation ratio is 8, then learned from coded frames.
  A useful first reference receives enough bits for subsequent prediction.
* NULL packets are accounted using their actual two-byte size, both for an
  unchanged source and after residual coding selects NULL substitution.
* Invalid `rc_qmin > rc_qmax` is rejected explicitly.

Same 90-frame, 320×240 Thomas input as the prior RDO experiments, 15 fps,
GOP 15. The 15-frame entries are one-second clips. Rate figures are video
payload, excluding AVI/audio. PSNR is aggregate aligned luma error.

| Luma bands | Frames | RDO | Target kbit/s | Before → after kbit/s | Before → after PSNR dB |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 90 | 1 | 150 | 119.11 → 148.48 | 32.572 → 33.348 |
| 1 | 90 | 1 | 300 | 269.33 → 295.51 | 37.103 → 37.649 |
| 1 | 90 | 1 | 900 | 911.62 → 889.33 | 41.070 → 41.277 |
| 1 | 15 | 1 | 300 | 278.59 → 295.26 | 36.806 → 37.469 |
| 1 | 90 | 0 | 300 | 269.35 → 300.27 | 34.066 → 34.154 |
| 4 | 90 | 1 | 150 | 115.23 → 147.66 | 32.308 → 33.296 |
| 4 | 90 | 1 | 300 | 275.89 → 297.10 | 36.501 → 36.577 |
| 4 | 90 | 1 | 900 | 913.43 → 882.91 | 40.025 → 41.844 |
| 4 | 15 | 1 | 300 | 273.06 → 297.54 | 36.477 → 36.752 |
| 4 | 90 | 0 | 300 | 284.19 → 301.51 | 33.722 → 34.172 |

Every tested rate is within 2% of target, including the one-second tests;
measured luma quality improves in all ten comparisons. The input is finite
and budgets remain bounded; qmin/qmax restrictions, an expensive first frame
at qmax, or too little useful detail at qmin can still make a requested rate
unreachable. The encoder does not insert filler to spend unused bits.

`python3 tools/rate_control.py` checks startup search, configured quantizer
bounds, debt repayment, 30 NULL frames in each profile, actual NULL accounting
and invalid-range rejection. `python3 tools/rdo_recovery.py` confirms that RDO
strength can still reach zero and recover after a scene change.

Evidence: `out/work/refinement-speed-check.json`,
`refinement-speed-baseline.time`, `refinement-speed-final2.time`,
`rc-refinement-before.json`, `rc-refinement-final.json`,
`rate-control-check.log`, `rdo-recovery-refined.log`.

## Scalar parity

See `SCALAR_PARITY.md` for the exact paired arithmetic implementation,
REA evidence and validation. The new scalar implementation follows Intel's
unsigned carry, guard bias and clipping order; HH is still omitted, as in
Intel's scalar decoder. The default MMX path continues to include HH.

## Final compatibility checks

The expanded matrix passes all **80 cases on MMX and 80 on scalar**, with
zero luma differences, decode errors or checksum mismatches. This includes
QC176 alpha transparency, the DC guard fix, access keys, tiling, NULL and
droppable frames. HH detail, RC bookkeeping and RDO recovery checks also pass.
The normal binary, checksum-verifying binary and `indeo5enc.patch` are updated.
