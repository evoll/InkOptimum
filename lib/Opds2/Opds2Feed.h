#pragma once
// OPDS 2.0 (JSON) catalogue reader.
//
// Reads an `application/opds+json` feed as a stream: bytes go in through feed(), finished entries come
// out through the sink, and the feed-level links (next, previous, search) are kept. Nothing but a few
// strings is held in memory, whatever the size of the feed. It produces the same OpdsEntry records as
// the Atom reader (OpdsParser), so the cache and the browser do not care which format a catalogue uses.
//
// Mapping from the OPDS 2.0 draft:
//   navigation[]                 -> NAVIGATION entries (title, href)
//   publications[]               -> BOOK entries: metadata.title/author/description/identifier/series,
//                                   links[] with an acquisition relation and a supported media type
//   groups[].navigation/publications are read like the top-level ones
//   links[] rel=next|previous|search at the top level -> page links and the search template
//
// Pure C++ (StreamingJsonParser + OpdsMeta), covered by native tests.
#include <cstddef>
#include <cstdint>
#include <string>

#include "OpdsEntry.h"
#include "StreamingJsonParser.h"

class Opds2Feed {
 public:
  using EntrySink = bool (*)(void* context, OpdsEntry&& entry);

  Opds2Feed(EntrySink sink, void* sinkContext);

  // True when the first non-blank byte of a feed is '{': the feed is JSON, not Atom XML.
  static bool looksLikeJson(const char* data, size_t length);

  void feed(const char* data, size_t length);

  bool hasError() const { return error_ || json_.hasError(); }
  // The sink refused an entry; nothing more is read.
  bool sinkRefused() const { return sinkRefused_; }

  const std::string& searchTemplate() const { return searchTemplate_; }
  const std::string& nextPageUrl() const { return nextPageUrl_; }
  const std::string& prevPageUrl() const { return prevPageUrl_; }
  size_t entryCount() const { return entryCount_; }

  // "/search{?query}" or "/search{?q,author}" -> "/search?query={searchTerms}"; a template that
  // already has {searchTerms} is kept. Public so the rule is testable on its own.
  static std::string toSearchTemplate(const std::string& href);

 private:
  enum class Kind : uint8_t {
    Root,
    FeedLinks,      // links[] of the feed
    FeedLink,
    NavArray,       // navigation[]
    NavItem,
    PubArray,       // publications[]
    Pub,
    GroupArray,     // groups[]
    Group,
    PubMeta,        // publication.metadata
    PubLinks,       // publication.links[]
    PubLink,
    AuthorArray,
    AuthorObject,
    IdentifierArray,
    BelongsTo,
    SeriesArray,
    SeriesObject,
    RelArray,       // "rel": ["a", "b"]
    Other,          // anything not read: its children are skipped
  };

  struct Link {
    std::string href;
    std::string type;
    std::string rel;
    std::string title;
    bool templated = false;
  };

  static constexpr size_t MAX_DEPTH = 24;
  static constexpr size_t MAX_AUTHORS = 3;

  // StreamingJsonParser callbacks.
  static void onKey(void* ctx, const char* key, size_t len);
  static void onString(void* ctx, const char* value, size_t len);
  static void onNumber(void* ctx, const char* value, size_t len);
  static void onBool(void* ctx, bool value);
  static void onNull(void* ctx);
  static void onObjectStart(void* ctx);
  static void onObjectEnd(void* ctx);
  static void onArrayStart(void* ctx);
  static void onArrayEnd(void* ctx);

  void key(const char* key, size_t len);
  void scalar(const std::string& value);
  void number(const std::string& value);
  void boolean(bool value);
  void open(bool isObject);
  void close(bool isObject);

  Kind childKind(Kind parent, const std::string& key, bool isObject) const;
  Kind top() const { return depth_ == 0 ? Kind::Root : stack_[depth_ - 1]; }
  void beginObject(Kind kind);
  void endObject(Kind kind);
  void finishFeedLink();
  void finishNavItem();
  void finishPublication();
  void addAuthor(const std::string& name);
  void addAcquisition(const Link& link);
  bool deliver(OpdsEntry&& entry);

  JsonCallbacks callbacks();

  StreamingJsonParser json_;
  EntrySink sink_;
  void* sinkContext_;
  bool error_ = false;
  bool sinkRefused_ = false;

  Kind stack_[MAX_DEPTH] = {};
  size_t depth_ = 0;
  std::string pendingKey_;  // the key of the value about to be read
  bool hasKey_ = false;
  size_t entryCount_ = 0;

  // Feed-level results.
  std::string searchTemplate_;
  std::string nextPageUrl_;
  std::string prevPageUrl_;

  // The object being read.
  Link link_;
  OpdsEntry entry_;
  std::string seriesName_;
  std::string seriesIndex_;
  uint8_t authorCount_ = 0;
  bool seriesTaken_ = false;
  std::string navTitle_;
  std::string navHref_;
  std::string selfHref_;  // a publication's own details document
};
