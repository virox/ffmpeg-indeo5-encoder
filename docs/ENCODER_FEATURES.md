# Intel Indeo Video 5 encoder: feature and parameter audit

Comparison of the original Intel encoder (`IR50_32.DLL`, Indeo® video **5.06**, build `R.5.06.15.2.44`,
plus its Quick Compress modules `IR50_QC.DLL` / `IR50_QCX.DLL` **5.03**, build `R.5.03.63/64.2.38`)
with our FFmpeg encoder (`libavcodec/indeo5enc.c`).

Status legend for our implementation:

* **Implemented**: present and verified against Intel's decoder
* **Partial**: present in a restricted or different form
* **Missing**: not implemented
* **N/A**: not a bitstream feature, or not applicable to an FFmpeg encoder
* **Unknown**: Intel's behaviour itself is not fully understood yet

Evidence legend:

* **[D]** decompiled code (Ghidra), with function address
* **[X]** experiment: Intel's encoder driven natively via `tools/iv5ref enc` and the result inspected
* **[S]** reference sample streams in `samples/`
* **[R]** DLL resources (dialogs, string table, embedded type library)
* **[H]** Intel documentation (`re/vfw_head.txt` = Intel's `vfw_spec.h` VfW header, interface 1.2;
  `re/indeo_help.txt` = Indeo video interactive help file)
* **[I]** inference, not directly confirmed

---

## 1. How the evidence was gathered

| Source | Location | Notes |
|---|---|---|
| Full decompilation (856 functions) | `re/ir50_32_decomp.c` | Ghidra 12.1.4 headless, `tools/ghidra/DecompileAll.java`; print one function with `tools/fn.sh <addr>` |
| REA session snapshot | `re/ir50_32.rea-snapshot` | REA (Ghidra provider) analysis with the transparency evidence; reopen with `open_binary` + `snapshot_path` |
| Resources | `re/ir50_32_resources.txt` | dialogs, string table, version; extracted with `tools/pe_resources.py` |
| OLE type library | `re/ir50_typelib.tlb`, `re/typelib.bin.txt` | `IIndeoEncodeOA` / `IIndeoDecodeOA` property names and help strings |
| Intel VfW header | `re/vfw_head.htm`, `re/vfw_head.txt` | `R4_ENC_SEQ_DATA`, `R4_DEC_*`, transparency, scalability enums |
| Intel help file | `re/indeo_help.pdf`, `re/indeo_help.txt` | user-level option descriptions and defaults |
| Live encoder experiments | `tools/iv5ref/iv5ref enc …`, `tools/intel_enc.sh`, outputs in `re/enc/*.ivr` | native Linux loader running Intel's encoder (and the Quick Compress DLLs) without Wine |
| Stream analysis | `tools/iv5stat.py` (per-frame headers, summary), `tools/ivr2avi.py`, `out/ffmpeg_mbstats` + `tools/mbstats.py` (macroblock statistics via an instrumented FFmpeg decoder) | |

Reproduce any experiment, e.g. `tools/intel_enc.sh name 320 240 re/enc/in320.bgr dr=40000 kf=15`.

---

## 2. Configuration interfaces of the original encoder

### 2.1 VfW messages handled by `DriverProc` (0x10044fe0) [D]

| Message | Handler | Encoder-relevant behaviour |
|---|---|---|
| `ICM_GETSTATE` 0x5000 | `FUN_100400f0` | size 0x30: private 48-byte config blob (magic `0x12345678`, 12 dwords at `ctx+0x40`); other sizes: R4 structures (`FUN_10040270`) |
| `ICM_SETSTATE` 0x5001 | `FUN_10040150` → `FUN_10040700` | size 0x30: private blob; otherwise R4 structures validated by `FUN_10040380` and dispatched by `mtType` (§2.2) |
| `ICM_GETINFO` 0x5002 | `FUN_1003ff20` | codec info |
| `ICM_CONFIGURE` 0x500A / `ICM_ABOUT` 0x500B | `FUN_10040060` / `FUN_10040240` | dialogs 101/103 (§2.5) |
| `ICM_GETDEFAULTQUALITY` 0x501E | `FUN_10046d60` | returns **8500** [X] |
| `ICM_GETQUALITY` / `ICM_SETQUALITY` 0x501F/0x5020 | `FUN_10046dd0` / `FUN_10046da0` | `ctx+0x20` |
| `ICM_GETDEFAULTKEYFRAMERATE` 0x402A | `FUN_10046d20` | returns **15** [X] |
| `ICM_COMPRESS_GET_FORMAT` 0x4004 | `FUN_10046270` | output `IV50`, biBitCount 24 [X] |
| `ICM_COMPRESS_GET_SIZE` 0x4005 | `FUN_100463c0` | 2.25 × W × H bytes (172800 for 320×240) [X] |
| `ICM_COMPRESS_QUERY` 0x4006 | `FUN_10046450` → `FUN_10047c20` | input format/size validation (§3, F-IN) |
| `ICM_COMPRESS_BEGIN` 0x4007 | `FUN_10046470` | allocates the encoder, loads the Quick Compress DLL if enabled |
| `ICM_COMPRESS` 0x4008 | `FUN_100467d0` | per frame: colour conversion (RGB: `FUN_10036550`), transparency analysis, core encode `FUN_1002b000`; ckid `'iv'`; `AVIIF_KEYFRAME` reported |
| `ICM_COMPRESS_END` 0x4009 | `FUN_10046740` | |
| `ICM_COMPRESS_FRAMES_INFO` 0x4046 | `FUN_10046e10` | sequence parameters (§2.3) |
| `ICM_SET_STATUS_PROC` 0x4048 | `FUN_10047010` | progress callback |
| `ICM_DECOMPRESS*`, `ICM_DRAW*`, `ICM_DECOMPRESSEX*` | various | decoder side, not covered here |

### 2.2 `R4_ENC_SEQ_DATA` (codec-specific state, `mtType` 5 = DEFAULT, 6 = VALUE)

The documented layout [H] is interface **1.2** (0x50 bytes). The DLL implements **interface 1.5** (`dwVersion 0x10005`,
size **0x58**), accepting older versions with their sizes (1.0: 0x4C, 1.1–1.3: 0x50, 1.4–1.5: 0x58) [D `FUN_10040380`].
Setter: `FUN_100473e0`. Defaults (`mtType`=5): `FUN_10047b50`.

| Offset | Field | Flag | Setter behaviour [D] | Effect observed [X] |
|---|---|---|---|---|
| 0x18 | `dwKey` | `ENCSEQ_KEY` (1<<0) | stored at cfg+0x44 if `fEnabledKey` | access key (F-AKEY) |
| 0x1C/0x20 | `dwMinViewportWidth/Height` | `ENCSEQ_MINIMUM_VIEWPORT` (1<<1) | cfg+0x48/0x4C | enables tiling (F-TILE) |
| 0x24 | `rngTransparencyRange` (Blue lo/hi 0x26/27, Green 0x28/29, Red 0x2A/2B) | `ENCSEQ_TRANSPARENCY` (1<<2) | cfg+0x62..0x67 when method = RANGE; used only for the GOP colour, never handed to the colour converter | F-TRANS (RANGE broken) |
| 0x2C | `pbmTransparencyBitmask` | 〃 | stored for TM_BITMASK; requires `oeEnvironment` = 2 (32-bit) | crashed in our harness, Unknown |
| 0x30 | `tmTransparencyMethod` | 〃 | 1 NONE, 2 RANGE, 3 BITMASK, 4 FRAME (first-frame analysis), 5 ALPHA_CHANNEL; other values → error | F-TRANS |
| 0x34 | `fEnabledKey` | `ENCSEQ_KEY` | cfg+0x54 | |
| 0x38 | `fScalability` | `ENCSEQ_SCALABILITY` (1<<3) | cfg+0x58 | 4 wavelet bands + type-2 frames (F-SCAL) |
| 0x3C | `fBiDirPrediction` | `ENCSEQ_BI_DIR_PREDICTION` (1<<4) | **ignored** (no code reads it) | no effect |
| 0x40 | `fRandomKeyAndBiDir` | `ENCSEQ_RANDOM_KEY_AND_BIDIR` (1<<6) | cfg+0x68 | property-page state only; no codec effect (F-RKB) |
| 0x44 | `dwPlaybackPlatform` (PP_LOW/MEDIUM/HIGH; help file: "Quality Good/Better/Best") | `ENCSEQ_PLAYBACK_PLATFORM` (1<<5) | **ignored** | no effect |
| 0x48/0x4A | `wFrameLatency`, `wDeadFrames` | `ENCSEQ_FRAME_INFO` (1<<7) | **rejected** with `ICERR_BADPARAM` (-6) | unsupported |
| 0x4C | `fQuickCompress` (v1.1+) | `ENCSEQ_QUICK_COMPRESS` (1<<8) | cfg+0x5C | switches to IR50_QC(X).DLL (F-QC) |
| 0x50, 0x54 | undocumented (v1.4/1.5) | – | not read by the setter; GETSTATE returns varying data at 0x54 | Unknown |

Other `mtType` values: 1/2 decoder frame data, 3/4 decoder sequence data, 7 `R4_DECODE_INIT_DATA`, and **0x32
(undocumented, size 0x1C)**, which always returns internal status 0x14 (`FUN_10035a10`, a stub) [D].

### 2.3 `ICCOMPRESSFRAMES` / `ICCOMPRESS` fields used [D `FUN_10046e10`, `FUN_100467d0`] [X]

| Field | Stored at | Behaviour |
|---|---|---|
| `lQuality` (0..10000) | cfg+0x20 | sequence quality; **overridden by the per-frame `ICCOMPRESS.dwQuality`** [X] |
| `lDataRate` (bytes/s) | cfg+0x24 | 0 = constant quality; otherwise per-frame rate control (F-RC) |
| `lKeyRate` | cfg+0x1C | 0 = first frame only, 1 = all key frames, N = every N frames [X] |
| `dwRate` / `dwScale` | cfg+0x2C (rate/scale), cfg+0x28 | frame rate for the rate controller |
| `dwOverheadPerFrame` | cfg+0x18 | subtracted from the per-frame budget (dr=40000: 93 KB → 66 KB with overhead 1000) [X] |
| `dwFlags & ICCOMPRESSFRAMES_PADDING` | cfg+0x30 | "CD-ROM padding": **no effect on codec output** [X]; help says don't use it |
| `ICCOMPRESS.dwFlags & ICCOMPRESS_KEYFRAME` | | forces a key frame [X] |
| `ICCOMPRESS.lFrameNum` | ctx+0x10 | a non-consecutive frame number restarts the sequence state |
| `ICCOMPRESS.dwFrameSize` | | **ignored** (no effect, with or without data rate) [X] |
| `ICCOMPRESS.dwQuality` | | per-frame quality, takes precedence [X] |

### 2.4 Private config blob, registry, OLE automation, DirectShow

* **48-byte state** (`ICM_GET/SETSTATE` size 0x30, magic 0x12345678) = the dialog configuration (key, viewport,
  transparency method/range, scalability, quick compress, random key/bidir) [D `FUN_10040150`].
* **Registry** `HKCU\Software\Intel\Indeo\5.0\Decoder`: `Scalability Levels`, `Delta frames on/off`, `Transparency`,
  `Tile Width`, `Tile Height` [D `FUN_10043c30`/`FUN_10043af0`]. These are `R4_DECODE_INIT_DATA` hints, not encoder options.
* **OLE automation** `IIndeoEncodeOA` [R typelib]: `FrameRate`, `KeyFrameInterval`, `DataRate`, `Quality`, `Padding` ("ROM Padding"),
  `MinViewportWidth/Height`, `TransparencyRange{Red,Green,Blue}{Low,High}`, `TransparencyMethod`, `Scalability`,
  `RandomKeyAndBiDir`, `FrameLatency`, `DeadFrames`, `Key`, `EnabledKey`. Same set as VfW, exposed to DirectShow apps.
* **DirectShow filters / property pages** [R strings 39–55]: Compression Filter, Real Time Compressor ("RTE"),
  Encode Sequence / Encode Parameters / About pages. The RTE path is licence-gated via `ir50_lcs.dll`
  (`SetRTEKey`, magic 0x80719708, `FUN_100460f0`) [D].

### 2.5 User interface (dialog 101 "Encoder Configuration", dialog 107 property page) [R]

Scalability (checkbox), Transparency (combo: **None / First Frame Analysis / Alpha Channel**, strings 19–21),
Viewport Size (combo incl. "Full image"), Access Key (Enable + Value), Quick Compress, Load Default.
Dialog 110 (Intel-only test page, "for Intel testing purposes only"): Key Frame Interval, Data Rate, Quality, Frame Rate.
Defaults [H]: quick compress off, scalability off, bidir off, transparency None, quality "Best", access key off, no viewport.

---

## 3. Master feature comparison

| ID | Feature | Intel behaviour (values, defaults, dependencies) | Bitstream impact | Evidence | Our implementation | Status |
|---|---|---|---|---|---|---|
| F-KEY | Key (intra) frames | Always produced; GOP header on every key frame; `frame_num` restarts at 0 | type 0 + GOP header | [X][S] | key frames with GOP header, `frame_num` restarts at 0 | **Implemented** |
| F-KINT | Key-frame interval | `lKeyRate`, default **15**; 0 = only first; forced via `ICCOMPRESS_KEYFRAME` | frame-type sequence | [X] | `-g` (default 15), forced key frames honoured | **Implemented** |
| F-P | Inter (P) frames, reference | type 1, with motion compensation, intra/inter/empty MBs | type 1 frames, MB info with MVs | [X][S] | P frames in both profiles, decoder-exact closed-loop reconstruction | **Implemented** |
| F-PD | Droppable P frames | Offline encoder alternates **P (type 1) / droppable P (type 3)**: `I d P d P …`; the droppable ones are never referenced | type 3 | [X] | `-droppable 1` | **Implemented** |
| F-PS | Scalable droppable frames | With scalability: `I S S P S S P…` (type 2 = INTER_SCAL) | type 2 | [X][S] | `-luma_bands 4 -droppable 1`, with the decoder's 3-buffer reference rules | **Implemented** |
| F-NULL | NULL frames | Produced by **Quick Compress** for unchanged content (type 4, no data), in the basic profile only (static clip: `I N P N P…`; scalable: `I S S P…`, no NULL frames); offline encoder never emits them. Intel's decoder accepts them in scalable streams too | type 4 | [X] | `-null_frames 1`, both profiles; NULL frames don't change the decoder's buffer state, so the reference choice of the next frame skips them | **Implemented** |
| F-BIDIR | Bidirectional prediction (B frames) | Flag accepted but **ignored** by 5.06; help file describes it (Indeo video interactive), no B frame type in IV50 | none | [D][X][H] | – | **N/A** (not an IV5 feature) |
| F-ME | Motion estimation | Half-pel in all offline modes (GOP descriptor halfpel=1); observed MV range ±23 half-pel (±11.5 px) full size, ±17 in the scalable LL band; chroma bands inherit luma MVs (band flag 0x02); Quick Compress: full-pel, ±14 px | MB MVs, halfpel bits, inherit flags | [X][S] (`mbstats`) | candidate + grid + diamond full-pel search, half-pel refinement with the decoder's interpolation, `-me_range` (default 24 half-pel); chroma inherits MVs; vectors validated for luma and derived chroma | **Implemented** |
| F-MBT | MB types in P frames | intra MBs inside P frames (~7% offline), empty/skip MBs, cbp per 8x8 block | MB info | [X] | RD intra/inter/zero-motion/skip decisions with trellis (`-rdo`); SAD path when disabled, cbp per block | **Implemented** |
| F-Q | Quality → quantizer | offline: `quant = ceil(23 × (10000 − quality) / 10000)`, Quick Compress: `floor(…)` (12-point sweep each, per-frame `dwQuality`); default quality 8500 → quant 4 (QC: 3); same quant for all bands | `glob_quant` per band | [X] | `-quality 0..10000` (Intel mapping of the selected profile) or `-quant` | **Implemented** |
| F-RC | Data-rate control | `lDataRate` bytes/s; per-frame quantizer (all bands same) tracking the target closely (40 kB/s → 46.5 kB/s, 100 kB/s → 103 kB/s), saturates at 23; honours `dwOverheadPerFrame`; ignores `dwFrameSize` | `glob_quant` varies per frame | [X] | `-b:v`: initial encode plus up to 3 bracketed quantizer trials, optional best-state restoration and fractional AQ trials; key/P budget split learnt from sizes; bounded buffer (0.15 s credit / 2 s debt), ±2/±3 quantizer swing limit. Accuracy depends on content and quantizer limits; see `RDO_RESULTS.md` | **Implemented** |
| F-AQ | Adaptive (per-MB) quantization | Enabled **only with data-rate control**: luma band 0 codes the deltas (band flag 0x04), all other bands **inherit** them (band flags 0x04+0x08: `0xCE`/`0xDE`), frame flag 0x08 = a delta for every luma MB (also empty / cbp=0 ones, so that inheriting bands get one everywhere); also in `intro_01.avi` (scalable), `indeo5.avi`, `sample.avi` | MB `q_delta` VLC, frame/band flags | [X][S] | `-aq_strength` and rate control, both profiles, same structure (frame flag 0x08, inherited deltas in all other bands) | **Implemented** |
| F-SCENE | Scene-cut handling | **No automatic key frame** at cuts; the cut frame becomes a P frame with many intra MBs | – | [X] | same: intra MBs inside the P frame | **Implemented** (as Intel) |
| F-HUFF | Custom Huffman codebooks | Per band and per frame: one of 8 static block codebooks **or an explicit custom descriptor** (sel 7); also custom **MB codebooks** (frame header) | band/frame header | [X][S] | static 0..6, default 7 (flag 0x80 clear) or optimal custom descriptor (DP) for block and MB codebooks (`-tables`) | **Implemented** |
| F-RVMAP | rvmap selection + correction pairs | Per band: rvmap 0..7 or default (8), plus up to 61 **swap pairs** (band flag 0x10) to adapt symbol order. **Intel's decoder rejects swaps that move the EOB or escape symbol** (FFmpeg accepts them) [X] | band header | [X][S] | rvmap 0..8 + greedy swap pairs (never EOB/escape) | **Implemented** |
| F-CSUM | Frame and band checksums | Always (frame flag 0x10, band checksum bit); QC: none | header fields | [X][S] | yes, bit-exact | **Implemented** |
| F-SIZES | Band sizes, picture size, padding | frame flags 0x80/0x01; size = `(end + 8) & ~3` | header fields | [D][X] | yes | **Implemented** |
| F-GOPSZ | GOP header size field | gop flag 0x01, always | header | [S][X] | yes | **Implemented** |
| F-PSI | Standard picture-size index | 15 table sizes or explicit 13-bit W/H | GOP header | [X] | yes | **Implemented** |
| F-STAMP | 24 bits after the GOP header | bit 23: GOP extension words follow; bits 19–22: **format version**, 2 for both 5.06 encoders (`FUN_10023310` → ctx+0xbf); Intel's decoder remaps the output planes through lookup tables for version <2 (mode 0) or >2 (mode 1) (`FUN_10035710`), not for 2; bits 0–18: build id (offline 5.06 `0x02C0F` = build 44 / 15, QC 5.03 `0x1263E`, bit 16 set) | GOP header | [D][X][S] | `0x102C0F` (`-quick`: `0x11263E`) | **Implemented** |
| F-FF04 | Frame flag 0x04 (and GOP flag 0x80) | always set by both encoders; Intel's decoder stores frame flag 0x04 in ctx+0x124 and never reads it, GOP flag 0x80 is never tested (`FUN_10023310`) | frame/GOP header | [D][X][S] | set (as Intel) | **Implemented** (no effect in any decoder) |
| F-UNK3 | 3 bits after the MB codebook | per-plane "no clipping needed" hint (4 = Y, 2 = V, 1 = U; `FUN_10023310` → ctx+0xe8, used in `FUN_10034040` to pick the output routine without the clipping table, `FUN_10030fc0` instead of `FUN_100310d0`). Intel's offline encoder sets 1 on some frames | picture header | [D][X] | set per single-band plane whose reconstruction stays within −128..127 (forcing it wrongly makes Intel's decoder wrap, verified) | **Implemented** |
| F-SCAL | Scalability (wavelet) | `fScalability`, default off; 4 luma bands (5/3 wavelet), chroma 8x8 MB / 4x4 blocks; offline: luma 16x16 MB / 8x8 blocks; QC: luma MB = block (8x8), chroma MB = block (4x4) | GOP descriptors, 6 bands | [X][S] | `-luma_bands 4` with I, P and type-2 frames, both MB layouts (`-quick 1` gives QC's); HH encoded by default; `-hh_band 0` for scalar/MMX common reconstruction | **Implemented** |
| F-TILE | Tiling / local decode (minimum viewport) | `dwMinViewport{Width,Height}`, default 0×0 (off); sets GOP flag 0x40 with tile size 64/128/256 chosen from the viewport size (vp 64 → 64, vp 128 → 128); enables decoding of sub-rectangles | GOP flag + tile size, multiple tiles per band, empty tiles | [D][X][H] | `-tile_size 64/128/256`, both profiles, also with transparency; empty tiles (all MBs empty with zero vectors) | **Implemented** |
| F-TRANS | Transparency | `tmTransparencyMethod`: NONE (default) / RANGE (RGB min/max; **broken in 5.06**: the range never reaches the colour converter, every pixel comes out transparent) / BITMASK (1 bpp mask from the app) / FRAME (first-frame analysis: key range from the histogram of frame 0, frame 1 forced to a key frame) / ALPHA_CHANNEL (32-bit input only). GOP flag 0x08 + 24-bit colour (`B<<16\|G<<8\|R`, middle of the key range, else 0x808080) and a transparency plane after the bands, coded per tile of the GOP tile grid (format in §5). Transparent pixels are filled per 4x4 block before coding | GOP flag 0x08, colour, transparency plane | [D][X][H] | `-transparency alpha` (yuva420p), `range` (`-transparency_lo/_hi`, bgr24, the intended semantics), `first_frame` (bgr24); Intel's 4x4 fill; with and without tiling. Masks, colour and tile layout bit-identical to Intel's encoder. BITMASK = alpha input in FFmpeg | **Implemented** |
| F-AKEY | Access key (copy protection) | `fEnabledKey` + `dwKey` (32-bit), default off; GOP flag 0x20 + **32-bit lock word** (depends on key and frame checksum); on key frames the first ≤64 bytes after the first luma band header are **scrambled**; P frames unchanged | GOP flag, lock word, scrambled bytes | [D][X] | `-access_key N`, encoder and FFmpeg decoder (which refused such streams before) | **Implemented** |
| F-QC | Quick Compress | `fQuickCompress` (v1.1+), default off; loads `ir50_qcx.dll` (MMX) or `ir50_qc.dll`: full-pel vectors (half-pel flag 0; ±14 px, ±7 band px in the scalable LL band), NULL frames, frame flags 0xCD (all luma MBs carry a q_delta, always 0; no checksums), band flags 0x84 / 0x8E (inherit MV even in key frames), static block and MB codebook 3, default rvmap, stamp `0x11263E`, quant = **floor**(23·(10000−q)/10000) (offline: ceil); scalable: luma MB = 8x8 block, chroma MB = 4x4 block | many (see column) | [D][X][H] | `-quick 1`: all of the above (checksums back on with `-checksum 1`) | **Implemented** |
| F-PP | Playback platform ("Quality Good/Better/Best") | ignored by 5.06 | none | [D][X] | – | **N/A** |
| F-RKB | Random key and bidir | stored (cfg+0x68 → ctx+0x80) and shown in the property page (`FUN_100497f0`), never read by the codec | none | [D][X] | – | **N/A** (no effect in 5.06) |
| F-LAT | Frame latency / dead frames | `ENCSEQ_FRAME_INFO` rejected (-6) | – | [D][X] | – | **N/A** (unsupported in 5.06) |
| F-PAD | CD-ROM padding | flag stored, no codec-level effect; help says don't use it | none | [D][X][H] | – | **N/A** (container-level) |
| F-IN | Input formats | VfW: **RGB24, RGB32, RGB555** accepted; RGB32 X byte ignored unless transparency = alpha; **YVU9 / I420 / YUY2 rejected** (`ICERR_BADFORMAT`) unless the licensed real-time (RTE/DirectShow) path is active; top-down and bottom-up DIBs. RGB → YUV: BT.601 studio-range tables, chroma = 3/5/25-weighted 4x4 average | colour conversion | [X][D `FUN_10047c20`, `FUN_1001a6d0`, `FUN_1001c220`] | any FFmpeg format → yuv410p via swscale; colour-key modes take bgr24 and convert like Intel (BT.601, within 1 LSB of its tables; same 4x4 weighting) | **Implemented** (superset) |
| F-DIM | Dimension limits | W and H multiples of **4**, minimum **32×32**, maximum **4,194,304 pixels** (2048×2048, 4092×1024 OK; one more row fails). **Intel's decoder refuses other sizes** at `ICM_DECOMPRESS_BEGIN` (-201, tested 333x197) | – | [X] | any size 4..8191 (warning outside Intel's limits; FFmpeg decodes them) | **Implemented** (superset) |
| F-YV12 | YV12 picture format (GOP flag 0x02) | never produced by the VfW encoder (YUV input rejected); FFmpeg's decoder refuses it | GOP flag | [X] | – | **Unknown** (possibly RTE only) |
| F-GOPEXT | GOP extension words | present in `sample.avi` (older encoder, e.g. `0x9302 0x1403`), never emitted by 5.06 | GOP header | [S][X] | – | **Unknown** |
| F-EXT | Picture/band header extensions (flags 0x20) | never emitted | – | [X][S] | – | **N/A** |
| F-HDRV | Header versions / interface | R4 interface 1.5 | – | [D] | – | **N/A** |

### 3.1 Bitstream syntax coverage (what each encoder actually writes)

| Syntax element | Intel offline 5.06 | Intel Quick Compress 5.03 | Ours |
|---|---|---|---|
| Frame types | 0, 1, 2 (scalable), 3 | 0, 1, 2, 3, 4 | 0, 1, 2, 3, 4 |
| GOP flags | 0x01, 0x08 (transp.), 0x20 (key), 0x40 (tiles), 0x80 | 0x01, 0x80 | 0x01, 0x08, 0x20, 0x40, 0x80 |
| Luma bands / chroma bands | 1 or 4 / 1 | 1 or 4 / 1 | 1 or 4 / 1 |
| Luma MB/block | 16/8 | 16/8 (scalable: 8/8) | 16/8 (`-quick` scalable: 8/8) |
| Halfpel flag | 1 | 0 | 1 (`-quick`: 0) |
| Frame flags | 0xD5, 0xDD (with AQ: 0x08 = q_delta for every luma MB) | 0xCD | 0xD5, 0xDD (AQ), 0x95 (default MB codebook), without 0x10 for `-checksum 0` |
| MB codebook | static 0..6 or custom | static 3 | static 0..6, default or custom (`-quick`: static 3) |
| Band flags seen | 0x80/0x40/0x10/0x04/0x02 combinations (0xD0, 0xD4, 0xC2, 0xD2, 0xDE, 0x82, 0x92) | 0x84, 0x8E | 0x80/0x40/0x10/0x04/0x02 combinations |
| Block codebook | static 0..6 or custom | static 3 | static 0..6, default or custom (`-quick`: static 3) |
| rvmap | 0..7, default, + corrections | default (8) | 0..7, default, + corrections (`-quick`: default) |
| Quant | quality/RC-driven, AQ deltas with RC | quality-driven | quant/quality/RC-driven, AQ deltas |
| Checksums | yes | no | yes (`-quick`: no) |
| Transparency plane | yes (if enabled) | – | yes (alpha, range, first frame) |

---

## 4. Historical implementation plan and reverse-engineering notes

The work/dependency lists below record the original plan and are not outstanding tasks. All roadmap items are now implemented; sections 5 and 7 describe current status. The transparency findings remain useful reference material. Each original item lists its proposed work, dependencies and validation. Validation always uses three references:
FFmpeg's decoder, Intel's decoder through `tools/iv5ref` (bit-exact luma via `I420:12` output + `tools/cmp_luma.py`),
and the matching Intel encode through `tools/iv5ref enc` / `tools/intel_enc.sh`.

### F-STAMP: version stamp
* **Work:** write `0x102C0F` (5.06 offline) for both profiles; optionally an option to emit other observed stamps.
* **Validation:** header comparison with `tools/iv5stat.py`; Intel decoder still decodes (it did with `0x10190F`).

### F-Q: quality scale
* **Work:** map FFmpeg `global_quality` / `-q:v` (or a `quality` option 0..10000) via `quant = ceil(23·(10000−q)/10000)`; default quality 8500 (quant 4).
* **Validation:** same `glob_quant` as Intel for the same quality ([X] table).

### F-RVMAP / F-HUFF: corrections and custom codebooks
* **Work:**
  * **rvmap corrections:** greedy swap pairs (up to 61) that move frequent (run,level) pairs onto short codes. Already have symbol histograms (`choose_tables`).
  * **Custom codebooks:** search over IVI Huffman descriptors (rows of xbits, ≤ 13 bits, ≤ 16 rows) minimising coded size; encode as selector 7 + descriptor.
  * **MB codebook:** the same idea once MB-level symbols exist (P frames, quant deltas).
* **Dependencies:** none for block data; the MB codebook is only useful with F-P / F-AQ.
* **Validation:** bit savings vs. now; decoders accept the descriptors (FFmpeg rebuilds the VLC; Intel decode bit-exact). Compare the descriptors Intel picks for the same content.

### F-P (+ F-PD, F-MBT, F-ME, F-KINT, F-SCENE): inter frames
* **Work:**
  1. Reference handling identical to the decoder: buffer switching rules (`switch_buffers`); type 1 updates the reference, type 3 doesn't.
  2. MB decision per 16x16 MB: intra / inter with MV / empty (skip), cbp per 8x8 block.
  3. MV coding: deltas vs the previous MB in the row (`mv_x += delta`), MB codebook; half-pel interpolation exactly as `ff_ivi_mc_*` (the encoder must reconstruct like the decoder).
  4. Inter quantization tables (`ivi5_*_inter`), no DC prediction for inter blocks, residual = source − prediction.
  5. Chroma: inherit luma MVs (band flag 0x02), scaled by `mv_scale`.
  6. Motion search: full-pel ±12 then half-pel refinement (matches Intel's observed ±23 half-pel range).
  7. GOP structure: key interval (`-g`, default 15) and Intel's `I d P d P` alternation option.
* **Dependencies:** none, but it's the largest item. Closed-loop reconstruction is needed (already have the decoder-exact intra path; extend with MC).
* **Validation:**
  * Exact reconstruction match: band checksums vs `ffmpeg_dbg` (DEBUG checksum compare), as done for intra.
  * Intel decoder bit-exact luma.
  * Efficiency vs Intel: Intel ~20–25% fewer bits than our intra encoder at equal PSNR [X benchmark], so expect at least that.

### F-RC + F-AQ: rate control and adaptive quantization
* **Work:**
  * Map `-b:v` (FFmpeg bit rate) to Intel's bytes/s semantics.
  * Per-frame quant from a buffer model (Intel's model isn't reverse engineered: RE `FUN_1002b000` callees when implementing); honour frame-type budgets (I vs P vs droppable).
  * AQ: per-MB `q_delta` (band flag 0x04, frame flag 0x08) driven by activity, coded with the MB codebook.
* **Dependencies:** best with F-P; AQ needs the MB codebook (F-HUFF).
* **Validation:**
  * Hit target rates within a few % (Intel: 40 kB/s → 46.5 kB/s).
  * Compare quant traces with Intel's on the same clip.
  * Decoders accept q_delta (FFmpeg already decodes Intel's AQ streams).

### F-SCAL (inter part) and F-PS
* **Work:** scalable P frames (type 1 and droppable type 2 pattern `I S S P`), MVs coded in the LL band and inherited by the other bands (band flags 0x02/0x08). HH now encoded by default; disable with `-hh_band 0` for matching scalar/MMX reconstruction. The earlier claim that MMX ignores HH was incorrect.
* **Dependencies:** F-P.
* **Validation:** as F-P plus the scalable decoder fixes already in `ivi_dsp.c`.

### F-TILE: tiling / minimum viewport
* **Work:**
  * GOP flag 0x40 + tile size code (64 << n); split each band into tiles (`ff_ivi_init_tiles` semantics), per-tile data size, DC prediction reset per tile (already per tile in our code).
  * Empty-tile coding for P frames (`is_empty` bit).
  * Mapping from viewport size to tile size: Intel picks the tile size from the viewport (64 → 64, 128 → 128). RE the exact rule for other sizes.
* **Dependencies:** none for intra; empty tiles need F-P.
* **Validation:** FFmpeg + Intel decode; compare GOP headers with Intel's for the same viewport settings.

### F-TRANS: transparency (done)
* Plane format: §5. Mask sources: alpha channel (yuva420p), colour range and first-frame analysis (bgr24).
* **Encoder side of IR50_32.DLL** (REA/Ghidra):
  * `FUN_10047580`/`FUN_100476f0` (compress begin): methods 2–5 enable the converter's transparency path;
    GOP colour = middle of the configured range for RANGE, else 0x808080.
  * `FUN_10047970`: FRAME arms the analysis on the first frame of a sequence (`FUN_100369d0`);
    `FUN_100479f0` forces a key frame on the frame after it.
  * `FUN_1001a6d0` (RGB → YVU9): FRAME analysis (`FUN_1001c6a0`): per-channel histograms, `thr = W·H/500`,
    `lo` = first level where the cumulative count reaches `thr`, `hi` likewise from 255 down;
    `hi' = min(255, hi + (hi−lo)/2)`, `lo' = max(0, lo − (hi'−lo)/2)`. A pixel is transparent when B, G and R
    all lie in their ranges. The GOP colour becomes the range middle, `B<<16 | G<<8 | R`.
  * RANGE in 5.06: the configured range (cfg+0x62..0x67) is stored and used for the GOP colour, but never copied
    into the converter, which then produces an all-transparent mask [X: every frame 0 opaque pixels]. We implement
    the documented intent instead.
  * `FUN_1001c220` (4x4 fill): chroma = 3/5/25-weighted average of the opaque pixels (weights handed out to the
    opaque pixels in turn), transparent luma in mixed blocks = mean opaque luma, fully transparent blocks = fill
    luma (starts at 128, follows the frame's mean opaque luma when it is ≥ 24 away and ≥ 1/64 of the pixels are opaque).
* **Decoder side**: Intel's scalable synthesis with transparency runs only inside the union bounding box of the
  rectangles (`FUN_10050c20` → `FUN_1003eaf0` → `FUN_10034040`): rows from `y & ~1`, columns widened to multiples of
  32, box borders treated as picture edges. FFmpeg's decoder now does the same (`recompose_transp_rect()` in `ivi.c`).
* **Validation:** `first_frame` masks and colour bit-identical to Intel's encoder on the same clip; Intel's decoder
  reproduces every mask; opaque luma bit-exact between FFmpeg and Intel in all regression cases (both profiles).
* **Tiling** (`FUN_10050c20` tile loop, `FUN_10050790`, grid from the picture/GOP header parse `FUN_10023310`, ctx+0xd0..0xdc):
  the plane is coded per tile of `tile_size` **pixels** (both profiles; the whole picture without tiling; a picture
  smaller than the tile gives one tile of its size), in raster order. The last column ends at the picture width
  rounded up to 32, the last row at the picture height. Tile x must be a multiple of 32. Each tile's RLE covers its
  rows of `FFALIGN(tile_w, 32)` pixels; the row XOR restarts at the first row of each tile. Uniform tiles (`1 v`)
  are packed bit by bit; a sized tile's size counts from the byte where the current bit run began (so the bits of
  uniform tiles before it are included) to the end of its runs, and the next tile starts byte aligned. The decoder
  skips tiles outside the requested decode region (sized tiles by their size).
* **Validation (tiling):** tiles 64/128/256 in both profiles: masks and per-tile layout/sizes identical to Intel's
  encoder (`tm=4 vpw=vph=T`), Intel's decoder returns the same masks.
* **Open:** BITMASK crash in the harness (irrelevant for FFmpeg).

### F-AKEY: access key
* **Work:** RE the lock word and scrambling: lock changes per key frame; ~64 bytes of the band-0 header (rvmap corrections) are scrambled. Find the encoder routine that writes GOP flag 0x20, starting from the bytes in `re/enc/akey.ivr` vs `re/enc/default.ivr`.
* **Dependencies:** F-RVMAP corrections (the scrambled area).
* **Validation:** Intel decoder with the key (needs `R4_DEC_SEQ_DATA` KEY support in `iv5ref`) and without it. FFmpeg would also need decoder support, a separate project.

### F-QC: Quick Compress profile
* **Work:** a "fast" profile mimicking IR50_QC: full-pel ME, NULL frames for static content, fixed codebook 3 and default rvmap, MB qdelta signalled (frame flags 0xCD), no checksums, stamp `0x11263E`.
* **Dependencies:** F-P.
* **Validation:** header comparison against `re/enc/qc*.ivr`.

---

## 5. Prioritised roadmap

### Progress (2026-10-12)

| # | Item | Status | Result |
|---|---|---|---|
| 1 | version stamp | done | `0x102C0F` in both profiles |
| 2 | quality scale | done | `-quality` with Intel's mapping |
| 3 | rvmap corrections + custom codebooks | done | ~1.1% smaller intra frames on Thomas footage; Intel rejects EOB/escape swaps (avoided); fixed a latent bug (codebook selector 7 means "custom", not static table 7) |
| 4–6 | P frames, half-pel ME, chroma inheritance, droppable frames | done (basic profile) | Thomas 20 s segment: quant 4 → 2.49 Mbit/s / 41.2 dB vs intra-only 5.24 Mbit/s / 40.8 dB; on par with or better than Intel's encoder (Intel q9500: 2.53 Mbit/s / 40.4 dB) |
| 7 | rate control | done | `-b:v`, −1…+6% accuracy, undershoots when the 0↔1 quantizer step makes the target unreachable |
| 8 | per-MB quantizer deltas (AQ) | done | `-aq_strength` (activity based, luma; original option name was `-aq`); rate control uses a fraction of −1 MBs to fill quantizer gaps; RC now −5…+2.4% |
| 9 | scalable P frames | done | `-luma_bands 4` with P frames; `-droppable 1` gives Intel's `I S S P` (type 2) pattern with the decoder's 3-buffer reference rules |
| 10 | tiling | done | `-tile_size 64/128/256` (GOP flag 0x40), both profiles; empty-tile coding deliberately not used |
| 11 | NULL frames / quick profile | done | `-null_frames 1` (unchanged source → 2-byte type-4 frame, basic profile only), `-quick 1` (full-pel ME ±14 px, static tables) |
| 12 | transparency | done | format fully reverse engineered (IR50_32.DLL FUN_10050c20 / FUN_10050790, see below) and verified: Intel's decoder reproduces the source alpha mask exactly; `-transparency 1` (yuva420p input, alpha ≥ 128 = opaque) |
| 13 | access key | done | lock word formula and band scrambling reverse engineered (FUN_10023310 / FUN_1001e020), verified on Intel's own protected streams; `-access_key N` in the encoder **and the FFmpeg decoder** (which previously refused protected clips). The FFmpeg sample `miss_congeniality_cryptedindeo5_sbcaudio.avi` was opened by exhaustive key search: key **0** (290 keys pass the 32-bit lock check, only 0 decodes) |
| 14 | colour-key transparency | done | Intel's encoder-side transparency reverse engineered with REA (§4 F-TRANS): `-transparency range` (`-transparency_lo/_hi`) and `first_frame` (bgr24 input), Intel's 4x4 transparent-pixel fill for all modes, GOP colour from the key range. `first_frame` masks/colour bit-identical to Intel's encoder. Found: RANGE is broken in 5.06 (all transparent). FFmpeg's decoder now synthesises scalable transparent frames like Intel (bounding-box region), so opaque luma is bit-exact in both decoders. Intel's decoder refuses non-multiple-of-4 sizes |
| 15 | transparency with tiling | done | per-tile plane layout reverse engineered with REA (`FUN_10050c20` tile loop, `FUN_10050790` tile RLE, grid from `FUN_10023310`); tiles 64/128/256 give masks, tile layout and sizes identical to Intel's encoder; `iv5trans.py` parses tiled planes, `regress.sh` now also checks Intel's decoded masks (`tools/cmp_mask.py`) |
| 16 | empty tiles | done | Intel's decoder (`FUN_10025320`) **copies** an empty tile from the reference without motion compensation, also in MV-inheriting bands, and always zeroes the vectors of luma band 0 MBs; the encoder therefore marks a tile empty only if all its MBs are empty with zero vectors. The size of the next coded tile counts the preceding `is_empty` bits. FFmpeg's decoder changed to Intel's semantics (it motion-compensated with inherited vectors and kept stale luma vectors when quant deltas were present). `tools/build_dbg.sh` rebuilds `out/ffmpeg_dbg` (checksum-verifying decoder) |
| 17 | scalable NULL frames | done | Intel's decoder accepts NULL frames in scalable streams (Intel's QC never writes them there); FFmpeg no longer rejects them (Indeo 5). NULL frames are invisible to the buffer switching (`switch_buffers()` acts on the previous frame's type), so `S N S` continues the type-2 chain: the encoder's reference choice now skips NULL frames. Found and fixed two more FFmpeg empty-tile bugs on the way: the copy covered only the visible tile instead of whole MBs (Intel copies MBs; stale padding was read by later MC in the scalable profile), and empty luma MBs got `q_delta = glob_quant`, doubling the quantizer of inheriting bands. `regress.sh` now compares NULL-frame streams too (`tools/expand_null.py` repeats FFmpeg's last picture like Intel). Intel's QC streams with NULL frames decode bit-exact in FFmpeg |
| 18 | Intel-style AQ, frame flag 0x08, clip hint | done | AQ in both profiles with Intel's structure: luma band 0 sends a delta for every MB (frame flag 0x08) and every other band inherits it (band flags 0x0C), so chroma and the scalable high bands are quantized adaptively too; an empty luma tile resets its deltas, so it is only used when they are 0. The "unknown" 3 bits after the MB codebook are a per-plane no-clipping hint (REA: `FUN_10023310`, `FUN_10034040`, `FUN_10030fc0` vs `FUN_100310d0`), now set when safe |
| 19 | Quick Compress parity | done | `-quick 1` now writes what IR50_QC(X).DLL writes: half-pel flag 0 with full-pel vectors (kept in half-pels internally; inherited chroma vectors derived from the coded full-pel luma vector, as the decoder does), frame flags 0xCD with zero deltas on every luma MB, band flags 0x84/0x8E, static block/MB codebook 3, default rvmap, no checksums (`-checksum` defaults to auto), stamp `0x11263E`, QC's floor quality mapping, scalable 8x8 luma / 4x4 chroma MBs, ±7 band px in the scalable LL band. Headers and MB statistics side by side with Intel's QC output match |
| 20 | HH synthesis investigation | corrected by item 23 | Earlier tests exercised Intel scalar synthesis, which omits HH; the previous description of this as MMX behavior was wrong. MMX includes HH, and default FFmpeg decoding now does too. |
| 21 | header unknowns | done | REA on the picture/GOP parser `FUN_10023310`: frame flag 0x04 is write-only (ctx+0x124) and GOP flag 0x80 never tested (no effect); the 24 bits after the GOP header = extension flag (bit 23), format version (bits 19–22, 2; other versions get a lookup-table remap of the output in `FUN_10035710`) and build id; `fRandomKeyAndBiDir` only reaches the property page. GOP flag 0x04 (never written by 5.06) sets a decoder state bit and is rejected on some frame types; GOP extension words still need an older DLL |
| 22 | RD optimisation | done | `-rdo` enables trellis and reconstructed-distortion MB decisions across inheriting bands; 1 by default offline, 0 in QC. RC quantizer bracketing, fractional AQ search and RDO-scale recovery fixed; partial QC MB activity bounds fixed. REA confirmed 32-pixel synthesis viewport boundaries, now used by FFmpeg; motion references stay inside coded MB padding. See `RDO_RESULTS.md` for measurements and limitations. |
| 23 | exact scalable MMX synthesis | done | Explicit CPU-path selection in the native harness revealed the scalar/MMX distinction. Default FFmpeg synthesis includes HH and matches Intel MMX exactly across all 2,151 `intro_01.avi` frames (165,196,800 luma samples). `-scalable_synthesis scalar` preserves HH-omitting compatibility. Regression tests select MMX and require zero differences. See `SYNTHESIS_PARITY.md`. |
| 24 | nonzero scalable HH encoding | done | Existing wavelet HH coefficients are retained and coded by default. `-hh_band 0` preserves common scalar/MMX reconstruction. All 76 compatibility cases pass with exact Intel MMX luma and clean checksums. See `HH_ENCODING.md`. |
| 25 | RDO speed and rate-control refinements | done | Cached rounded transforms, sparse matrix rows, winning candidate reuse and early candidate rejection preserve constant-quantizer bytes. Corrected debt/NULL accounting and initial key/P search improve short/low-rate accuracy. See `ENCODER_REFINEMENTS.md`. |
| 26 | exact scalar synthesis and QC LL guard | done | Packed carry/rounding/clipping order matches Intel scalar exactly on the full 165,196,800-sample reference. QC guard samples use the current intra DC predictor, with reference copying for empty tiles. See `SCALAR_PARITY.md`. |

Fixes made along the way: the AQ option was renamed `-aq_strength` (`-aq` is an ffmpeg CLI option, so the encoder never saw it); the regression suite `tools/regress.sh` covers 80 option combinations for each of Intel's MMX and scalar paths (160 exact comparisons).

### Feature-completeness status

The listed encoder feature roadmap is implemented, including trellis and MB
RD optimisation. This does not imply bit-identical encoding or identical
rate-control/ME algorithms to Intel. Known format/version questions remain
listed below; Current RD speed and rate-control results are documented in
`ENCODER_REFINEMENTS.md`; earlier measurements are in `RDO_RESULTS.md`.

### Remaining differences and verification limits

* **Exact compression behavior:** ME, AQ, rate control and RD search are independent implementations. Compressed bytes, quantizer traces and speed need not match Intel. Short low-rate clips can undershoot; impossible rate targets can overshoot.
* **Defaults:** FFmpeg currently defaults to `-quant 6` with `-quality -1`; Intel's quality 8500 maps to quant 4 offline or 3 in QC. Use `-quality 8500` to match that setting. NULL frames require `-null_frames 1`, including with `-quick 1`.
* **Exact decoded output:** default synthesis matches explicitly selected Intel MMX exactly on the full scalable reference and edge clips; the suite tests MMX and scalar with zero tolerance. Scalar synthesis now reproduces the packed arithmetic exactly on the full reference. QC LL guard padding is filled from the actual DC predictor, or copied for empty tiles. The new encoder avoids motion references beyond coded allocation padding. RGB conversion is not guaranteed to match Intel exactly in every input/output path.
* **Intentional differences:** RANGE transparency implements the documented color-key behavior instead of Intel 5.06's all-transparent bug; scalable HH encoding is enabled by default, with `-hh_band 0` for scalar/MMX common reconstruction. FFmpeg accepts dimensions beyond Intel's limits; this does not make those dimensions playable by Intel.
* **Outside the tested 5.06 VfW feature set:** older GOP extension words, the licensed RTE/YV12 path and undocumented configuration fields remain unresolved. Intel BITMASK and palettized-input experiments still need harness work. Exact scalar synthesis is implemented; remaining legacy-version behavior still needs separate evidence.

These are behavioral differences, quality/performance refinements or investigation gaps. No implemented 5.06 roadmap feature is still marked missing.

### Transparency plane format (decoder FUN_10050c20 / FUN_10050790)

Follows the colour bands when GOP flag 0x08 is set; LSB-first bit order like the rest of the stream.

| Field | Bits | Meaning |
|---|---|---|
| size | 24, if frame flag 0x01 | bytes of the plane including this field |
| nrects | 3 | then nrects × (x, y, w, h), 16 bits each: bounding rectangles of the opaque area |
| xor_rows | 1 | each coded row is XORed with the previous one |
| has_table | 1 | then 4-bit row count + 4-bit xbits (IVI codebook descriptor; xbits ≤ 8, codes ≤ 13 bits) for run lengths |
| byte align | | |
| per tile (luma tile grid) | | `1 v`: whole tile = v · `0 1 size8[/24] (align) RLE`: size in bytes from the start of the current bit run (uniform tiles' bits included) to the end of the runs · `0 0 (align) RLE`. Tiles: `tile_size` pixels in raster order (whole picture without tiling), last column up to the width rounded to 32; RLE rows are `FFALIGN(tile_w, 32)` wide, XOR restarts per tile |
| RLE | | 1 bit start colour, then run-length symbols with alternating colour; symbol 0 = 255 pixels without colour change; rows padded to 32 pixels; 1 = opaque |

### Access key (decoder FUN_10023310 / FUN_1001e020)

* GOP flag 0x20, 32-bit lock word after the GOP header size; requires the frame checksum (frame flag 0x10).
* `a = k & 0xFF; t = (a·256 + 1 + (k>>16)) · ((k & 0xFF00) + 1 + cs); u = t / (t % (a+1) + 1); lock = (u % ((cs & 0xFF00) + 1) + 1) · u` (32-bit unsigned, `cs` = frame checksum).
* Key frames: the first `min(64, (size−4) & ~1)` bytes after the first luma band's flags+size are XORed with 16-bit words of `x = (mult·x + 1) & 0xFFFF`, `x0 = ~k >> 16`, `mult = ((((k>>8)&0xFF) + (k&0xFF)·256) >> 1) + 0x4000) / 200 · 200 + 0x15`.
* Without a key Intel's decoder shows its "invalid access key" picture (mode 7) instead of failing.

All of the above verified: band checksums vs FFmpeg's decoder, zero decode errors, and bit-exact luma
between FFmpeg's and Intel's decoders on the tested clips. Current strict comparisons for both paths are recorded in `SCALAR_PARITY.md`.

### Original plan

| # | Item | Effort | Value | Depends on |
|---|---|---|---|---|
| 1 | F-STAMP: use `0x102C0F` everywhere | trivial | fidelity | – |
| 2 | F-Q: Intel quality scale (`-q:v`/`quality`, default 8500 → quant 4) | small | usability, parity | – |
| 3 | F-RVMAP corrections + F-HUFF custom block codebooks | medium | 3–10% smaller intra frames (est.) | – |
| 4 | F-P core: P frames (type 1), full-pel ME, intra/inter/empty MBs, MV coding, closed-loop reconstruction, `-g` key interval | large | ~20–25%+ smaller files (measured Intel gain) | – |
| 5 | Half-pel ME + chroma MV inheritance | medium | quality per bit | 4 |
| 6 | Droppable frames (type 3) and Intel's `I d P d P` pattern | small | parity, scalability of playback | 4 |
| 7 | F-RC data-rate control (per-frame quant) | medium | needed for bitrate-targeted output (`-b:v`) | 4 (works intra-only too) |
| 8 | F-AQ per-MB quant deltas + MB codebook | medium | parity with Intel RC streams | 7 |
| 9 | Scalable inter frames (type 2, `I S S P`) | medium | `intro_01.avi`-style streams with inter coding | 4, 5 |
| 10 | F-TILE tiling / minimum viewport | medium | local-decode compatibility | (4 for empty tiles) |
| 11 | F-QC quick profile incl. NULL frames | small–medium | speed profile, parity | 4 |
| 12 | F-TRANS transparency (needs RE of the plane format) | large | compositing use cases | RE |
| 13 | F-AKEY access key (needs RE of lock/scrambling) | medium–large | copy protection parity | 3, RE |
| – | F-BIDIR, F-PP, F-LAT, F-PAD | – | not supported or no effect in 5.06 | – |

---

## 6. Open questions (Unknown) and how to resolve them

| Item | Evidence so far | Next step |
|---|---|---|
| R4 v1.5 fields 0x50/0x54 | not read by the setter | xref the 0x58-byte struct users |
| `mtType` 0x32 | stub returning 0x14 | none (no functionality) |
| BITMASK transparency crash in the harness | plane format, RANGE (broken in 5.06) and FRAME resolved (§4 F-TRANS) | fix harness pointer setup (only matters for Intel experiments; FFmpeg uses alpha input) |
| GOP extension words (`sample.avi`) | older encoder only | find an older DLL (5.0x) |
| YV12 GOP flag 0x02 | never produced | RTE path (needs `ir50_lcs.dll`) |
| Rate-control and ME algorithms | behaviour measured, code not traced | trace `FUN_1002b000` and callees if exact Intel algorithm replication is desired; F-RC / F-P are already implemented |
| Palettised 8-bit input | not tested properly | harness needs a palette in the BITMAPINFO |

---

## 7. Current implementation summary (for reference)

`libavcodec/indeo5enc.c` currently provides (options: `ffmpeg -h encoder=indeo5`):
* basic (1 luma band) and scalable (4 luma bands, HH enabled by default) layouts; I, P, droppable (type 3 / type 2) and NULL frames
* half-pel motion estimation, intra/inter/empty MBs, chroma MV inheritance, closed-loop decoder-exact reconstruction
* quantizer / Intel quality scale / rate control (`-b:v`), adaptive quantization, trellis and MB RD optimisation (`-rdo`)
* best static or custom block and MB codebooks, rvmap selection with correction pairs
* tiling (64/128/256), transparency (alpha, colour range, first-frame analysis), access key
* frame/band checksums, standard size index, Intel's padding rule and version stamp
* `-quick 1`: Intel's Quick Compress profile (full-pel vectors, static tables, zero deltas, no checksums, QC stamp and quality mapping)

Changes to the FFmpeg decoder: 5/3 synthesis fix (band 1, edge rules), access key support, Intel's
bounding-box synthesis for scalable transparent frames, Intel's empty-tile semantics (copy whole MBs, no MC,
zero vectors and quant delta), NULL frames in scalable streams, path-selectable HH synthesis (included by default for Intel MMX; omitted in scalar compatibility), and 32-pixel-aligned synthesis viewport boundaries.

Verified with `tools/regress.sh` (80 cases per path; transparency cases also check Intel's decoded masks): zero decode errors and checksum mismatches in FFmpeg, zero errors in
Intel's `IR50_32.DLL` with MMX explicitly selected, luma bit-exact with zero-difference tolerance between the two decoders. See `SCALAR_PARITY.md` for exact scalar validation.
Accepted dimensions are broader than Intel's (4..8191); Intel's decoder refuses sizes that are not multiples of 4.
