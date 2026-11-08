#include "FtShaper.h"

#if INKMOD_VECTOR_FONTS

#include <GlyphKey.h>
#include <Utf8.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>

#ifdef ESP_PLATFORM
#include <esp_heap_caps.h>
#endif

namespace {
constexpr size_t kMaxTableBytes = 384 * 1024;
constexpr size_t kBufferSlots = FtShaper::MAX_WORD + 32;  // normalization and dotted circles add glyphs
constexpr size_t kHeadroom = 48 * 1024;                   // left for everything else while a shaper lives

constexpr uint32_t tag(const char a, const char b, const char c, const char d) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24) | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16) |
         (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8) | static_cast<uint8_t>(d);
}

bool heapAllows(const size_t bytes) {
#ifdef ESP_PLATFORM
  return heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) >= bytes + kHeadroom ||
         heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= bytes;
#else
  (void)bytes;
  return true;
#endif
}
}  // namespace

bool FtShaper::nominalThunk(void* ctx, const uint32_t cp, uint32_t* glyph) {
  const auto g = static_cast<FtShaper*>(ctx)->ft_.glyphId(cp);
  *glyph = g;
  return g != 0;
}

int32_t FtShaper::advanceThunk(void* ctx, const uint32_t glyph) {
  auto* self = static_cast<FtShaper*>(ctx);
  AdvanceSlot& slot = self->advances_[glyph & 63];
  if (slot.glyph == glyph) return slot.advance;
  const uint16_t upem = self->ft_.unitsPerEm();
  freeink::font::FtFont::GlyphMetrics m;
  int32_t units = 0;
  if (upem && self->ft_.metricsGlyph26_6(glyph, static_cast<uint32_t>(upem) * 64u, m)) units = (m.advance26_6 + 32) >> 6;
  slot.glyph = glyph;
  slot.advance = units;
  return units;
}

bool FtShaper::ensureReady() {
  if (ready_) return true;
  if (tried_) return false;
  tried_ = true;

  const uint16_t upem = ft_.unitsPerEm();
  const uint32_t glyphs = ft_.glyphCount();
  if (upem == 0 || glyphs == 0) return false;

  const uint32_t tags[3] = {tag('G', 'D', 'E', 'F'), tag('G', 'S', 'U', 'B'), tag('G', 'P', 'O', 'S')};
  for (int i = 0; i < 3; ++i) {
    if (!heapAllows(0)) break;
    ft_.loadSfntTable(tags[i], kMaxTableBytes, &table_[i], &tableSize_[i], &owned_[i]);
  }
  // A font without GSUB has nothing to shape with; GDEF and GPOS are optional.
  if (table_[1] == nullptr) {
    release();
    tried_ = true;
    return false;
  }

  head_[18] = static_cast<uint8_t>(upem >> 8);
  head_[19] = static_cast<uint8_t>(upem);
  maxp_[4] = static_cast<uint8_t>(glyphs >> 8);
  maxp_[5] = static_cast<uint8_t>(glyphs);

  ot::FaceTables tables;
  tables[ot::FaceTables::HEAD] = ot::Table(head_, sizeof(head_));
  tables[ot::FaceTables::MAXP] = ot::Table(maxp_, sizeof(maxp_));
  tables[ot::FaceTables::GDEF] = ot::Table(table_[0], static_cast<uint32_t>(tableSize_[0]));
  tables[ot::FaceTables::GSUB_TABLE] = ot::Table(table_[1], static_cast<uint32_t>(tableSize_[1]));
  tables[ot::FaceTables::GPOS_TABLE] = ot::Table(table_[2], static_cast<uint32_t>(tableSize_[2]));

  ot::Face::Hooks hooks;
  hooks.ctx = this;
  hooks.nominalGlyph = &FtShaper::nominalThunk;
  hooks.advance = &FtShaper::advanceThunk;
  face_.setHooks(hooks);
  if (!face_.init(tables)) {
    release();
    tried_ = true;
    return false;
  }

  const size_t bytes = ot::Buffer::bytesFor(kBufferSlots);
  if (!heapAllows(bytes + sizeof(ot::Buffer))) {
    release();
    tried_ = true;
    return false;
  }
  storage_ = makeUniqueNoThrow<uint8_t[]>(bytes);
  buffer_ = makeUniqueNoThrow<ot::Buffer>();
  if (!storage_ || !buffer_) {
    release();
    tried_ = true;
    return false;
  }
  buffer_->setStorage(storage_.get(), bytes);
  ready_ = true;
  return true;
}

bool FtShaper::shapeWord(const uint32_t* cps, const unsigned count, const ot::Script script, const char* lang,
                         const uint32_t size26_6, std::string& out) {
  if (count == 0 || count > MAX_WORD || size26_6 == 0 || !ensureReady()) return false;

  auto& plan = plans_[static_cast<unsigned>(script)];
  if (!plan) {
    if (!heapAllows(8 * 1024)) return false;
    plan = makeUniqueNoThrow<ot::Plan>();
    if (!plan || !plan->build(face_, script, ot::languageTagsFor(lang ? lang : ""))) {
      plan.reset();
      return false;
    }
  }

  ot::Scale scale;
  scale.set(static_cast<int32_t>(size26_6), std::max<unsigned>(1, size26_6 >> 6), face_.upem());
  if (!ot::shape(face_, scale, *plan, cps, count, *buffer_) || !buffer_->successful) return false;

  const size_t before = out.size();
  for (unsigned i = 0; i < buffer_->len(); ++i) {
    const auto& info = buffer_->info[i];
    const auto& pos = buffer_->pos[i];
    if (info.codepoint > 0xFFFFu) {
      out.resize(before);
      return false;
    }
    // 26.6 pixels -> 1/32 em, rounded.
    const auto toUnits = [&](const int32_t v) {
      const int64_t n = static_cast<int64_t>(v) * glyphkey::OFFSET_UNITS_PER_EM;
      return static_cast<int>((n >= 0 ? n + size26_6 / 2 : n - size26_6 / 2) / static_cast<int64_t>(size26_6));
    };
    utf8AppendCodepoint(glyphkey::make(info.codepoint, toUnits(pos.xOffset), toUnits(pos.yOffset), info.isMark()), out);
  }
  return true;
}

void FtShaper::release() {
  buffer_.reset();
  storage_.reset();
  for (auto& p : plans_) p.reset();
  for (int i = 0; i < 3; ++i) {
    if (owned_[i] && table_[i]) freeink::font::FtFont::freeSfntTable(table_[i]);
    table_[i] = nullptr;
    owned_[i] = false;
    tableSize_[i] = 0;
  }
  face_ = ot::Face();
  ready_ = false;
  tried_ = false;
  for (auto& a : advances_) a = AdvanceSlot();
}

#endif  // INKMOD_VECTOR_FONTS
