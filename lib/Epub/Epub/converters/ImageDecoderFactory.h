#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "ImageToFramebufferDecoder.h"

class JpegToFramebufferConverter;
class PngToFramebufferConverter;
class GifToFramebufferConverter;

class ImageDecoderFactory {
 public:
  // Returns non-owning pointer - factory owns the decoder lifetime
  static ImageToFramebufferDecoder* getDecoder(const std::string& imagePath);
  static bool isFormatSupported(const std::string& imagePath);
  // ".jpg", ".png" or ".gif" for a file that starts with that format's signature, else "".
  static std::string sniffExtension(const uint8_t* header, size_t length);

 private:
  static std::unique_ptr<JpegToFramebufferConverter> jpegDecoder;
  static std::unique_ptr<PngToFramebufferConverter> pngDecoder;
  static std::unique_ptr<GifToFramebufferConverter> gifDecoder;
};
