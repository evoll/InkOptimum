#include <PluginEvents.h>
#include <gtest/gtest.h>

using namespace pluginevents;

namespace {
const char* DEVICE = R"({
  "events": {
    "reader.exit": {"request": {"url": "{cfg.server}/p", "body": "{\"b\":\"{event.book}\"}",
                                "headers": {"Authorization": "Bearer {token}"}}, "toast": "Synced"},
    "sleep.enter": {"download": {"url": "{cfg.server}/d.bmp", "dest": "/sleep.bmp"}},
    "bogus.event": {"request": {"url": "x"}},
    "reader.open": {"request": {}}
  }
})";

Event make(const std::string& id, const std::string& name) {
  Event e;
  e.id = id;
  e.name = name;
  e.ts = 1700000000;
  e.vars = {{"book", "/Books/a.epub"}, {"percent", "42"}};
  return e;
}
}  // namespace

TEST(PluginEvents, ParsesKnownHandlersOnly) {
  const auto h = parseHandlers(DEVICE);
  ASSERT_EQ(h.size(), 2u);
  EXPECT_EQ(h[0].event, "reader.exit");
  EXPECT_EQ(h[0].request.method, "POST");
  EXPECT_FALSE(h[0].isDownload);
  EXPECT_EQ(h[0].toast, "Synced");
  EXPECT_EQ(h[1].event, "sleep.enter");
  EXPECT_TRUE(h[1].isDownload);
  EXPECT_EQ(h[1].request.method, "GET");
  EXPECT_EQ(h[1].dest, "/sleep.bmp");
}

TEST(PluginEvents, LineRoundTripAndSizeLimit) {
  const Event e = make("a1", "reader.exit");
  Event back;
  ASSERT_TRUE(decodeLine(encodeLine(e), back));
  EXPECT_EQ(back.id, "a1");
  EXPECT_EQ(back.name, "reader.exit");
  EXPECT_EQ(back.ts, 1700000000);
  ASSERT_EQ(back.vars.size(), 2u);
  EXPECT_EQ(back.vars[1].second, "42");

  Event big = e;
  big.vars = {{"book", std::string(600, 'x')}};
  EXPECT_TRUE(encodeLine(big).empty());
  EXPECT_FALSE(decodeLine("not json", back));
}

TEST(PluginEvents, OutboxDropsWholesaleWhenFull) {
  std::string box;
  const std::string line(500, 'a');
  for (int i = 0; i < 8; i++) appendToOutbox(box, line);
  EXPECT_LE(box.size(), MAX_OUTBOX);
  const size_t before = box.size();
  appendToOutbox(box, line);  // would pass the cap: older lines are dropped
  EXPECT_EQ(box.size(), line.size() + 1);
  EXPECT_LT(box.size(), before);
}

TEST(PluginEvents, VariablesFollowTemplateRules) {
  const auto vars = eventVars(make("id9", "reader.exit"), {{"token", "T"}});
  EXPECT_EQ(plugincat::substitute("{token}|{event.book}|{event.percent}|{event.ts}|{event.id}", vars),
            "T|/Books/a.epub|42|1700000000|id9");
}

TEST(PluginEvents, DrainIsOrderedAndStopsAtFirstFailure) {
  const auto handlers = parseHandlers(DEVICE);
  std::string box;
  appendToOutbox(box, encodeLine(make("1", "reader.exit")));
  appendToOutbox(box, "garbage line");
  appendToOutbox(box, encodeLine(make("2", "reader.open")));  // no handler: delivered
  appendToOutbox(box, encodeLine(make("3", "reader.exit")));
  appendToOutbox(box, encodeLine(make("4", "sleep.enter")));

  std::vector<std::string> seen;
  int calls = 0;
  const int ran = drain(box, handlers, [&](const Handler&, const Event& e) {
    seen.push_back(e.id);
    return ++calls == 2 ? Delivery::Failed : Delivery::Delivered;
  });
  EXPECT_EQ(ran, 2);
  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], "1");
  EXPECT_EQ(seen[1], "3");
  // The failed event and everything after it remain, in order.
  Event first, second;
  const size_t nl = box.find('\n');
  ASSERT_TRUE(decodeLine(box.substr(0, nl), first));
  ASSERT_TRUE(decodeLine(box.substr(nl + 1, box.size() - nl - 2), second));
  EXPECT_EQ(first.id, "3");
  EXPECT_EQ(second.id, "4");
}

TEST(PluginEvents, DrainHonoursHandlerBudget) {
  const auto handlers = parseHandlers(DEVICE);
  std::string box;
  for (int i = 0; i < 6; i++) appendToOutbox(box, encodeLine(make(std::to_string(i), "reader.exit")));
  int ran = drain(box, handlers, [](const Handler&, const Event&) { return Delivery::Delivered; });
  EXPECT_EQ(ran, MAX_HANDLERS_PER_DRAIN);
  ran = drain(box, handlers, [](const Handler&, const Event&) { return Delivery::Delivered; });
  EXPECT_EQ(ran, 2);
  EXPECT_TRUE(box.empty());
}

TEST(PluginEvents, RequireMetaFilterAndSidecarMatching) {
  const auto handlers = parseHandlers(R"({"events":{
    "reader.exit":{"request":{"url":"https://h/x"},"require_meta":"svc_id"},
    "reader.open":{"request":{"url":"https://h/y"},"require_meta":true},
    "sleep.enter":{"request":{"url":"https://h/z"}}}})");
  ASSERT_EQ(handlers.size(), 2u);  // the wrong-typed filter drops its handler
  EXPECT_EQ(handlers[0].event, "reader.exit");
  EXPECT_EQ(handlers[0].requireMeta, "svc_id");
  EXPECT_TRUE(handlers[1].requireMeta.empty());

  EXPECT_TRUE(sidecarHasKey(R"({"svc_id":"5"})", "svc_id"));
  EXPECT_TRUE(sidecarHasKey(R"({"svc_id":5})", "svc_id"));
  EXPECT_FALSE(sidecarHasKey(R"({"svc_id":""})", "svc_id"));
  EXPECT_FALSE(sidecarHasKey(R"({"other":"1"})", "svc_id"));
  EXPECT_FALSE(sidecarHasKey(R"({"svc_id":5.5})", "svc_id"));
  EXPECT_FALSE(sidecarHasKey(R"({"svc_id":true})", "svc_id"));
  EXPECT_FALSE(sidecarHasKey("not json", "svc_id"));
}
