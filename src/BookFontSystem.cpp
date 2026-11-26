#include "BookFontSystem.h"

#include <Epub.h>
#include <Epub/BookFont.h>
#include <Epub/css/CssParser.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Print.h>
#include <VectorFontSupport.h>

#include <cstring>
#include <memory>
#include <mutex>
#include <vector>

#include "InkMODSettings.h"

#if INKMOD_VECTOR_FONTS
#include <FontPsram.h>
#include <TtfEpdFont.h>
#include <esp_heap_caps.h>
#endif

namespace BookFontSystem {
#if INKMOD_VECTOR_FONTS
namespace {
constexpr size_t kMaxFontBytes = 3 * 1024 * 1024;     // one face
constexpr size_t kMaxTotalBytes = 8 * 1024 * 1024;    // every embedded face of the book
constexpr size_t kPsramHeadroom = 1024 * 1024;

// Collects an EPUB item into one PSRAM buffer of known size.
class PsramSink : public Print {
 public:
  PsramSink(uint8_t* buf, const size_t cap) : buf_(buf), cap_(cap) {}
  size_t write(const uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, const size_t len) override {
    if (len > cap_ - used_) return 0;
    memcpy(buf_ + used_, data, len);
    used_ += len;
    return len;
  }
  size_t used() const { return used_; }

 private:
  uint8_t* buf_;
  size_t cap_;
  size_t used_ = 0;
};

struct Source {
  uint8_t* data = nullptr;
  size_t len = 0;
};

struct Slot {
  uint32_t family = 0;
  Source sources[4];  // EpdFontFamily style order: regular, bold, italic, bold italic
  std::unique_ptr<TtfEpdFont> font;
  int fontId = 0;
  uint8_t pointSize = 0;
  bool failed = false;  // a family that cannot load is not retried every word
};

std::mutex gMutex;
GfxRenderer* gRenderer = nullptr;
std::vector<std::unique_ptr<Slot>> gSlots;

void unloadFonts() {
  for (auto& slot : gSlots) {
    if (slot->fontId != 0 && gRenderer) gRenderer->removeFont(slot->fontId);
    slot->font.reset();
    slot->fontId = 0;
    slot->pointSize = 0;
  }
}

void freeSlots() {
  unloadFonts();
  for (auto& slot : gSlots) {
    for (auto& s : slot->sources) {
      if (s.data) freeink::font::psramDeleteArray(s.data);
      s = Source{};
    }
  }
  gSlots.clear();
}

Slot* findSlot(const uint32_t family) {
  for (auto& slot : gSlots) {
    if (slot->family == family) return slot.get();
  }
  return nullptr;
}

int resolveFamily(const uint32_t family) {
  std::lock_guard<std::mutex> lock(gMutex);
  if (!gRenderer) return 0;
  Slot* slot = findSlot(family);
  if (!slot || slot->failed) return 0;

  const uint8_t pt = SETTINGS.getSdFontTargetPointSize();
  if (pt == 0) return 0;
  if (slot->fontId != 0 && slot->pointSize == pt && gRenderer->getFontMap().count(slot->fontId) != 0) {
    return slot->fontId;
  }
  if (slot->fontId != 0) {
    // The reader size changed: every book font follows it.
    unloadFonts();
  }

  auto font = std::make_unique<TtfEpdFont>();
  for (uint8_t st = 0; st < 4; ++st) {
    if (slot->sources[st].data) font->addResidentSource(st, slot->sources[st].data, static_cast<uint32_t>(slot->sources[st].len));
  }
  if (!font->load(pt)) {
    LOG_ERR("BKFONT", "Cannot use embedded font (family %08x)", static_cast<unsigned>(family));
    slot->failed = true;
    return 0;
  }
  uint32_t hash = family ^ 0xB00F0000u ^ (static_cast<uint32_t>(pt) << 8);
  hash *= 16777619u;
  int id = static_cast<int>(hash);
  if (id == 0) id = 1;
  while (gRenderer->getFontMap().count(id) != 0) ++id;  // never shadow a built-in or reader font
  gRenderer->insertFont(id, font->family());
  slot->font = std::move(font);
  slot->fontId = id;
  slot->pointSize = pt;
  LOG_DBG("BKFONT", "Embedded font %08x loaded at %u pt, id %d", static_cast<unsigned>(family), pt, id);
  return id;
}
}  // namespace

void detach() {
  BookFont::set(false, nullptr);
  std::lock_guard<std::mutex> lock(gMutex);
  freeSlots();
  gRenderer = nullptr;
}

void attach(const Epub& epub, GfxRenderer& renderer) {
  detach();
  if (SETTINGS.publisherFonts == 0) return;

  std::vector<CssFontFace> faces;
  if (!CssParser::loadFontFaces(epub.getCachePath(), faces)) return;

  std::lock_guard<std::mutex> lock(gMutex);
  gRenderer = &renderer;
  size_t total = 0;
  for (const auto& face : faces) {
    size_t size = 0;
    if (!epub.getItemSize(face.href, &size) || size == 0 || size > kMaxFontBytes || total + size > kMaxTotalBytes) {
      LOG_DBG("BKFONT", "Skipping %s (%u bytes)", face.href.c_str(), static_cast<unsigned>(size));
      continue;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < size + kPsramHeadroom) continue;
    uint8_t* buf = freeink::font::psramNewArray<uint8_t>(size);
    if (!buf) continue;
    PsramSink sink(buf, size);
    if (!epub.readItemContentsToStream(face.href, sink, 4096) || sink.used() != size) {
      freeink::font::psramDeleteArray(buf);
      continue;
    }
    Slot* slot = findSlot(face.family);
    if (!slot) {
      gSlots.push_back(std::make_unique<Slot>());
      slot = gSlots.back().get();
      slot->family = face.family;
    }
    const uint8_t style = static_cast<uint8_t>((face.bold ? 1 : 0) | (face.italic ? 2 : 0));
    if (slot->sources[style].data) {  // a second face for the same style: keep the first
      freeink::font::psramDeleteArray(buf);
      continue;
    }
    slot->sources[style] = {buf, size};
    total += size;
  }
  // A family needs a regular face (TtfEpdFont's base); a lone bold or italic face stands in for it.
  for (auto it = gSlots.begin(); it != gSlots.end();) {
    Slot& slot = **it;
    if (!slot.sources[0].data) {
      for (uint8_t st = 1; st < 4 && !slot.sources[0].data; ++st) {
        if (slot.sources[st].data) {
          slot.sources[0] = slot.sources[st];
          slot.sources[st] = Source{};
        }
      }
    }
    if (!slot.sources[0].data) {
      it = gSlots.erase(it);
    } else {
      ++it;
    }
  }
  if (gSlots.empty()) {
    gRenderer = nullptr;
    return;
  }
  LOG_DBG("BKFONT", "%u embedded font families, %u KB", static_cast<unsigned>(gSlots.size()),
          static_cast<unsigned>(total / 1024));
  BookFont::set(true, &resolveFamily);
}
#else
void detach() { BookFont::set(false, nullptr); }
void attach(const Epub&, GfxRenderer&) { BookFont::set(false, nullptr); }
#endif
}  // namespace BookFontSystem
