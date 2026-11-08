#include "Opds2Feed.h"

#include <OpdsMeta.h>

#include <cstring>
#include <utility>

#include "OpdsText.h"

namespace {
bool hasWord(const std::string& rel, const char* word) { return opds_text::containsIgnoreCase(rel.c_str(), word); }

// An acquisition relation that gives the whole book. Samples, previews, purchases and subscriptions are not
// something the reader can fetch with a plain GET.
bool isFullAcquisition(const std::string& rel) {
  if (!hasWord(rel, "acquisition")) return false;
  return !(hasWord(rel, "/sample") || hasWord(rel, "preview") || hasWord(rel, "/buy") || hasWord(rel, "subscribe"));
}
}  // namespace

Opds2Feed::Opds2Feed(EntrySink sink, void* sinkContext)
    : json_(callbacks()), sink_(sink), sinkContext_(sinkContext) {
  if (!sink_) error_ = true;
}

JsonCallbacks Opds2Feed::callbacks() {
  JsonCallbacks cb{};
  cb.ctx = this;
  cb.onKey = &Opds2Feed::onKey;
  cb.onString = &Opds2Feed::onString;
  cb.onStringPrefix = &Opds2Feed::onString;  // long descriptions arrive cut instead of dropped
  cb.onNumber = &Opds2Feed::onNumber;
  cb.onBool = &Opds2Feed::onBool;
  cb.onNull = &Opds2Feed::onNull;
  cb.onObjectStart = &Opds2Feed::onObjectStart;
  cb.onObjectEnd = &Opds2Feed::onObjectEnd;
  cb.onArrayStart = &Opds2Feed::onArrayStart;
  cb.onArrayEnd = &Opds2Feed::onArrayEnd;
  return cb;
}

bool Opds2Feed::looksLikeJson(const char* data, const size_t length) {
  for (size_t i = 0; i < length; ++i) {
    const unsigned char c = static_cast<unsigned char>(data[i]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') continue;
    if (i == 0 && length >= 3 && c == 0xEF) continue;  // UTF-8 BOM: look at what follows
    if (c == 0xEF || c == 0xBB || c == 0xBF) continue;
    return c == '{';
  }
  return false;
}

void Opds2Feed::feed(const char* data, const size_t length) {
  if (error_ || sinkRefused_) return;
  json_.feed(data, length);
}

// ---- callbacks ----

void Opds2Feed::onKey(void* ctx, const char* key, const size_t len) { static_cast<Opds2Feed*>(ctx)->key(key, len); }
void Opds2Feed::onString(void* ctx, const char* value, const size_t len) {
  static_cast<Opds2Feed*>(ctx)->scalar(std::string(value, len));
}
void Opds2Feed::onNumber(void* ctx, const char* value, const size_t len) {
  static_cast<Opds2Feed*>(ctx)->number(std::string(value, len));
}
void Opds2Feed::onBool(void* ctx, const bool value) { static_cast<Opds2Feed*>(ctx)->boolean(value); }
void Opds2Feed::onNull(void* ctx) { static_cast<Opds2Feed*>(ctx)->hasKey_ = false; }
void Opds2Feed::onObjectStart(void* ctx) { static_cast<Opds2Feed*>(ctx)->open(true); }
void Opds2Feed::onObjectEnd(void* ctx) { static_cast<Opds2Feed*>(ctx)->close(true); }
void Opds2Feed::onArrayStart(void* ctx) { static_cast<Opds2Feed*>(ctx)->open(false); }
void Opds2Feed::onArrayEnd(void* ctx) { static_cast<Opds2Feed*>(ctx)->close(false); }

void Opds2Feed::key(const char* key, const size_t len) {
  pendingKey_.assign(key, len);
  hasKey_ = true;
}

// What a container opened under `parent` with `key` is. Array elements arrive with no key.
Opds2Feed::Kind Opds2Feed::childKind(const Kind parent, const std::string& key, const bool isObject) const {
  switch (parent) {
    case Kind::Root:
      if (key == "links" && !isObject) return Kind::FeedLinks;
      if (key == "navigation" && !isObject) return Kind::NavArray;
      if (key == "publications" && !isObject) return Kind::PubArray;
      if (key == "groups" && !isObject) return Kind::GroupArray;
      return Kind::Other;
    case Kind::GroupArray:
      return isObject ? Kind::Group : Kind::Other;
    case Kind::Group:
      if (key == "navigation" && !isObject) return Kind::NavArray;
      if (key == "publications" && !isObject) return Kind::PubArray;
      return Kind::Other;
    case Kind::FeedLinks:
      return isObject ? Kind::FeedLink : Kind::Other;
    case Kind::NavArray:
      return isObject ? Kind::NavItem : Kind::Other;
    case Kind::PubArray:
      return isObject ? Kind::Pub : Kind::Other;
    case Kind::Pub:
      if (key == "metadata" && isObject) return Kind::PubMeta;
      if (key == "links" && !isObject) return Kind::PubLinks;
      return Kind::Other;
    case Kind::PubMeta:
      if (key == "author") return isObject ? Kind::AuthorObject : Kind::AuthorArray;
      if (key == "identifier" && !isObject) return Kind::IdentifierArray;
      if (key == "belongsTo" && isObject) return Kind::BelongsTo;
      if (key == "series") return isObject ? Kind::SeriesObject : Kind::SeriesArray;
      return Kind::Other;
    case Kind::AuthorArray:
      return isObject ? Kind::AuthorObject : Kind::Other;
    case Kind::BelongsTo:
      if (key == "series") return isObject ? Kind::SeriesObject : Kind::SeriesArray;
      return Kind::Other;
    case Kind::SeriesArray:
      return isObject ? Kind::SeriesObject : Kind::Other;
    case Kind::PubLinks:
      return isObject ? Kind::PubLink : Kind::Other;
    case Kind::PubLink:
    case Kind::FeedLink:
      if (key == "rel" && !isObject) return Kind::RelArray;
      return Kind::Other;
    default:
      return Kind::Other;
  }
}

void Opds2Feed::open(const bool isObject) {
  if (depth_ >= MAX_DEPTH) {
    error_ = true;
    return;
  }
  const Kind kind =
      depth_ == 0 ? Kind::Root : childKind(top(), hasKey_ ? pendingKey_ : std::string(), isObject);
  hasKey_ = false;
  stack_[depth_++] = kind;
  if (isObject) beginObject(kind);
}

void Opds2Feed::close(const bool isObject) {
  if (depth_ == 0) return;
  const Kind kind = stack_[--depth_];
  if (isObject) endObject(kind);
  hasKey_ = false;
}

void Opds2Feed::beginObject(const Kind kind) {
  switch (kind) {
    case Kind::FeedLink:
    case Kind::PubLink:
      link_ = Link{};
      break;
    case Kind::NavItem:
      navTitle_.clear();
      navHref_.clear();
      break;
    case Kind::Pub:
      entry_ = OpdsEntry{};
      seriesName_.clear();
      seriesIndex_.clear();
      authorCount_ = 0;
      seriesTaken_ = false;
      selfHref_.clear();
      break;
    default:
      break;
  }
}

void Opds2Feed::endObject(const Kind kind) {
  switch (kind) {
    case Kind::FeedLink:
      finishFeedLink();
      break;
    case Kind::NavItem:
      finishNavItem();
      break;
    case Kind::Pub:
      finishPublication();
      break;
    case Kind::PubLink:
      if (isFullAcquisition(link_.rel) || (link_.rel.empty() && !link_.type.empty())) {
        addAcquisition(link_);
      } else if (hasWord(link_.rel, "self") && selfHref_.empty()) {
        selfHref_ = link_.href;
      }
      break;
    case Kind::SeriesObject:
      seriesTaken_ = !seriesName_.empty();
      break;
    default:
      break;
  }
}

// ---- values ----

void Opds2Feed::scalar(const std::string& value) {
  const bool keyed = hasKey_;
  const std::string k = keyed ? pendingKey_ : std::string();
  hasKey_ = false;

  switch (top()) {
    case Kind::FeedLink:
    case Kind::PubLink:
      if (k == "href") link_.href = value;
      else if (k == "type") link_.type = value;
      else if (k == "rel") link_.rel = value;
      else if (k == "title") link_.title = value;
      break;
    case Kind::RelArray:
      if (!link_.rel.empty()) link_.rel += ' ';
      link_.rel += value;
      break;
    case Kind::NavItem:
      if (k == "title") navTitle_ = value;
      else if (k == "href") navHref_ = value;
      break;
    case Kind::PubMeta:
      if (k == "title") {
        entry_.title = value;
      } else if (k == "author") {
        addAuthor(value);
      } else if (k == "description") {
        entry_.description = opds_text::sanitizeDescriptionText(value);
      } else if (k == "identifier") {
        if (entry_.id.empty()) entry_.id = value;
        const auto ident = opds_meta::parseIdentifier(value);
        if (ident.kind == opds_meta::IdKind::Isbn && entry_.isbn.empty()) entry_.isbn = ident.value;
        if (ident.kind == opds_meta::IdKind::Asin && entry_.asin.empty()) entry_.asin = ident.value;
      } else if (k == "series" && !seriesTaken_) {
        seriesName_ = value;
        seriesTaken_ = !seriesName_.empty();
      }
      break;
    case Kind::AuthorArray:
      addAuthor(value);
      break;
    case Kind::AuthorObject:
      if (k == "name") addAuthor(value);
      break;
    case Kind::IdentifierArray: {
      if (entry_.id.empty()) entry_.id = value;
      const auto ident = opds_meta::parseIdentifier(value);
      if (ident.kind == opds_meta::IdKind::Isbn && entry_.isbn.empty()) entry_.isbn = ident.value;
      if (ident.kind == opds_meta::IdKind::Asin && entry_.asin.empty()) entry_.asin = ident.value;
      break;
    }
    case Kind::SeriesObject:
      if (!seriesTaken_) {
        if (k == "name") seriesName_ = value;
        else if (k == "position") seriesIndex_ = value;
      }
      break;
    default:
      break;
  }
}

void Opds2Feed::number(const std::string& value) {
  const std::string k = hasKey_ ? pendingKey_ : std::string();
  hasKey_ = false;
  if (top() == Kind::SeriesObject && k == "position" && !seriesTaken_) seriesIndex_ = value;
}

void Opds2Feed::boolean(const bool value) {
  const std::string k = hasKey_ ? pendingKey_ : std::string();
  hasKey_ = false;
  if ((top() == Kind::FeedLink || top() == Kind::PubLink) && k == "templated") link_.templated = value;
}

void Opds2Feed::addAuthor(const std::string& name) {
  if (name.empty() || authorCount_ >= MAX_AUTHORS) return;
  if (!entry_.author.empty()) entry_.author += ", ";
  entry_.author += name;
  ++authorCount_;
}

void Opds2Feed::addAcquisition(const Link& link) {
  OpdsBookFormat format;
  if (link.href.empty() || !opds_text::classifyAcquisitionType(link.type.c_str(), format)) return;
  for (uint8_t i = 0; i < entry_.acquisitionCount; ++i) {
    if (entry_.acquisitions[i].format == format && entry_.acquisitions[i].href == link.href) return;
  }
  if (entry_.acquisitionCount >= MAX_OPDS_ACQUISITIONS) return;
  auto& slot = entry_.acquisitions[entry_.acquisitionCount++];
  slot.format = format;
  slot.href = link.href;
}

// ---- finished objects ----

std::string Opds2Feed::toSearchTemplate(const std::string& href) {
  if (href.find("{searchTerms}") != std::string::npos) return href;
  const size_t open = href.find('{');
  if (open == std::string::npos) return {};
  const size_t close = href.find('}', open);
  if (close == std::string::npos) return {};

  std::string expr = href.substr(open + 1, close - open - 1);  // "?query,page" or "query"
  char lead = 0;
  if (!expr.empty() && (expr[0] == '?' || expr[0] == '&')) {
    lead = expr[0];
    expr.erase(0, 1);
  }
  const size_t comma = expr.find(',');
  const std::string name = comma == std::string::npos ? expr : expr.substr(0, comma);
  if (name.empty()) return {};

  std::string base = href.substr(0, open);
  const std::string tail = href.substr(close + 1);
  if (lead == 0) {
    // {query} inside the path or a query value: the variable itself is the search text.
    return base + "{searchTerms}" + tail;
  }
  const char sep = (lead == '&' || base.find('?') != std::string::npos) ? '&' : '?';
  return base + sep + name + "={searchTerms}" + tail;
}

void Opds2Feed::finishFeedLink() {
  if (link_.href.empty()) return;
  if (hasWord(link_.rel, "search")) {
    if (searchTemplate_.empty() && (link_.templated || link_.href.find('{') != std::string::npos)) {
      searchTemplate_ = toSearchTemplate(link_.href);
    }
  } else if (hasWord(link_.rel, "next")) {
    nextPageUrl_ = link_.href;
  } else if (hasWord(link_.rel, "previous") || hasWord(link_.rel, "prev")) {
    prevPageUrl_ = link_.href;
  }
}

bool Opds2Feed::deliver(OpdsEntry&& entry) {
  if (!sink_(sinkContext_, std::move(entry))) {
    sinkRefused_ = true;
    return false;
  }
  ++entryCount_;
  return true;
}

void Opds2Feed::finishNavItem() {
  if (navTitle_.empty() || navHref_.empty()) return;
  OpdsEntry nav;
  nav.type = OpdsEntryType::NAVIGATION;
  nav.title = navTitle_;
  nav.href = navHref_;
  nav.navigationHref = navHref_;
  deliver(std::move(nav));
}

void Opds2Feed::finishPublication() {
  if (entry_.title.empty()) return;
  if (!seriesName_.empty()) entry_.series = opds_meta::formatSeries(seriesName_, seriesIndex_);

  if (entry_.acquisitionCount > 0) {
    entry_.type = OpdsEntryType::BOOK;
    entry_.href = entry_.acquisitions[0].href;
    entry_.navigationHref = selfHref_;
  } else if (!selfHref_.empty()) {
    entry_.type = OpdsEntryType::NAVIGATION;
    entry_.navigationHref = selfHref_;
    entry_.href = selfHref_;
  } else if (!entry_.description.empty()) {
    entry_.type = OpdsEntryType::INFO;
  } else {
    return;  // nothing to open or read
  }
  deliver(std::move(entry_));
  entry_ = OpdsEntry{};
}
