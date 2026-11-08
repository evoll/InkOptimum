#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "Opds2Feed.h"
#include "OpdsText.h"

namespace {

struct Collected {
  std::vector<OpdsEntry> entries;
  bool refuse = false;
};

bool collect(void* ctx, OpdsEntry&& entry) {
  auto* c = static_cast<Collected*>(ctx);
  if (c->refuse) return false;
  c->entries.push_back(std::move(entry));
  return true;
}

// Feeds `json` in chunks of `chunk` bytes so a token split across reads is covered.
void run(Opds2Feed& feed, const std::string& json, const size_t chunk = 0) {
  if (chunk == 0) {
    feed.feed(json.data(), json.size());
    return;
  }
  for (size_t i = 0; i < json.size(); i += chunk) feed.feed(json.data() + i, std::min(chunk, json.size() - i));
}

const char* kCatalog = R"({
  "metadata": {"title": "Library", "numberOfItems": 2},
  "links": [
    {"rel": "self", "href": "/opds2", "type": "application/opds+json"},
    {"rel": "search", "href": "/opds2/search{?query}", "type": "application/opds+json", "templated": true},
    {"rel": "next", "href": "/opds2?page=2", "type": "application/opds+json"},
    {"rel": "previous", "href": "/opds2?page=0", "type": "application/opds+json"}
  ],
  "navigation": [
    {"title": "New", "href": "/opds2/new", "type": "application/opds+json", "rel": "http://opds-spec.org/sort/new"},
    {"title": "Authors", "href": "/opds2/authors"}
  ],
  "publications": [
    {
      "metadata": {
        "title": "Война и мир",
        "author": [{"name": "Лев Толстой"}, {"name": "Другой Автор"}],
        "description": "<p>Роман&nbsp;эпопея</p>",
        "identifier": "urn:isbn:9780140447934",
        "belongsTo": {"series": [{"name": "Классика", "position": 2}]}
      },
      "links": [
        {"rel": "self", "href": "/opds2/pub/1", "type": "application/opds-publication+json"},
        {"rel": "http://opds-spec.org/acquisition/open-access", "href": "/dl/1.epub", "type": "application/epub+zip"},
        {"rel": "http://opds-spec.org/acquisition", "href": "/dl/1.fb2.zip", "type": "application/fb2+zip"},
        {"rel": "http://opds-spec.org/acquisition/sample", "href": "/dl/1-sample.epub", "type": "application/epub+zip"}
      ],
      "images": [{"href": "/cover/1.jpg", "type": "image/jpeg"}]
    },
    {
      "metadata": {"title": "Only details", "author": "Single Author"},
      "links": [{"rel": ["self"], "href": "/opds2/pub/2", "type": "application/opds-publication+json"}]
    }
  ]
})";

}  // namespace

TEST(Opds2Feed, DetectsJsonByFirstByte) {
  EXPECT_TRUE(Opds2Feed::looksLikeJson("{\"a\":1}", 7));
  EXPECT_TRUE(Opds2Feed::looksLikeJson("  \n\t{", 5));
  EXPECT_TRUE(Opds2Feed::looksLikeJson("\xEF\xBB\xBF{", 4));
  EXPECT_FALSE(Opds2Feed::looksLikeJson("<?xml version", 13));
  EXPECT_FALSE(Opds2Feed::looksLikeJson("   ", 3));
}

TEST(Opds2Feed, ReadsNavigationAndPublications) {
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, kCatalog);
  ASSERT_FALSE(feed.hasError());
  ASSERT_EQ(out.entries.size(), 4u);

  EXPECT_EQ(out.entries[0].type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(out.entries[0].title, "New");
  EXPECT_EQ(out.entries[0].href, "/opds2/new");
  EXPECT_EQ(out.entries[1].title, "Authors");

  const OpdsEntry& book = out.entries[2];
  EXPECT_EQ(book.type, OpdsEntryType::BOOK);
  EXPECT_EQ(book.title, "Война и мир");
  EXPECT_EQ(book.author, "Лев Толстой, Другой Автор");
  EXPECT_EQ(book.description, "Роман эпопея");
  EXPECT_EQ(book.isbn, "9780140447934");
  EXPECT_EQ(book.series, "Классика #2");
  EXPECT_EQ(book.navigationHref, "/opds2/pub/1");
  ASSERT_EQ(book.acquisitionCount, 2);  // the sample link is not an acquisition
  EXPECT_EQ(book.acquisitions[0].format, OpdsBookFormat::EPUB);
  EXPECT_EQ(book.acquisitions[0].href, "/dl/1.epub");
  EXPECT_EQ(book.acquisitions[1].format, OpdsBookFormat::FB2_ZIP);
  EXPECT_EQ(book.href, "/dl/1.epub");

  // A publication with only a details document becomes a navigation row, like an Atom entry would.
  const OpdsEntry& details = out.entries[3];
  EXPECT_EQ(details.type, OpdsEntryType::NAVIGATION);
  EXPECT_EQ(details.title, "Only details");
  EXPECT_EQ(details.author, "Single Author");
  EXPECT_EQ(details.href, "/opds2/pub/2");
}

TEST(Opds2Feed, KeepsPageAndSearchLinks) {
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, kCatalog);
  EXPECT_EQ(feed.nextPageUrl(), "/opds2?page=2");
  EXPECT_EQ(feed.prevPageUrl(), "/opds2?page=0");
  EXPECT_EQ(feed.searchTemplate(), "/opds2/search?query={searchTerms}");
}

TEST(Opds2Feed, ResultDoesNotDependOnChunking) {
  for (const size_t chunk : {1u, 3u, 7u, 64u}) {
    Collected out;
    Opds2Feed feed(&collect, &out);
    run(feed, kCatalog, chunk);
    ASSERT_FALSE(feed.hasError()) << "chunk " << chunk;
    ASSERT_EQ(out.entries.size(), 4u) << "chunk " << chunk;
    EXPECT_EQ(out.entries[2].title, "Война и мир");
    EXPECT_EQ(feed.searchTemplate(), "/opds2/search?query={searchTerms}");
  }
}

TEST(Opds2Feed, ReadsGroups) {
  const std::string json = R"({
    "metadata": {"title": "Home"},
    "groups": [
      {"metadata": {"title": "Shelf"},
       "navigation": [{"title": "More", "href": "/more"}],
       "publications": [{"metadata": {"title": "Book"},
         "links": [{"rel": "http://opds-spec.org/acquisition", "href": "/b.mobi", "type": "application/x-mobipocket-ebook"}]}]}
    ]
  })";
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, json);
  ASSERT_EQ(out.entries.size(), 2u);
  EXPECT_EQ(out.entries[0].title, "More");
  EXPECT_EQ(out.entries[1].type, OpdsEntryType::BOOK);
  EXPECT_EQ(out.entries[1].acquisitions[0].format, OpdsBookFormat::MOBI);
}

TEST(Opds2Feed, IgnoresFacetsAndForeignLinks) {
  const std::string json = R"({
    "links": [{"rel": "self", "href": "/x"}],
    "facets": [{"metadata": {"title": "Sort"}, "links": [{"title": "A-Z", "href": "/sorted", "rel": "next"}]}],
    "publications": [{"metadata": {"title": "B", "subject": ["x"], "publisher": {"name": "P"}},
      "links": [{"rel": "http://opds-spec.org/acquisition", "href": "/b.epub", "type": "application/epub+zip"}]}]
  })";
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, json);
  ASSERT_EQ(out.entries.size(), 1u);
  EXPECT_TRUE(feed.nextPageUrl().empty());  // the facet's "next" is not the feed's page link
  EXPECT_TRUE(out.entries[0].author.empty());
}

TEST(Opds2Feed, AuthorShapes) {
  const std::string json = R"({"publications": [
    {"metadata": {"title": "A", "author": "Plain"}, "links": [{"rel":"http://opds-spec.org/acquisition","href":"/a.epub","type":"application/epub+zip"}]},
    {"metadata": {"title": "B", "author": {"name": "Object"}}, "links": [{"rel":"http://opds-spec.org/acquisition","href":"/b.epub","type":"application/epub+zip"}]},
    {"metadata": {"title": "C", "author": ["One", "Two", "Three", "Four"]}, "links": [{"rel":"http://opds-spec.org/acquisition","href":"/c.epub","type":"application/epub+zip"}]}
  ]})";
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, json);
  ASSERT_EQ(out.entries.size(), 3u);
  EXPECT_EQ(out.entries[0].author, "Plain");
  EXPECT_EQ(out.entries[1].author, "Object");
  EXPECT_EQ(out.entries[2].author, "One, Two, Three");
}

TEST(Opds2Feed, SeriesStateDoesNotLeakBetweenPublications) {
  const std::string json = R"({"publications": [
    {"metadata": {"title": "A", "belongsTo": {"series": {"name": "S", "position": 1}}},
     "links": [{"rel":"http://opds-spec.org/acquisition","href":"/a.epub","type":"application/epub+zip"}]},
    {"metadata": {"title": "B"},
     "links": [{"rel":"http://opds-spec.org/acquisition","href":"/b.epub","type":"application/epub+zip"}]}
  ]})";
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, json);
  ASSERT_EQ(out.entries.size(), 2u);
  EXPECT_EQ(out.entries[0].series, "S #1");
  EXPECT_TRUE(out.entries[1].series.empty());
}

TEST(Opds2Feed, PublicationWithoutTitleOrLinksIsSkipped) {
  const std::string json = R"({"publications": [
    {"metadata": {"author": "No title"}, "links": [{"rel":"http://opds-spec.org/acquisition","href":"/a.epub","type":"application/epub+zip"}]},
    {"metadata": {"title": "Nothing to open"}, "links": []}
  ]})";
  Collected out;
  Opds2Feed feed(&collect, &out);
  run(feed, json);
  EXPECT_TRUE(out.entries.empty());
}

TEST(Opds2Feed, StopsWhenTheSinkRefuses) {
  Collected out;
  out.refuse = true;
  Opds2Feed feed(&collect, &out);
  run(feed, kCatalog);
  EXPECT_TRUE(feed.sinkRefused());
}

TEST(Opds2Feed, SearchTemplates) {
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/s{?query}"), "/s?query={searchTerms}");
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/s?lang=ru{&q}"), "/s?lang=ru&q={searchTerms}");
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/s?lang=ru{?q,page}"), "/s?lang=ru&q={searchTerms}");
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/s/{query}"), "/s/{searchTerms}");
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/s?q={searchTerms}"), "/s?q={searchTerms}");
  EXPECT_EQ(Opds2Feed::toSearchTemplate("/plain"), "");
}

TEST(OpdsText, UntypedDirectDownloadTakesItsFormatFromTheFileName) {
  OpdsBookFormat f{};
  const char* direct = "http://opds-spec.org/acquisition";
  EXPECT_TRUE(opds_text::classifyAcquisition(nullptr, direct, "/books/a.epub", f));
  EXPECT_EQ(f, OpdsBookFormat::EPUB);
  EXPECT_TRUE(opds_text::classifyAcquisition("", direct, "/dl/Book.FB2.ZIP?id=7#x", f));
  EXPECT_EQ(f, OpdsBookFormat::FB2_ZIP);
  EXPECT_TRUE(opds_text::classifyAcquisition("application/octet-stream", "http://opds-spec.org/acquisition/open-access",
                                             "/x/b.mobi", f));
  EXPECT_EQ(f, OpdsBookFormat::MOBI);
  EXPECT_TRUE(opds_text::classifyAcquisition(nullptr, direct, "/x/b.kepub", f));
  EXPECT_EQ(f, OpdsBookFormat::EPUB);
}

TEST(OpdsText, ShopLoanAndUnknownLinksAreNotGuessed) {
  OpdsBookFormat f{};
  EXPECT_FALSE(opds_text::classifyAcquisition(nullptr, "http://opds-spec.org/acquisition/buy", "/a.epub", f));
  EXPECT_FALSE(opds_text::classifyAcquisition(nullptr, "http://opds-spec.org/acquisition/sample", "/a.epub", f));
  EXPECT_FALSE(opds_text::classifyAcquisition(nullptr, nullptr, "/a.epub", f));  // no rel at all
  EXPECT_FALSE(opds_text::classifyAcquisition(nullptr, "http://opds-spec.org/acquisition", "/a.pdf", f));
  EXPECT_FALSE(opds_text::classifyAcquisition("text/html", "http://opds-spec.org/acquisition", "/a.epub", f));
}

TEST(OpdsText, TypedLinksStillWinOverTheName) {
  OpdsBookFormat f{};
  EXPECT_TRUE(opds_text::classifyAcquisition("application/x-fb3", "http://opds-spec.org/acquisition", "/a.epub", f));
  EXPECT_EQ(f, OpdsBookFormat::FB3);
}
