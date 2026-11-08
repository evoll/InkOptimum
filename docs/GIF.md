# GIF images

EPUB pages can now contain `.gif` images. The first frame is decoded and shown
like JPEG/PNG (scaling, dithering, pixel cache).

- `lib/GifDecoder` — platform-free streaming LZW decoder (GIF87a/89a): global/local
  palettes, transparency (drawn white), interlacing. Tests: `test/native_gif`.
- `lib/Epub/Epub/converters/GifToFramebufferConverter.*` — adapter for storage,
  scaling, dithering and cache; registered in `ImageDecoderFactory`.

Limits: animation is not played (first frame only); frame offset inside the
logical screen is ignored; source size limits match PNG (2048x3072).
Interlaced images are not cached (rows arrive out of order).

Approach derived from Paperio-Reader (MIT, Dave Allie) — see `THIRD_PARTY.md`.
