#include "PluginXml.h"

#include <expat.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace plugincat {
namespace {

std::string localName(const char* name) {
  const char* colon = strrchr(name, ':');
  return colon ? colon + 1 : name;
}

struct Selector {
  std::string elem, attr;
  bool empty() const { return elem.empty() && attr.empty(); }
};

Selector parseSelector(const std::string& s) {
  Selector sel;
  const size_t at = s.find('@');
  if (at == std::string::npos) {
    sel.elem = s;
  } else {
    sel.elem = s.substr(0, at);
    sel.attr = s.substr(at + 1);
  }
  sel.elem = localName(sel.elem.c_str());
  sel.attr = localName(sel.attr.c_str());
  return sel;
}

std::string trim(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && isspace(static_cast<unsigned char>(s[a]))) a++;
  while (b > a && isspace(static_cast<unsigned char>(s[b - 1]))) b--;
  return s.substr(a, b - a);
}

std::string lower(std::string s) {
  for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string stripSlash(std::string s) {
  while (s.size() > 1 && s.back() == '/') s.pop_back();
  return s;
}

std::string pathOf(const std::string& url) {
  const size_t scheme = url.find("://");
  if (scheme == std::string::npos) return url;
  const size_t slash = url.find('/', scheme + 3);
  return slash == std::string::npos ? "/" : url.substr(slash);
}

std::string percentDecode(const std::string& in) {
  std::string out;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] == '%' && i + 2 < in.size() + 0 && isxdigit(static_cast<unsigned char>(in[i + 1])) &&
        isxdigit(static_cast<unsigned char>(in[i + 2]))) {
      out += static_cast<char>(strtol(in.substr(i + 1, 2).c_str(), nullptr, 16));
      i += 2;
    } else {
      out += in[i];
    }
  }
  return out;
}

}  // namespace

std::string resolveAgainst(const std::string& base, const std::string& href) {
  if (href.find("://") != std::string::npos) return href;
  const size_t scheme = base.find("://");
  if (scheme == std::string::npos) return {};
  const size_t slash = base.find('/', scheme + 3);
  const std::string origin = slash == std::string::npos ? base : base.substr(0, slash);
  if (!href.empty() && href[0] == '/') return origin + href;
  std::string basePath = slash == std::string::npos ? "/" : base.substr(slash);
  const size_t q = basePath.find_first_of("?#");
  if (q != std::string::npos) basePath.erase(q);
  const size_t lastSlash = basePath.rfind('/');
  return origin + basePath.substr(0, lastSlash + 1) + href;
}

struct XmlCatalogParser::Impl {
  XmlOptions options;
  Selector sel[6];  // title, author, id, url, version, description
  XML_Parser parser = nullptr;
  std::vector<XmlEntry>* entries = nullptr;
  bool* truncated = nullptr;

  int depth = 0;
  int itemDepth = 0;  // depth of the open item element, 0 = none
  XmlEntry current;
  std::string text[6];
  bool captured[6] = {};
  int capturing = -1;  // selector whose element text is being collected
  int capturingDepth = 0;
  std::string chars;
  bool isFolder = false;

  void finishItem() {
    std::string* out[6] = {&current.title, &current.author, &current.id, &current.url, &current.version,
                           &current.description};
    for (int i = 0; i < 6; i++) *out[i] = trim(text[i]);
    current.folder = isFolder;
    if (options.resolveUrls && !current.url.empty()) {
      const std::string resolved = resolveAgainst(options.requestUrl, current.url);
      if (!resolved.empty()) current.url = resolved;
    }
    if (options.skipSelf && !current.url.empty()) {
      if (stripSlash(percentDecode(pathOf(current.url))) == stripSlash(percentDecode(pathOf(options.requestUrl)))) {
        return;
      }
    }
    if (current.title.empty()) {
      // WebDAV rows often carry only an href: the last path segment makes a readable title.
      const std::string path = stripSlash(percentDecode(pathOf(current.url)));
      const size_t slash = path.rfind('/');
      current.title = slash == std::string::npos ? path : path.substr(slash + 1);
    }
    if (current.title.empty()) return;
    if (!current.folder && !options.extensions.empty()) {
      const std::string name = lower(current.url.empty() ? current.title : pathOf(current.url));
      bool ok = false;
      for (const auto& ext : options.extensions) {
        if (name.size() >= ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0) ok = true;
      }
      if (!ok) return;
    }
    if (entries->size() >= options.maxEntries) {
      *truncated = true;
      XML_StopParser(parser, XML_FALSE);
      return;
    }
    entries->push_back(current);
  }

  static void XMLCALL onStart(void* ud, const char* rawName, const char** attrs) {
    auto* self = static_cast<Impl*>(ud);
    self->depth++;
    const std::string name = localName(rawName);
    if (self->itemDepth == 0) {
      if (name == self->options.itemName) {
        self->itemDepth = self->depth;
        self->current = XmlEntry{};
        for (auto& t : self->text) t.clear();
        for (auto& c : self->captured) c = false;
        self->isFolder = false;
        self->capturing = -1;
        for (int i = 0; i < 6; i++) {  // "@attr" selectors read the item element itself
          if (!self->sel[i].elem.empty() || self->sel[i].attr.empty()) continue;
          for (int a = 0; attrs[a]; a += 2) {
            if (localName(attrs[a]) == self->sel[i].attr) {
              self->text[i] = attrs[a + 1];
              self->captured[i] = true;
            }
          }
        }
      }
      return;
    }
    if (!self->options.containerName.empty() && name == self->options.containerName) self->isFolder = true;
    for (int i = 0; i < 6; i++) {
      if (self->captured[i] || self->sel[i].elem != name) continue;
      if (!self->sel[i].attr.empty()) {
        for (int a = 0; attrs[a]; a += 2) {
          if (localName(attrs[a]) == self->sel[i].attr) {
            self->text[i] = attrs[a + 1];
            self->captured[i] = true;
          }
        }
      } else if (self->capturing < 0) {
        self->capturing = i;
        self->capturingDepth = self->depth;
        self->chars.clear();
      }
    }
  }

  static void XMLCALL onChars(void* ud, const char* s, int len) {
    auto* self = static_cast<Impl*>(ud);
    if (self->capturing >= 0 && self->chars.size() < 4096) self->chars.append(s, static_cast<size_t>(len));
  }

  static void XMLCALL onEnd(void* ud, const char*) {
    auto* self = static_cast<Impl*>(ud);
    if (self->capturing >= 0 && self->depth == self->capturingDepth) {
      self->text[self->capturing] = self->chars;
      self->captured[self->capturing] = true;
      self->capturing = -1;
    }
    if (self->itemDepth == self->depth) {
      self->itemDepth = 0;
      self->finishItem();
    }
    self->depth--;
  }
};

XmlCatalogParser::XmlCatalogParser(const XmlOptions& options) : impl_(new Impl) {
  impl_->options = options;
  const std::string* f[6] = {&options.fields.title, &options.fields.author,  &options.fields.id,
                             &options.fields.url,   &options.fields.version, &options.fields.description};
  for (int i = 0; i < 6; i++) impl_->sel[i] = parseSelector(*f[i]);
  impl_->parser = XML_ParserCreate(nullptr);
  impl_->entries = &entries_;
  impl_->truncated = &truncated_;
  if (impl_->parser) {
    XML_SetUserData(impl_->parser, impl_);
    XML_SetElementHandler(impl_->parser, Impl::onStart, Impl::onEnd);
    XML_SetCharacterDataHandler(impl_->parser, Impl::onChars);
  }
}

XmlCatalogParser::~XmlCatalogParser() {
  if (impl_->parser) XML_ParserFree(impl_->parser);
  delete impl_;
}

bool XmlCatalogParser::feed(const char* data, size_t len, bool last) {
  if (!impl_->parser) return false;
  if (truncated_) return true;
  if (XML_Parse(impl_->parser, data, static_cast<int>(len), last ? 1 : 0) == XML_STATUS_ERROR) {
    return truncated_;  // stopping at the entry limit is not an error
  }
  return true;
}

}  // namespace plugincat
