#include <PluginRequest.h>
#include <PluginXml.h>
#include <PluginToken.h>
#include <gtest/gtest.h>

#include "PluginDetails.h"
#include "PluginItems.h"
#include "PluginManifest.h"
#include "PluginTemplate.h"

using namespace plugincat;

TEST(PluginTemplate, ReplacesKnownNamesAndKeepsTheRest) {
  const Vars vars = {{"token", "T"}, {"page", "2"}};
  EXPECT_EQ(substitute("Bearer {token} p={page} {unknown}", vars), "Bearer T p=2 {unknown}");
  EXPECT_EQ(substitute("{\"page\":{page},\"x\":{}}", vars), "{\"page\":2,\"x\":{}}");
}

TEST(PluginTemplate, SubstitutedValuesAreNotScannedAgain) {
  const Vars vars = {{"title", "{token}"}, {"token", "SECRET"}};
  EXPECT_EQ(substitute("{title}", vars), "{token}");
}

TEST(PluginTemplate, UrlEncodeAndFileNames) {
  EXPECT_EQ(urlEncode("a b&c/é"), "a%20b%26c%2F%C3%A9");
  EXPECT_EQ(sanitizeFileName("a/b:c?.epub"), "a_b_c_.epub");
  EXPECT_EQ(sanitizeFileName(".."), "download");
  EXPECT_EQ(sanitizeFileName(""), "download");
  const std::string longName = std::string(300, 'x') + ".epub";
  const std::string cut = sanitizeFileName(longName);
  EXPECT_EQ(cut.size(), 100u);
  EXPECT_EQ(cut.substr(cut.size() - 5), ".epub");
  // A cut never lands inside a UTF-8 sequence.
  std::string cyr;
  for (int i = 0; i < 80; i++) cyr += "я";
  const std::string cutCyr = sanitizeFileName(cyr);
  EXPECT_LE(cutCyr.size(), 100u);
  EXPECT_EQ(cutCyr.size() % 2, 0u);
}

TEST(PluginManifest, ParsesACatalogManifest) {
  const char* json = R"({
    "title": "My Hub", "description": "d",
    "token": {"file": "token.json", "path": "auth.token"},
    "config": {"file": "config.json"},
    "browse": {"url": "https://h/c?page={page}&n={limit}", "headers": {"Authorization": "Bearer {token}"},
               "items": "data.items", "fields": {"title": "name", "id": "id", "version": "version"},
               "page_size": 99,
               "lists": [{"title": "All"}, {"title": "Community", "notice": {"message": "careful"}}]},
    "download": {"bundle": {"base": "base", "files": "files"}, "dest_dir": "/plugins"}
  })";
  DeviceManifest m;
  ASSERT_TRUE(parseDeviceManifest(json, "/plugins/hub", m));
  EXPECT_TRUE(m.isCatalog());
  EXPECT_TRUE(m.isBundle());
  EXPECT_TRUE(m.tracksInstalls());
  EXPECT_EQ(m.title, "My Hub");
  EXPECT_EQ(m.tokenFile, "/plugins/hub/token.json");
  EXPECT_EQ(m.tokenPath, "auth.token");
  EXPECT_EQ(m.configFile, "/plugins/hub/config.json");
  EXPECT_EQ(m.pageSize, MAX_PAGE_SIZE);  // clamped
  ASSERT_EQ(m.lists.size(), 2u);
  EXPECT_TRUE(m.lists[1].notice.present());
  EXPECT_EQ(m.browse.headers.at(0).first, "Authorization");
  EXPECT_EQ(m.destDir, "/plugins");
  EXPECT_EQ(m.bundleSubdir, "{id}");
}

TEST(PluginManifest, AnEventsOnlyManifestIsNotACatalog) {
  DeviceManifest m;
  ASSERT_TRUE(parseDeviceManifest(R"({"title":"bg","events":{}})", "/plugins/bg", m));
  EXPECT_FALSE(m.isCatalog());
}

TEST(PluginManifest, RefusesPathsOutsideThePluginSpace) {
  DeviceManifest m;
  EXPECT_FALSE(parseDeviceManifest(R"({"browse":{"url":"u"},"download":{"dest_dir":"/.inkmod/wifi.json"}})",
                                   "/plugins/x", m));
  EXPECT_FALSE(parseDeviceManifest(R"({"browse":{"url":"u"},"download":{"dest_dir":"../x"}})", "/plugins/x", m));
  ASSERT_TRUE(parseDeviceManifest(R"({"browse":{"url":"u"},"token":{"file":"../../.inkmod/wifi.json"}})",
                                  "/plugins/x", m));
  EXPECT_TRUE(m.tokenFile.empty());  // refused, not followed
  EXPECT_FALSE(parseDeviceManifest("not json", "/plugins/x", m));
}

TEST(PluginItems, ResolvesDottedPathsWithArrayIndexes) {
  JsonDocument doc;
  deserializeJson(doc, R"({"a":{"b":[{"c":"x"},{"c":"y"}]},"n":5,"t":true})");
  EXPECT_EQ(variantToString(resolvePath(doc.as<JsonVariantConst>(), "a.b.1.c")), "y");
  EXPECT_EQ(variantToString(resolvePath(doc.as<JsonVariantConst>(), "n")), "5");
  EXPECT_EQ(variantToString(resolvePath(doc.as<JsonVariantConst>(), "t")), "true");
  EXPECT_EQ(variantToString(resolvePath(doc.as<JsonVariantConst>(), "a.b.9.c")), "");
  EXPECT_EQ(variantToString(resolvePath(doc.as<JsonVariantConst>(), "a.zz")), "");
}

TEST(PluginItems, CompatibilityRules) {
  EXPECT_TRUE(entryCompatible("", 0, ""));
  EXPECT_TRUE(entryCompatible("crosspoint", 0, ""));
  EXPECT_TRUE(entryCompatible("other, inkmod", 0, ""));
  EXPECT_FALSE(entryCompatible("other", 0, ""));
  EXPECT_FALSE(entryCompatible("", identity::PLUGIN_API + 1, ""));
  EXPECT_TRUE(entryCompatible("", 0, "catalog.lists, catalog.notices"));
  EXPECT_TRUE(entryCompatible("", 0, "catalog.lists,catalog.item_details"));
  EXPECT_FALSE(entryCompatible("", 0, "catalog.lists,catalog.teleport"));
  EXPECT_STREQ(identity::FAMILY, "crosspoint");
}

namespace {
DeviceManifest catalogManifest(int pageSize = 2) {
  DeviceManifest m;
  EXPECT_TRUE(parseDeviceManifest(
      R"({"browse":{"url":"u","items":"items","fields":{"title":"name","id":"id","author":"authors.0.name",
          "version":"version","description":"description"},
          "compatibility":{"firmware_family":"compat.family","plugin_api_min":"compat.api","requires":"compat.req"},
          "page_size":)" + std::to_string(pageSize) +
          R"(},"download":{"bundle":{"base":"base","files":"files"}}})",
      "/plugins/hub", m));
  return m;
}
}  // namespace

TEST(PluginItems, ExtractsRowsAndDetectsAnotherPage) {
  JsonDocument doc;
  deserializeJson(doc, R"({"items":[
    {"id":"a","name":"A","authors":[{"name":"X"}],"version":"1.0.0","base":"https://h/a","files":["manifest.json","device.json"]},
    {"id":"b","name":"B"},
    {"id":"c","name":"C"}]})");
  std::vector<Item> items;
  bool more = false;
  extractItems(doc.as<JsonVariantConst>(), catalogManifest(2), items, more);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_TRUE(more);  // the third row proves a next page
  EXPECT_EQ(items[0].author, "X");
  EXPECT_EQ(items[0].version, "1.0.0");
  EXPECT_EQ(items[0].files.size(), 2u);
}

TEST(PluginItems, HiddenAndTitlelessRowsTakeNoSlot) {
  JsonDocument doc;
  deserializeJson(doc, R"({"items":[
    {"id":"1","name":"incompatible","compat":{"api":99}},
    {"id":"2"},
    {"id":"3","name":"ok","compat":{"family":"crosspoint","req":"catalog.lists"}}]})");
  std::vector<Item> items;
  bool more = true;
  extractItems(doc.as<JsonVariantConst>(), catalogManifest(2), items, more);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].id, "3");
  EXPECT_FALSE(more);
}

TEST(PluginItems, AFilterKeepsOnlyMappedFields) {
  const DeviceManifest m = catalogManifest(8);
  JsonDocument filter;
  ASSERT_TRUE(buildItemFilter(m, filter));
  JsonDocument doc;
  ASSERT_EQ(deserializeJson(doc,
                            R"({"items":[{"name":"A","id":"1","huge":"zzzzzzzzzzzzzzzz","authors":[{"name":"X","bio":"y"}]}],"junk":1})",
                            DeserializationOption::Filter(filter)),
            DeserializationError::Ok);
  EXPECT_TRUE(doc["junk"].isNull());
  EXPECT_TRUE(doc["items"][0]["huge"].isNull());
  EXPECT_STREQ(doc["items"][0]["name"], "A");
  std::vector<Item> items;
  bool more;
  extractItems(doc.as<JsonVariantConst>(), m, items, more);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].author, "X");
}

TEST(PluginItems, RootArrayCatalogs) {
  DeviceManifest m;
  ASSERT_TRUE(parseDeviceManifest(R"({"browse":{"url":"u","fields":{"title":"t"}}})", "/plugins/x", m));
  JsonDocument filter;
  ASSERT_TRUE(buildItemFilter(m, filter));
  JsonDocument doc;
  ASSERT_EQ(deserializeJson(doc, R"([{"t":"one","x":1},{"t":"two"}])", DeserializationOption::Filter(filter)),
            DeserializationError::Ok);
  std::vector<Item> items;
  bool more;
  extractItems(doc.as<JsonVariantConst>(), m, items, more);
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[1].title, "two");
}

TEST(PluginManifest, ParsesAuthAndSidecar) {
  plugincat::DeviceManifest m;
  const char* json = R"({
    "browse": {"url": "https://h/c"},
    "download": {"url": "https://h/d", "sidecar": {"path": "{dest}.meta.json", "body": "{\"id\":{id}}"}},
    "auth": {"type": "device_code",
             "request": {"url": "https://h/code", "method": "POST"},
             "poll": {"url": "https://h/poll", "body": "{\"d\":\"{device_code}\"}"},
             "token_path": "tok"}
  })";
  ASSERT_TRUE(plugincat::parseDeviceManifest(json, "/.inkmod/plugins/x", m));
  EXPECT_EQ(m.auth.type, plugincat::Auth::Type::DeviceCode);
  EXPECT_EQ(m.auth.tokenPath, "tok");
  EXPECT_EQ(m.auth.codePath, "user_code");
  EXPECT_EQ(m.auth.poll.method, "POST");
  EXPECT_EQ(m.sidecarPath, "{dest}.meta.json");

  const char* broken = R"({"browse":{"url":"https://h/c"},"auth":{"type":"device_code","request":{"url":"u"}}})";
  ASSERT_TRUE(plugincat::parseDeviceManifest(broken, "/.inkmod/plugins/x", m));
  EXPECT_EQ(m.auth.type, plugincat::Auth::Type::None);  // device_code without a poll request is unusable
}

TEST(PluginTemplate, RequestEncodesItemValuesButNotConfig) {
  plugincat::Request tpl;
  tpl.url = "{cfg.server}/find?q={title}&p={page}";
  tpl.body = "{\"t\":\"{title}\"}";
  tpl.headers = {{"Authorization", "Bearer {token}"}};
  const auto r = plugincat::substituteRequest(tpl, {{"cfg.server", "https://h:8443/api"}, {"token", "a b"}},
                                              {{"title", "A&B c"}, {"page", "2"}});
  EXPECT_EQ(r.url, "https://h:8443/api/find?q=A%26B%20c&p=2");
  EXPECT_EQ(r.body, "{\"t\":\"A&B c\"}");
  EXPECT_EQ(r.headers[0].second, "Bearer a b");
  EXPECT_EQ(r.method, "GET");

  plugincat::Request direct;
  direct.url = "{url}";
  EXPECT_EQ(plugincat::substituteRequest(direct, {}, {{"url", "https://x/y?a=b"}}).url, "https://x/y?a=b");
}

TEST(PluginToken, WritesTheShapeTheManifestReads) {
  EXPECT_EQ(plugincat::tokenFileJson("token", "abc"), "{\"token\":\"abc\"}");
  EXPECT_EQ(plugincat::tokenFileJson("auth.access", "abc"), "{\"auth\":{\"access\":\"abc\"}}");
  EXPECT_EQ(plugincat::tokenFileJson("a..b", "x"), "");
  EXPECT_EQ(plugincat::tokenFileJson("", "x"), "");
}

namespace {
std::vector<plugincat::XmlEntry> parseXml(const plugincat::XmlOptions& o, const std::string& xml) {
  plugincat::XmlCatalogParser p(o);
  // Feed in small pieces: the device streams the file in chunks.
  for (size_t i = 0; i < xml.size(); i += 7) p.feed(xml.data() + i, std::min<size_t>(7, xml.size() - i), false);
  p.feed("", 0, true);
  return p.entries();
}
}  // namespace

TEST(PluginXml, WebDavMultistatus) {
  plugincat::XmlOptions o;
  o.itemName = "response";
  o.containerName = "collection";
  o.fields.title = "displayname";
  o.fields.url = "href";
  o.skipSelf = true;
  o.resolveUrls = true;
  o.extensions = {".epub", ".pdf"};
  o.requestUrl = "https://nas.local/dav/books/";
  const std::string xml = R"(<?xml version="1.0"?>
<D:multistatus xmlns:D="DAV:">
 <D:response><D:href>/dav/books/</D:href><D:propstat><D:prop><D:displayname>books</D:displayname>
   <D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>
 <D:response><D:href>/dav/books/Sci-Fi/</D:href><D:propstat><D:prop><D:displayname>Sci-Fi</D:displayname>
   <D:resourcetype><D:collection/></D:resourcetype></D:prop></D:propstat></D:response>
 <D:response><D:href>/dav/books/Dune%20(1965).epub</D:href><D:propstat><D:prop>
   <D:displayname>Dune</D:displayname><D:resourcetype/></D:prop></D:propstat></D:response>
 <D:response><D:href>/dav/books/notes.txt</D:href><D:propstat><D:prop><D:displayname>notes</D:displayname></D:prop></D:propstat></D:response>
</D:multistatus>)";
  const auto rows = parseXml(o, xml);
  ASSERT_EQ(rows.size(), 2u);  // self and the .txt file are dropped
  EXPECT_EQ(rows[0].title, "Sci-Fi");
  EXPECT_TRUE(rows[0].folder);
  EXPECT_EQ(rows[0].url, "https://nas.local/dav/books/Sci-Fi/");
  EXPECT_EQ(rows[1].title, "Dune");
  EXPECT_FALSE(rows[1].folder);
  EXPECT_EQ(rows[1].url, "https://nas.local/dav/books/Dune%20(1965).epub");
}

TEST(PluginXml, AtomFeedWithAttributeSelectors) {
  plugincat::XmlOptions o;
  o.itemName = "entry";
  o.fields.title = "title";
  o.fields.author = "name";
  o.fields.url = "link@href";
  o.fields.id = "@xml:lang";  // an attribute of the item element itself
  const std::string xml =
      "<feed><entry xml:lang=\"en\"><title>A &amp; B</title><author><name>Ann</name></author>"
      "<link href=\"http://h/a.epub\"/><link href=\"http://h/second\"/></entry>"
      "<entry><title></title></entry></feed>";
  const auto rows = parseXml(o, xml);
  ASSERT_EQ(rows.size(), 1u);  // the titleless row without a url is dropped
  EXPECT_EQ(rows[0].title, "A & B");
  EXPECT_EQ(rows[0].author, "Ann");
  EXPECT_EQ(rows[0].url, "http://h/a.epub");  // first match wins
  EXPECT_EQ(rows[0].id, "en");
}

TEST(PluginXml, EntryLimitAndResolver) {
  plugincat::XmlOptions o;
  o.itemName = "i";
  o.fields.title = "t";
  o.maxEntries = 3;
  std::string xml = "<r>";
  for (int i = 0; i < 10; i++) xml += "<i><t>row" + std::to_string(i) + "</t></i>";
  xml += "</r>";
  plugincat::XmlCatalogParser p(o);
  EXPECT_TRUE(p.feed(xml.data(), xml.size(), true));
  EXPECT_EQ(p.entries().size(), 3u);
  EXPECT_TRUE(p.truncated());

  EXPECT_EQ(plugincat::resolveAgainst("https://h/a/b?q=1", "c.epub"), "https://h/a/c.epub");
  EXPECT_EQ(plugincat::resolveAgainst("https://h/a/b", "/x"), "https://h/x");
  EXPECT_EQ(plugincat::resolveAgainst("https://h/a/b", "http://o/y"), "http://o/y");
}

TEST(PluginManifest, ParsesAnXmlCatalog) {
  plugincat::DeviceManifest m;
  const char* json = R"({"browse":{"format":"xml","url":"https://h/dav/","method":"PROPFIND","item":"response",
    "container_element":"collection","skip_self":true,"resolve_urls":true,"extensions":["EPUB","pdf"],
    "fields":{"title":"displayname","url":"href"}}})";
  ASSERT_TRUE(plugincat::parseDeviceManifest(json, "/.inkmod/plugins/x", m));
  EXPECT_TRUE(m.xml);
  EXPECT_EQ(m.xmlItem, "response");
  EXPECT_EQ(m.xmlExtensions, (std::vector<std::string>{".epub", ".pdf"}));
  EXPECT_EQ(m.browse.method, "PROPFIND");
  EXPECT_FALSE(plugincat::parseDeviceManifest(R"({"browse":{"format":"xml","url":"https://h"}})", "/.inkmod/plugins/x", m));
}

TEST(PluginDetails, HtmlToText) {
  const std::string html =
      "<html><head><style>p{color:red}</style><script>var x='<b>';</script></head><body>"
      "<h1>Title &amp; more</h1><p>First   para<br>line two</p><ul><li>One</li><li>Two &lt;3</li></ul>"
      "<!-- hidden --><p>Caf&#233; &#x41;&nbsp;end &bogus; done</p></body></html>";
  EXPECT_EQ(plugincat::htmlToText(html),
            "Title & more\n\nFirst para\nline two\n\n- One\n- Two <3\n\nCaf\xC3\xA9 A end &bogus; done");
}

TEST(PluginDetails, BoundedAndUtf8Safe) {
  std::string html = "<p>";
  for (int i = 0; i < 100; ++i) html += "\xD0\xB0";  // Cyrillic a, 2 bytes each
  const std::string out = plugincat::htmlToText(html, 11);
  EXPECT_LE(out.size(), 11u);
  EXPECT_EQ(out.size() % 2, 0u);  // no half character
}

TEST(PluginDetails, PlainTextFormat) {
  EXPECT_EQ(plugincat::detailsToText("a\r\nb\r\n", ""), "a\nb");
  EXPECT_EQ(plugincat::detailsToText("<b>x</b>", "text"), "<b>x</b>");
  EXPECT_EQ(plugincat::detailsToText("<b>x</b>", "html"), "x");
}

TEST(PluginItems, DetailsFields) {
  DeviceManifest m;
  ASSERT_TRUE(parseDeviceManifest(
      R"({"browse":{"url":"https://h/c","items":"items","fields":{"title":"t","description":"d",
          "detail_author":"da","repository":"repo","details_url":"du","details_format":"df"}}})",
      "/.inkmod/plugins/x", m));
  JsonDocument doc;
  ASSERT_FALSE(deserializeJson(doc, R"({"items":[{"t":"A","d":"desc","da":"Me","repo":"r","du":"https://h/d","df":"html","junk":1}]})"));
  std::vector<Item> out;
  bool more = false;
  extractItems(doc.as<JsonVariantConst>(), m, out, more);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0].detailAuthor, "Me");
  EXPECT_EQ(out[0].repository, "r");
  EXPECT_EQ(out[0].detailsUrl, "https://h/d");
  EXPECT_EQ(out[0].detailsFormat, "html");
  JsonDocument filter;
  ASSERT_TRUE(buildItemFilter(m, filter));
  JsonDocument filtered;
  ASSERT_FALSE(deserializeJson(filtered, R"({"items":[{"t":"A","da":"Me","du":"u","junk":1}]})",
                               DeserializationOption::Filter(filter)));
  EXPECT_FALSE(filtered["items"][0]["junk"].is<int>());
  EXPECT_STREQ(filtered["items"][0]["du"] | "", "u");
}

TEST(PluginCatalog, EscapeUrlKeepsValidUrlsAndEncodesTheRest) {
  using plugincat::escapeUrl;
  EXPECT_EQ(escapeUrl("https://h/p?q=1&r=%2F~ok#f"), "https://h/p?q=1&r=%2F~ok#f");
  EXPECT_EQ(escapeUrl("https://h/dl/Caf\xC3\xA9 & Co 1"), "https://h/dl/Caf%C3%A9%20&%20Co%201");
  EXPECT_EQ(escapeUrl("a\tb\n"), "a%09b%0A");
}

TEST(PluginCatalog, EncodedItemFieldsAreBuiltOnlyWhereUsed) {
  using namespace plugincat;
  Request tpl;
  tpl.url = "https://h/hop/{id_enc}?t={title_enc}&u={url_enc}";
  tpl.body = "a={author_enc}";
  const Vars item = {{"id", "a b"}, {"title", "Caf\xC3\xA9 & Co"}, {"author", "X/Y"}, {"url", "https://ex.com/p?q=1&r=2"}};
  const Request r = substituteRequest(tpl, {}, item);
  EXPECT_EQ(r.url, "https://h/hop/a%20b?t=Caf%C3%A9%20%26%20Co&u=https%3A%2F%2Fex.com%2Fp%3Fq%3D1%26r%3D2");
  EXPECT_EQ(r.body, "a=X%2FY");

  Request raw;
  raw.url = "{url}";
  EXPECT_EQ(substituteRequest(raw, {}, {{"url", "https://x/a b"}}).url, "https://x/a%20b");
}

TEST(PluginCatalog, AdvertisesEncodedItemFields) {
  EXPECT_NE(std::string(identity::CAPABILITIES).find("catalog.encoded_item_fields"), std::string::npos);
}

TEST(PluginItems, PagingStyleFollowsTheTemplateVariables) {
  Request r;
  r.url = "https://h/c?page={page}&per_page={limit}";
  EXPECT_EQ(pagingOf(r), Paging::PageLimit);
  EXPECT_EQ(limitFor(Paging::PageLimit, 8), 8);
  r.url = "https://h/c?offset={offset}&limit={limit}&page={page}";  // {offset} wins
  EXPECT_EQ(pagingOf(r), Paging::Offset);
  EXPECT_EQ(limitFor(Paging::Offset, 8), 9);
  r.url = "https://h/all-{page}.json";
  EXPECT_EQ(pagingOf(r), Paging::Page);
  r.url = "https://h/all.json";
  EXPECT_EQ(pagingOf(r), Paging::Static);
  r.headers.emplace_back("X-Page", "{page}");
  EXPECT_EQ(pagingOf(r), Paging::Page);
}

TEST(PluginItems, StaticListIsSlicedLocallyAndFullServerPageShowsNext) {
  JsonDocument doc;
  deserializeJson(doc, R"({"items":[{"id":"1","name":"A"},{"id":"2","name":"B"},{"id":"3","name":"C"},
                                   {"id":"4","name":"D"},{"id":"5","name":"E"}]})");
  std::vector<Item> items;
  bool more = false;
  extractItems(doc.as<JsonVariantConst>(), catalogManifest(2), items, more, 2);  // page 2 of a static list
  ASSERT_EQ(items.size(), 2u);
  EXPECT_EQ(items[0].title, "C");
  EXPECT_TRUE(more);
  items.clear();
  extractItems(doc.as<JsonVariantConst>(), catalogManifest(2), items, more, 4);  // page 3: the last row
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].title, "E");
  EXPECT_FALSE(more);

  JsonDocument server;  // the server already sliced the page: exactly page_size rows
  deserializeJson(server, R"({"items":[{"id":"1","name":"A"},{"id":"2","name":"B"}]})");
  items.clear();
  extractItems(server.as<JsonVariantConst>(), catalogManifest(2), items, more, 0, true);
  EXPECT_EQ(items.size(), 2u);
  EXPECT_TRUE(more);
}

TEST(PluginServerPath, ResolvesAgainstTheRequestOrigin) {
  std::string p = "/files/1";
  EXPECT_TRUE(resolveServerPath(p, "https://host.example:8443/api/list?page=2"));
  EXPECT_EQ(p, "https://host.example:8443/files/1");
  p = "/x";
  EXPECT_TRUE(resolveServerPath(p, "http://h"));
  EXPECT_EQ(p, "http://h/x");
  p = "/y";
  EXPECT_TRUE(resolveServerPath(p, "http://h?q=1"));
  EXPECT_EQ(p, "http://h/y");
}

TEST(PluginServerPath, LeavesEverythingElseAlone) {
  for (const char* v : {"", "https://other/x", "//cdn/x", "relative/x"}) {
    std::string p = v;
    EXPECT_FALSE(resolveServerPath(p, "https://host/api")) << v;
    EXPECT_EQ(p, v);
  }
  std::string p = "/x";
  EXPECT_FALSE(resolveServerPath(p, "not a url"));
  EXPECT_EQ(p, "/x");
}
