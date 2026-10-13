# Indeo Video 5 (IV50) encoder for FFmpeg

An Indeo 5 encoder for FFmpeg (`-c:v indeo5`), written by studying Intel's
`IR50_32.DLL` encoder/decoder, plus decoder fixes found along the way
(synthesis fix, access keys, transparency regions).

- Encoder source lives in `indeo5enc.patch` (new file `libavcodec/indeo5enc.c` plus changes to `indeo5.c`, `ivi*.c/h`, `allcodecs.c`, `Makefile`, `configure`).
- Base FFmpeg commit: `615b07170e30d79ad5906418ea49f81c3cbd8457`.
- `NOTES.md` has layout, build and usage; `ENCODER_FEATURES.md` the feature audit versus Intel's encoder.

## Build

```sh
git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg && git checkout 615b07170e30d79ad5906418ea49f81c3cbd8457
git apply ../indeo5enc.patch
./configure --disable-x86asm --disable-doc --disable-autodetect && make -j16 ffmpeg
```

## Use

```sh
ffmpeg -i input.mp4 -c:v indeo5 -quality 12 -an output.avi
ffmpeg -h encoder=indeo5
```

## Not included

Intel's DLLs, their decompilation, and sample videos are proprietary/copyrighted and are not in this repo.
The scripts in `tools/` that compare against Intel's decoder expect `IR50_32.DLL` in the repo root
(`tools/iv5ref` needs building from `tools/iv5ref/iv5ref.c`).

## License

The patch modifies FFmpeg and is therefore LGPL-2.1-or-later, like FFmpeg's libavcodec.
