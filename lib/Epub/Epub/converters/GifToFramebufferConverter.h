#pragma once

#include "ImageToFramebufferDecoder.h"

// Renders the first frame of a GIF. LZW decoding lives in lib/GifDecoder; this
// class only adapts it to storage, scaling, dithering and the pixel cache.
class GifToFramebufferConverter final : public ImageToFramebufferDecoder {
 public:
  static bool getDimensionsStatic(const std::string& imagePath, ImageDimensions& out);

  bool decodeToFramebuffer(const std::string& imagePath, GfxRenderer& renderer, const RenderConfig& config) override;

  bool getDimensions(const std::string& imagePath, ImageDimensions& dims) const override {
    return getDimensionsStatic(imagePath, dims);
  }

  static bool supportsFormat(const std::string& extension);
  const char* getFormatName() const override { return "GIF"; }
};
