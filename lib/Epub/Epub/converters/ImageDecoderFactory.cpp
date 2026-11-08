#include "ImageDecoderFactory.h"

#include <HalStorage.h>
#include <Logging.h>

#include <memory>
#include <new>
#include <string>

#include "GifToFramebufferConverter.h"
#include "JpegToFramebufferConverter.h"
#include "PngToFramebufferConverter.h"

std::unique_ptr<JpegToFramebufferConverter> ImageDecoderFactory::jpegDecoder = nullptr;
std::unique_ptr<PngToFramebufferConverter> ImageDecoderFactory::pngDecoder = nullptr;
std::unique_ptr<GifToFramebufferConverter> ImageDecoderFactory::gifDecoder = nullptr;

std::string ImageDecoderFactory::sniffExtension(const uint8_t* header, const size_t length) {
  if (length >= 3 && header[0] == 0xFF && header[1] == 0xD8 && header[2] == 0xFF) return ".jpg";
  if (length >= 4 && header[0] == 0x89 && header[1] == 'P' && header[2] == 'N' && header[3] == 'G') return ".png";
  if (length >= 4 && header[0] == 'G' && header[1] == 'I' && header[2] == 'F' && header[3] == '8') return ".gif";
  return "";
}

ImageToFramebufferDecoder* ImageDecoderFactory::getDecoder(const std::string& imagePath) {
  std::string ext = imagePath;
  size_t dotPos = ext.rfind('.');
  const size_t slashPos = ext.rfind('/');
  if (dotPos != std::string::npos && (slashPos == std::string::npos || dotPos > slashPos)) {
    ext = ext.substr(dotPos);
    for (auto& c : ext) {
      c = tolower(c);
    }
  } else {
    ext = "";
  }

  // An archive entry without an extension (FB2-to-EPUB converters write <img src="images/img_32"/>)
  // is cached as ".img"; only its bytes can say what it is. Other extensions are judged by name.
  if (ext.empty() || ext == ".img") {
    HalFile file;
    if (Storage.exists(imagePath.c_str()) && Storage.openFileForRead("DEC", imagePath, file)) {
      uint8_t header[8] = {};
      const int got = file.read(header, sizeof(header));
      file.close();
      if (got > 0) ext = sniffExtension(header, static_cast<size_t>(got));
    }
  }

  if (JpegToFramebufferConverter::supportsFormat(ext)) {
    if (!jpegDecoder) {
      jpegDecoder.reset(new (std::nothrow) JpegToFramebufferConverter());
      if (!jpegDecoder) {
        LOG_ERR("DEC", "Low heap creating JPEG decoder wrapper");
        return nullptr;
      }
    }
    return jpegDecoder.get();
  } else if (PngToFramebufferConverter::supportsFormat(ext)) {
    if (!pngDecoder) {
      pngDecoder.reset(new (std::nothrow) PngToFramebufferConverter());
      if (!pngDecoder) {
        LOG_ERR("DEC", "Low heap creating PNG decoder wrapper");
        return nullptr;
      }
    }
    return pngDecoder.get();
  } else if (GifToFramebufferConverter::supportsFormat(ext)) {
    if (!gifDecoder) {
      gifDecoder.reset(new (std::nothrow) GifToFramebufferConverter());
      if (!gifDecoder) {
        LOG_ERR("DEC", "Low heap creating GIF decoder wrapper");
        return nullptr;
      }
    }
    return gifDecoder.get();
  }

  LOG_ERR("DEC", "No decoder found for image: %s", imagePath.c_str());
  return nullptr;
}

bool ImageDecoderFactory::isFormatSupported(const std::string& imagePath) { return getDecoder(imagePath) != nullptr; }
