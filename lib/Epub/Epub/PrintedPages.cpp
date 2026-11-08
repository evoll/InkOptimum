#include "PrintedPages.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Print.h>
#include <Serialization.h>
#include <expat.h>

#include <cstring>
#include <functional>

#include "../Epub.h"

namespace {

constexpr uint16_t MAX_ENTRIES = 4000;
constexpr size_t MAX_STRING = 512;
constexpr size_t MAX_LABEL = 24;

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool hasToken(const char* value, const char* token) {
  if (!value) return false;
  const size_t len = strlen(token);
  const char* p = value;
  while (*p) {
    while (*p && isSpace(*p)) ++p;
    const char* start = p;
    while (*p && !isSpace(*p)) ++p;
    if (static_cast<size_t>(p - start) == len && strncmp(start, token, len) == 0) return true;
  }
  return false;
}

const char* attr(const char** atts, const char* name) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
    if (strcmp(atts[i], name) == 0) return atts[i + 1];
  }
  return nullptr;
}

std::string trim(const std::string& s) {
  size_t b = 0, e = s.size();
  while (b < e && isSpace(s[b])) ++b;
  while (e > b && isSpace(s[e - 1])) --e;
  return s.substr(b, e - b);
}

// Reads <nav epub:type="page-list"> links or NCX <pageList>/<pageTarget> entries and writes
// them straight to disk (no vector of entries is ever built).
class PageListWriter final : public Print {
 public:
  PageListWriter(const std::string& basePath, FsFile& out) : base_(basePath), out_(out) {}
  ~PageListWriter() override {
    if (parser_) XML_ParserFree(parser_);
  }

  bool setup() {
    parser_ = XML_ParserCreate(nullptr);
    if (!parser_) return false;
    XML_SetUserData(parser_, this);
    XML_SetElementHandler(parser_, onStart, onEnd);
    XML_SetCharacterDataHandler(parser_, onText);
    return true;
  }

  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* data, size_t size) override {
    if (!parser_ || failed_ || done_) return size;
    if (XML_Parse(parser_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0) == XML_STATUS_ERROR) {
      failed_ = true;  // keep whatever page entries were already written
    }
    return size;
  }

  uint16_t count() const { return count_; }

 private:
  const std::string& base_;
  FsFile& out_;
  XML_Parser parser_ = nullptr;
  uint16_t count_ = 0;
  bool failed_ = false;
  bool done_ = false;

  int depth_ = 0;
  int listDepth_ = -1;  // depth of the <nav>/<pageList> element, -1 outside
  bool inLink_ = false;
  bool inTarget_ = false;
  bool inLabelText_ = false;
  std::string href_, label_;

  void emit() {
    const std::string label = trim(label_);
    if (!label.empty() && !href_.empty() && count_ < MAX_ENTRIES && href_.size() <= MAX_STRING) {
      const size_t hash = href_.find('#');
      std::string file = hash == std::string::npos ? href_ : href_.substr(0, hash);
      const std::string anchor = hash == std::string::npos ? std::string() : FsHelpers::decodeUriEscapes(href_.substr(hash + 1));
      if (!file.empty()) file = FsHelpers::normalisePath(base_ + FsHelpers::decodeUriEscapes(file));
      else file.clear();
      // Same-file references ("#x") cannot be resolved without a spine; skip them.
      if (!file.empty()) {
        serialization::writeString(out_, file);
        serialization::writeString(out_, anchor);
        serialization::writeString(out_, label.substr(0, MAX_LABEL));
        ++count_;
      }
    }
    href_.clear();
    label_.clear();
  }

  static void onStart(void* ctx, const char* name, const char** atts) {
    auto* self = static_cast<PageListWriter*>(ctx);
    if (self->listDepth_ < 0) {
      if ((strcmp(name, "nav") == 0 && hasToken(attr(atts, "epub:type"), "page-list")) ||
          strcmp(name, "pageList") == 0) {
        self->listDepth_ = self->depth_;
      }
    } else if (strcmp(name, "a") == 0) {
      const char* href = attr(atts, "href");
      self->inLink_ = href != nullptr;
      self->href_ = href ? href : "";
      self->label_.clear();
    } else if (strcmp(name, "pageTarget") == 0) {
      self->inTarget_ = true;
      self->href_.clear();
      self->label_.clear();
    } else if (self->inTarget_ && strcmp(name, "text") == 0) {
      self->inLabelText_ = true;
    } else if (self->inTarget_ && strcmp(name, "content") == 0) {
      const char* src = attr(atts, "src");
      if (src) self->href_ = src;
    }
    ++self->depth_;
  }

  static void onText(void* ctx, const char* text, int len) {
    auto* self = static_cast<PageListWriter*>(ctx);
    if ((self->inLink_ || self->inLabelText_) && self->label_.size() < MAX_LABEL + 8) {
      self->label_.append(text, len);
    }
  }

  static void onEnd(void* ctx, const char* name) {
    auto* self = static_cast<PageListWriter*>(ctx);
    --self->depth_;
    if (self->listDepth_ < 0) return;
    if (self->inLink_ && strcmp(name, "a") == 0) {
      self->inLink_ = false;
      self->emit();
    } else if (self->inLabelText_ && strcmp(name, "text") == 0) {
      self->inLabelText_ = false;
    } else if (self->inTarget_ && strcmp(name, "pageTarget") == 0) {
      self->inTarget_ = false;
      self->emit();
    } else if (self->depth_ == self->listDepth_) {
      self->done_ = true;  // end of the page-list container; nothing else is needed
      self->listDepth_ = -1;
    }
  }
};

bool extract(const Epub& epub, const std::string& item, FsFile& out, uint16_t& count) {
  if (item.empty()) return false;
  const std::string base = item.substr(0, item.find_last_of('/') + 1);
  PageListWriter writer(base, out);
  if (!writer.setup()) return false;
  if (!epub.readItemContentsToStream(item, writer, 1024)) return false;
  count = writer.count();
  return true;
}

// Streams entries; visit returns false to stop.
void forEach(const std::string& cachePath, const std::function<bool(const PrintedPages::Entry&)>& visit) {
  FsFile file;
  if (!Storage.openFileForRead("PGL", cachePath + PrintedPages::CACHE_FILENAME, file)) return;
  uint16_t count = 0;
  if (!serialization::tryReadPod(file, count)) {
    file.close();
    return;
  }
  auto readStr = [&file](std::string& s) {
    uint32_t len = 0;
    if (!serialization::tryReadPod(file, len) || len > MAX_STRING) return false;
    s.resize(len);
    return len == 0 || file.read(reinterpret_cast<uint8_t*>(&s[0]), len) == static_cast<int>(len);
  };
  for (uint16_t i = 0; i < count; ++i) {
    PrintedPages::Entry e;
    if (!readStr(e.href) || !readStr(e.anchor) || !readStr(e.label)) break;
    if (!visit(e)) break;
  }
  file.close();
}

}  // namespace

namespace PrintedPages {

std::optional<int> parseNumericLabel(const std::string& label) {
  if (label.empty() || label.size() > 6) return std::nullopt;
  int value = 0;
  for (const char c : label) {
    if (c < '0' || c > '9') return std::nullopt;
    value = value * 10 + (c - '0');
  }
  return value;
}

bool ensureBuilt(const Epub& epub) {
  const std::string path = epub.getCachePath() + CACHE_FILENAME;
  if (Storage.exists(path.c_str())) return true;

  FsFile out;
  if (!Storage.openFileForWrite("PGL", path, out)) return false;
  serialization::writePod(out, static_cast<uint16_t>(0));

  uint16_t count = 0;
  // EPUB 3 nav first; fall back to the NCX <pageList>.
  if (!extract(epub, epub.getTocNavItem(), out, count) || count == 0) {
    // A failed nav attempt may have written partial entries; restart the body.
    out.seek(0);
    serialization::writePod(out, static_cast<uint16_t>(0));
    count = 0;
    extract(epub, epub.getTocNcxItem(), out, count);
  }

  if (count == 0) {
    out.close();
    Storage.remove(path.c_str());
    return false;
  }
  out.flush();
  if (!out.seek(0)) {
    out.close();
    Storage.remove(path.c_str());
    return false;
  }
  serialization::writePod(out, count);
  out.close();
  LOG_DBG("PGL", "Wrote pagelist.bin with %u entries", static_cast<unsigned>(count));
  return true;
}

bool hasNumeric(const std::string& cachePath) {
  bool found = false;
  forEach(cachePath, [&found](const Entry& e) {
    found = parseNumericLabel(e.label).has_value();
    return !found;
  });
  return found;
}

bool labelRange(const std::string& cachePath, int& minLabel, int& maxLabel) {
  bool any = false;
  int lo = 0, hi = 0;
  forEach(cachePath, [&](const Entry& e) {
    if (const auto v = parseNumericLabel(e.label)) {
      if (!any) lo = hi = *v;
      else {
        if (*v < lo) lo = *v;
        if (*v > hi) hi = *v;
      }
      any = true;
    }
    return true;
  });
  if (!any) return false;
  minLabel = lo;
  maxLabel = hi;
  return true;
}

std::optional<Entry> findByLabel(const std::string& cachePath, int target) {
  std::optional<Entry> result;
  forEach(cachePath, [&](const Entry& e) {
    const auto v = parseNumericLabel(e.label);
    if (v && *v == target) {
      result = e;
      return false;
    }
    return true;
  });
  return result;
}

std::optional<Entry> findAtOrBefore(const std::string& cachePath, int target) {
  std::optional<Entry> best;
  int bestValue = -1;
  forEach(cachePath, [&](const Entry& e) {
    const auto v = parseNumericLabel(e.label);
    if (v && *v <= target && *v > bestValue) {
      bestValue = *v;
      best = e;
      if (*v == target) return false;
    }
    return true;
  });
  return best;
}

}  // namespace PrintedPages
