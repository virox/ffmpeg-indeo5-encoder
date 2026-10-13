# Bluey S01E02 Indeo5 encode

Source: `/mnt/horreum/media/tv/Bluey Kurzgeschichten (2024) [tmdbid-256953]/Season 01/Bluey Kurzgeschichten S01E02.mkv`. English audio is input stream 2, E-AC-3 5.1 at
48 kHz, 256 kbit/s. Source video is 1920×1080 at 24000/1001 fps.

A centered **1440×1080 crop at x=240, y=0** produces 4:3; it is downscaled
with Lanczos to **640×480**, square pixels, and converted to **30 fps**.
BT.709 YUV is converted to BT.601 for the legacy SD decoder's RGB matrix.
The original source is unchanged.

## Outputs

| English audio | Complete file size (decimal) | File in `out/` |
| --- | ---: | --- |
| EAC3 5.1 | 13.427 MB | `Bluey S01E02 (IV50 640x480 30fps English EAC3 5.1).mkv` |
| MP3 stereo | 11.460 MB | `Bluey S01E02 (IV50 640x480 30fps English MP3 stereo).avi` |
| PCM stereo | 35.017 MB | `Bluey S01E02 (IV50 640x480 30fps English PCM stereo).avi` |

All three files contain identical compressed video. The MKV copies the
original English E-AC-3 packets without transcoding. MP3 is a stereo downmix
at 48 kHz / 128 kbit/s; PCM is a stereo downmix at 48 kHz / signed 16-bit.
The latter two formats are conversions, not the source's audio codec.
Each output's audio packet hash matches its selected source/prepared audio.

## Compression selection

The basic profile outperformed the scalable profile for this animation in
initial profile trials. Further trials used the normalized BT.601 input,
three four-second scenes starting at 20, 60 and 100 seconds, with identical
motion search and NULL-frame settings. Selected quantizer 2 / RDO 2 retains
more color precision than the slightly smaller quantizer 1 / RDO 3 result.

Video options:

```text
-c:v indeo5 -luma_bands 1 -quant 2 -rdo 2 -tables 2
-me_range 60 -null_frames 1 -g 240
```

This uses offline half-pixel search, trellis and reconstructed-error mode
selection, optimized custom entropy tables/run-level maps, the maximum motion
search range, eight-second GOPs and two-byte NULL packets. Droppable frames,
tiles and Quick Compress are disabled. Constant-quantizer encoding lets the
bitrate vary with the scene's detail instead of imposing an unspecified rate.

## Full-episode validation

* 4,030 timeline frames at 30 fps (134.333 seconds of video).
* 17 I frames, 2,731 P frames and 1,282 NULL frames.
* Video payload: 9.068 MB / 540.03 kbit/s.
* Aggregate PSNR vs normalized cropped input: Y 40.907 dB,
  U 47.971 dB, V 47.960 dB.
* Overall SSIM: 0.986009.
* Intel MMX comparison: 1,238,016,000 luma samples, zero differences.
* No decode errors or frame/band checksum mismatches.
* All three final files decode cleanly; video packet hashes are identical.
* Original E-AC-3 audio packet hash: `5ed7580d9a38126bb3d1c35723c6fa0fe827463218071f5c541882afeab61145`.

NULL packets repeat the preceding picture; quality measurements expand them
back onto the full 30 fps timeline. These PSNR/SSIM figures isolate codec loss
at 640×480 yuv410p; they do not include the deliberate crop, scale or chroma
subsampling loss relative to the 1080p source.

## Reproduction

```sh
ffmpeg/ffmpeg -i INPUT.mkv -map 0:v:0 -an -sn \
  -vf 'crop=1440:1080:240:0,scale=640:480:flags=lanczos:in_color_matrix=bt709:out_color_matrix=bt601,fps=30,setsar=1' \
  -pix_fmt yuv410p -f rawvideo out/work/bluey-s01e02-source-601.yuv

ffmpeg/ffmpeg -f rawvideo -pixel_format yuv410p -video_size 640x480 \
  -framerate 30 -i out/work/bluey-s01e02-source-601.yuv -an \
  -c:v indeo5 -luma_bands 1 -quant 2 -rdo 2 -tables 2 -me_range 60 \
  -null_frames 1 -g 240 -colorspace smpte170m -color_range tv video.avi
```

Mux original English audio with `-i video.avi -i INPUT.mkv -map 0:v:0
-map 1:2 -c copy OUTPUT.mkv`. The prepared MP3/WAV files are retained under
`out/work/`; their muxes use `-map 1:a:0 -c copy` into AVI.

Machine-readable results: `out/work/bluey-final-summary.json`.
Final tuning: `out/work/bluey-tuning-601.json`, `bluey-tuning-final.json`.
Logs: `out/work/bluey-final-encode.log`, `bluey-checksums.log`,
`bluey-intel-parity.log`, `bluey-ssim.log`, `bluey-finish.log`.
