# Thomas full-episode encode

Output: `out/Thomas the Tank Engine and Friends S05E02 (IV50 HH P-frames).avi`

Source: `sources/Thomas the Tank Engine and Friends S05E02.avi`.
Quality target: the existing `out/Thomas the Tank Engine and Friends S05E02 (IV50).avi`,
verified to contain 4,960 intra frames. Both outputs are 640×480, 15 fps,
4,960 frames (330.667 seconds). The original source is 576×432 at 25 fps;
normalization matches the reference's scale and frame timing.

## Full-episode results

| Measurement | All-keyframe reference | New HH interframe encode |
| --- | ---: | ---: |
| Complete AVI size (decimal MB) | 239.457 | 90.076 |
| Video payload (decimal MB) | 209.845 | 60.464 |
| Video payload bitrate (Mbit/s) | 5.077 | 1.463 |
| Aggregate luma PSNR | 40.423 dB | 41.325 dB |
| Aggregate U PSNR | 42.544 dB | 46.019 dB |
| Aggregate V PSNR | 44.505 dB | 46.682 dB |
| Overall PSNR | 40.670 dB | 41.668 dB |
| Overall SSIM | 0.976983 | 0.981801 |
| Luma PSNR, frame 5th percentile | 37.823 dB | 38.455 dB |
| Frame types | 4,960 I | 111 I + 4,849 P |

The complete file is **62.38% smaller**; video payload is **71.19% smaller**.
Audio is copied directly from the reference: PCM signed 16-bit stereo at
22,050 Hz. The concatenated audio packet SHA-256 is identical:
`edf74d13ce77088ee26f006e91de753967ac0e4ef893bfb519001042976abe9b`.

Quality is measured against the aligned 640×480 yuv410p encoder input, over
all frames, using aggregate sample error for PSNR. The new output has lower
or equal luma MSE on **4,959 of 4,960 frames**, and higher aggregate PSNR in
opening, middle and ending sections. SSIM also improves. These are objective
quality comparisons; the new output is not pixel-identical to the old encode.
No resolution, frame-rate or audio reduction contributes to the size saving.

## Settings and reproduction

Scalable profile with HH enabled, quantizer 4, RDO 1, keyframe interval 45.
Sampled opening and five later sections guided the setting; final measurements
cover the entire episode. Motion/quantization choices remain our encoder's
independent implementation.

```sh
ffmpeg/ffmpeg -v error -y \
  -i 'sources/Thomas the Tank Engine and Friends S05E02.avi' \
  -an -vf scale=640:480 -r 15 -pix_fmt yuv410p \
  -f rawvideo out/work/thomas-source.yuv

ffmpeg/ffmpeg -v warning -y \
  -f rawvideo -pixel_format yuv410p -video_size 640x480 -framerate 15 \
  -i out/work/thomas-source.yuv \
  -i 'out/Thomas the Tank Engine and Friends S05E02 (IV50).avi' \
  -map 0:v:0 -map 1:a:0 -c:v indeo5 -luma_bands 4 -hh_band 1 \
  -quant 4 -rdo 1 -g 45 -c:a copy \
  'out/Thomas the Tank Engine and Friends S05E02 (IV50 HH P-frames).avi'
```

## Validation and evidence

* Both outputs contain exactly 4,960 video frames; audio hashes match.
* Full FFmpeg decoding has no errors. Frame/band checksums all pass.
* Full Intel MMX comparison is exact: **1,523,712,000 luma samples, zero differences**.
* The 32-bit native harness now opens its decoded output with `O_LARGEFILE`
  and checks writes; this episode's Intel I420 output exceeds 2 GiB.
* Scalar Intel playback omits HH and loses diagonal detail. The quality figures
  above use MMX-compatible synthesis. Use encoder `-hh_band 0` for common
  scalar/MMX reconstruction; that changes quality and size.

Machine-readable results: `out/work/thomas-final-summary.json`.
Quality measurement helper: `out/work/thomas_measure.py`.
Logs: `out/work/thomas-final-encode.log`, `thomas-checksums.log`,
`thomas-intel-parity.log`, `thomas-reference-ssim.log`, `thomas-final-ssim.log`.
See `HH_ENCODING.md` for the feature change and compatibility tests.
