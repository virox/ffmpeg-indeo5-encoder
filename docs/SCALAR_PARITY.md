# Exact scalar scalable synthesis

Intel's scalar synthesis packs two biased, scaled coefficients eight band
columns apart into one 32-bit word. Its filter uses ordinary unsigned additions,
subtractions and logical shifts, so carries and borrows cross between the two
16-bit lanes. Applying the mathematically equivalent filter to independent
signed coefficients loses occasional one-level rounding changes.

`ivi_dsp.c` now reconstructs Intel's packed representation (`4 * coefficient +
0x4000`), preserves the original filter's unsigned operation order and uses its
packed clipping sequence. The scalar path continues to omit HH, matching Intel.
The default MMX-compatible synthesis is unchanged.

This follows the scalar three-band inner loop at `0x10032bf8`, within
`FUN_100314e0`. The implementation pairs band columns eight samples apart,
replicates the left/low bottom boundaries, mirrors the high bottom boundary,
preserves the distinct guard bias at each pair crossing a 32-pixel group and
uses the existing 32-pixel synthesis viewport. It duplicates the final visible
column only when that column ends the viewport. Partial visible viewports retain
their coded band padding.

## Evidence

Target: `IR50_32.DLL`, SHA-256
`56760e0ea8c8709f4a0c34bee7289a87188aedd8bddfd05f8f62bee2f3f91238`.
REA/Ghidra scalar synthesis assembly:
`ev_9a663c180a3c624312f5c483ec06a855879dbf427662a6c83b3369befca728c2`.
Saved session: `re/ir50_32.synthesis.rea-snapshot`.
Complete loop disassembly: `out/work/intel-c-synthesis-objdump.txt`.

Static assembly establishes the packing, filter order and clipping.
An independent Python model (`out/work/scalar_model.py`) applied to previously
captured reconstructed bands reproduces all 1,536,000 pixels of the 20-frame
320x240 reproduction exactly, including its former one-level mismatch.
The three reference frames containing the last four right/group-edge failures
also match the independent model exactly (230,400 luma samples).

## Quick Compress padding

Expanded boundary checks identified an existing discrepancy shared by MMX
and scalar decoding at scalable Quick Compress widths such as 176. The last
coded 8x8 MB ends at band column 87, before the 32-pixel synthesis viewport.
Intel initializes the uncoded eight-column horizontal LL guard block with the
current intra DC predictor. This predictor starts at zero per tile, changes
when an intra block updates it and persists across subsequent inter MBs.
High-band padding is zero. FFmpeg previously left its allocated LL guard at zero.

The decoder now applies the band's existing DC transform to that guard block
after each final coded LL MB row, using the exact predictor. Empty LL tiles
copy their guards from the reference alongside the coded samples. The synthesis
filter requires no guessed last-pixel offset or approximate block average.

Runtime capture `out/work/scalar-qc176.bands` establishes the behavior on all
12 I/P frames: frame 0's uncoded LL columns 88 through 95 contain 171 in flat
block rows and -48 in detailed block rows; following P frames start at zero
until intra blocks update the predictor. A separate static five-frame capture
(`out/work/scalar-qc-static.bands`) retains guard -62 on every empty tile.
The production decoder fills the same samples from the actual decoded DC
predictor. Before this fix the QC176 clip differed in 1,205 scalar and 1,206
MMX luma samples, maximum magnitude 44. Afterward both comparisons are exact.

## Validation

* Full `samples/intro_01.avi`: 2,151 frames, 165,196,800 luma samples,
  **zero differences** against Intel scalar (previously 56 one-level changes).
* The same full reference remains exact against Intel MMX.
* Original focused 20-frame clips at 320x240, 176x144, 336x200 and 320x180:
  zero scalar differences.
* Additional 12-frame cases at 32x12, 36x20, 60x28, 64x60, 100x60, 128x68,
  132x60, 176x144 and 320x180: zero scalar differences, covering HH on/off, both compression
  profiles, quantizers 0/6/23 and partial horizontal/vertical viewports.
* Six additional 15-frame QC176 cases (tiled, droppable, static, NULL, tiled
  static and tiled droppable): all 2,280,960 luma samples exact on each path.

Logs: `out/work/scalar-full-parity.log`, `out/work/scalar-focused-parity.log`,
`out/work/scalar-width-parity.log`, `out/work/scalar-qc-extra-parity.log`. Temporary comparison models and band captures
are retained in `out/work/`; the production filter has no capture instrumentation.


```sh
python3 tools/scalable_parity.py samples/intro_01.avi 320 240 --path scalar
python3 tools/scalable_parity.py out/work/bottom-320x240.avi 320 240 --path scalar
python3 tools/scalable_parity.py out/work/bottom-176x144.avi 176 144 --path scalar
python3 tools/scalable_parity.py out/work/bottom-336x200.avi 336 200 --path scalar
python3 tools/scalable_parity.py out/work/bottom-320x180.avi 320 180 --path scalar
```

The reference comparisons cover luma reconstruction, with Intel explicitly
forced to scalar synthesis. They do not claim RGB/color-conversion parity or
exactness for every possible malformed stream or legacy DLL version.

Final expanded suite: **80 cases pass on each path (160 comparisons)**,
including QC176 alpha transparency, tiling, NULL/droppable frames and access
keys. All luma differences and checksum/error counts are zero. Logs:
`out/work/refinements-regress-mmx.log`, `out/work/refinements-regress-scalar.log`.
