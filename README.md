# ffmpeg-indeo5-encoder

An Indeo Video 5 (IV50) encoder for FFmpeg (`-c:v indeo5`), written by studying Intel's
`IR50_32.DLL`, plus decoder fixes found along the way (synthesis fix, access keys, transparency regions).

## Layout

| Path | Contents |
|---|---|
| `src/libavcodec/indeo5enc.c` | the encoder |
| `patches/` | changes to existing FFmpeg files: decoder fixes (`0001`) and build wiring (`0002`) |
| `scripts/` | `apply.sh` (add to an FFmpeg checkout), `export.sh` (regenerate `src/` and `patches/`), `base-commit` |
| `tools/` | stream analysis, comparison against Intel's decoder/encoder, regression suite |
| `docs/` | notes, feature audit versus Intel's encoder, parity and RDO write-ups |

Patches are made against FFmpeg commit `615b07170e30d79ad5906418ea49f81c3cbd8457`.

## Build

```sh
git clone https://github.com/FFmpeg/FFmpeg.git ffmpeg
git -C ffmpeg checkout 615b07170e30d79ad5906418ea49f81c3cbd8457
scripts/apply.sh ffmpeg
cd ffmpeg && ./configure --disable-doc --disable-autodetect && make -j"$(nproc)" ffmpeg
```

## Use

```sh
ffmpeg -i input.mp4 -c:v indeo5 -quality 12 -an output.avi
ffmpeg -h encoder=indeo5
```

See `docs/NOTES.md` for all options and `docs/ENCODER_FEATURES.md` for the feature audit.

## Not included

Intel's DLLs, their decompilation, and sample videos are proprietary or copyrighted and are not in this repo.
The scripts in `tools/` that compare against Intel's decoder expect `IR50_32.DLL` in the repo root
(`tools/iv5ref` is built from `tools/iv5ref/iv5ref.c`).

## License

LGPL-2.1-or-later, like FFmpeg's libavcodec. See `LICENSE`.
