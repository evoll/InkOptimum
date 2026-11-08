#include "GifToFramebufferConverter.h"

#include <BoardConfig.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <GifDecoder.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_err.h>
#include <esp_task_wdt.h>

#include <algorithm>
#include <cstdlib>
#include <new>

#include "BitmapHelpers.h"
#include "DirectPixelWriter.h"
#include "DitherUtils.h"
#include "PixelCache.h"

// The streaming row pipeline follows the GIF support in Paperio-Reader
// (MIT, Copyright (c) Dave Allie); see THIRD_PARTY.md.

namespace {

inline void resetWatchdogIfSubscribed() {
  if (esp_task_wdt_status(nullptr) == ESP_OK) esp_task_wdt_reset();
}

class FsGifSource final : public GifSource {
 public:
  explicit FsGifSource(FsFile& file) : file_(file) {}
  int read(uint8_t* buf, int len) override { return file_.read(buf, len); }
  bool skip(uint32_t len) override { return file_.seek(file_.position() + len); }

 private:
  FsFile& file_;
};

struct GifRender {
  GifDecoder* gif = nullptr;
  const RenderConfig* config = nullptr;

  int srcW = 0, srcH = 0, dstW = 0, dstH = 0;
  int screenW = 0, screenH = 0;
  int nextDstY = 0;  // first destination row not emitted yet (progressive only)
  bool interlaced = false;
  bool x4Pro = false;

  uint8_t* grayRow = nullptr;
  AtkinsonDitherer* ditherer = nullptr;  // progressive only; null -> ordered dither
  PixelCache cache;
  bool caching = false;
  DirectPixelWriter pw;

  void emitRow(int dstY) {
    const int outY = config->y + dstY;
    if (outY < 0 || outY >= screenH) return;
    pw.beginRow(outY);

    DirectCacheWriter cw;
    bool rowCaching = caching;
    if (rowCaching) {
      if (!cache.advanceTo(dstY)) {
        caching = false;
        rowCaching = false;
      } else {
        cw.init(cache.buffer, cache.bytesPerRow, cache.bandRows, cache.originX);
        cw.beginRow(outY, config->y + cache.bandStart);
      }
    }

    int srcX = 0, err = 0;
    for (int dstX = 0; dstX < dstW; dstX++) {
      const int outX = config->x + dstX;
      if (outX >= 0 && outX < screenW) {
        const uint8_t gray = grayRow[srcX];
        const uint8_t value = (ditherer && !x4Pro)
                                  ? ditherer->processPixel(gray, dstX)
                                  : quantizeImagePixel(gray, outX, outY, config->useDithering, x4Pro);
        pw.writePixel(outX, value);
        if (rowCaching) cw.writePixel(outX, value);
      }
      err += srcW;
      while (err >= dstW) {
        err -= dstW;
        if (srcX < srcW - 1) srcX++;
      }
    }
    if (ditherer) ditherer->nextRow();
  }

  static bool onRow(void* user, int actualY, const uint8_t* indexed, int width) {
    auto* self = static_cast<GifRender*>(user);
    resetWatchdogIfSubscribed();
    self->gif->toGray(indexed, self->grayRow, width);

    if (self->interlaced) {
      // Rows arrive out of order: each source row lands on the one destination
      // row it maps to. Caching is disabled in this mode.
      const int dstY = static_cast<int>(static_cast<int64_t>(actualY) * self->dstH / self->srcH);
      if (dstY < self->dstH) self->emitRow(dstY);
      return true;
    }

    // Progressive: cover every destination row this source row spans.
    int dstYEnd = static_cast<int>(static_cast<int64_t>(actualY + 1) * self->dstH / self->srcH);
    if (dstYEnd > self->dstH) dstYEnd = self->dstH;
    for (int y = self->nextDstY; y < dstYEnd; y++) self->emitRow(y);
    if (dstYEnd > self->nextDstY) self->nextDstY = dstYEnd;
    return true;
  }
};

}  // namespace

bool GifToFramebufferConverter::getDimensionsStatic(const std::string& imagePath, ImageDimensions& out) {
  FsFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    LOG_ERR("GIF", "Failed to open file for dimensions: %s", imagePath.c_str());
    return false;
  }
  uint8_t hdr[10];
  const int n = file.read(hdr, sizeof(hdr));
  file.close();
  int w = 0, h = 0;
  if (n != static_cast<int>(sizeof(hdr)) || !GifDecoder::parseDimensions(hdr, sizeof(hdr), w, h)) return false;
  out.width = static_cast<int16_t>(w);
  out.height = static_cast<int16_t>(h);
  return true;
}

bool GifToFramebufferConverter::supportsFormat(const std::string& extension) {
  return FsHelpers::hasGifExtension(extension);
}

bool GifToFramebufferConverter::decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer,
                                                    const RenderConfig& config) {
  LOG_DBG("GIF", "Decoding GIF: %s", imagePath.c_str());

  GifDecoder* gif = new (std::nothrow) GifDecoder();
  if (!gif) {
    LOG_ERR("GIF", "Failed to allocate decoder");
    return false;
  }

  FsFile file;
  if (!Storage.openFileForRead("GIF", imagePath, file)) {
    delete gif;
    return false;
  }
  FsGifSource source(file);
  if (!gif->open(source)) {
    LOG_ERR("GIF", "Failed to parse GIF: %s", imagePath.c_str());
    file.close();
    delete gif;
    return false;
  }

  GifRender r;
  r.gif = gif;
  r.config = &config;
  r.srcW = gif->frameWidth();
  r.srcH = gif->frameHeight();
  r.interlaced = gif->interlaced();
  r.x4Pro = BoardConfig::isX4Pro();
  r.screenW = renderer.getScreenWidth();
  r.screenH = renderer.getScreenHeight();

  if (!validateImageDimensions(r.srcW, r.srcH, "GIF")) {
    file.close();
    delete gif;
    return false;
  }

  if (config.useExactDimensions && config.maxWidth > 0 && config.maxHeight > 0) {
    r.dstW = config.maxWidth;
    r.dstH = config.maxHeight;
  } else {
    const float sx = static_cast<float>(config.maxWidth) / r.srcW;
    const float sy = static_cast<float>(config.maxHeight) / r.srcH;
    float scale = std::min(sx, sy);
    if (scale > 1.0f) scale = 1.0f;
    r.dstW = static_cast<int>(r.srcW * scale);
    r.dstH = static_cast<int>(r.srcH * scale);
  }
  if (r.dstW <= 0 || r.dstH <= 0) {
    LOG_ERR("GIF", "Zero-sized output for: %s", imagePath.c_str());
    file.close();
    delete gif;
    return false;
  }

  uint8_t* indexedRow = static_cast<uint8_t*>(malloc(r.srcW));
  r.grayRow = static_cast<uint8_t*>(malloc(r.srcW));
  if (!indexedRow || !r.grayRow) {
    LOG_ERR("GIF", "Failed to allocate row buffers");
    free(indexedRow);
    free(r.grayRow);
    file.close();
    delete gif;
    return false;
  }

  r.pw.init(renderer);
  if (!r.interlaced && config.useDithering && !r.x4Pro) {
    r.ditherer = new (std::nothrow) AtkinsonDitherer(r.dstW);  // null -> ordered dithering
  }
  r.caching = !config.cachePath.empty() && !r.interlaced;
  if (r.caching && !r.cache.begin(config.cachePath, r.dstW, r.dstH, config.x, config.y, 1)) {
    LOG_ERR("GIF", "Failed to start cache stream, continuing without caching");
    r.caching = false;
  }

  LOG_DBG("GIF", "GIF %dx%d -> %dx%d%s", r.srcW, r.srcH, r.dstW, r.dstH, r.interlaced ? " [interlaced]" : "");

  const int rows = gif->decode(source, indexedRow, &GifRender::onRow, &r);

  if (r.caching) {
    if (rows > 0) {
      r.cache.finalize();
    } else {
      r.cache.abort();
    }
  }

  delete r.ditherer;
  free(indexedRow);
  free(r.grayRow);
  file.close();
  delete gif;
  return rows > 0;
}
