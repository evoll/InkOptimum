#include <gtest/gtest.h>

#include "ReadingSessions.h"

using namespace readingsessions;

namespace {
const char* DOC = "0123456789abcdef0123456789ABCDEF";
const uint8_t MAC[6] = {0x98, 0xc3, 0x77, 0xbe, 0xea, 0x30};

Session make(int64_t start = 1760000000, uint32_t secs = 600) {
  Session s;
  s.doc = DOC;
  s.start = start;
  s.secs = secs;
  s.tz = 180;
  s.from = 100;
  s.to = 450;
  s.title = "Title";
  s.path = "/books/a.epub";
  return s;
}
}  // namespace

TEST(ReadingSessions, DocumentIdMustBe32Hex) {
  EXPECT_TRUE(isDocumentId(DOC));
  EXPECT_FALSE(isDocumentId("short"));
  EXPECT_FALSE(isDocumentId("0123456789abcdef0123456789abcdeg"));
}

TEST(ReadingSessions, LineRoundTrip) {
  const std::string line = encodeLine(make());
  ASSERT_FALSE(line.empty());
  EXPECT_EQ(line.find('\n'), std::string::npos);
  Session back;
  ASSERT_TRUE(decodeLine(line, back));
  EXPECT_EQ(back.doc, DOC);
  EXPECT_EQ(back.start, 1760000000);
  EXPECT_EQ(back.secs, 600u);
  EXPECT_EQ(back.tz, 180);
  EXPECT_EQ(back.from, 100);
  EXPECT_EQ(back.to, 450);
  EXPECT_EQ(back.title, "Title");
  EXPECT_EQ(back.path, "/books/a.epub");
}

TEST(ReadingSessions, BadDocumentIdGivesNoLine) {
  Session s = make();
  s.doc = "nope";
  EXPECT_TRUE(encodeLine(s).empty());
  Session back;
  EXPECT_FALSE(decodeLine("not json", back));
}

TEST(ReadingSessions, TitleIsCutOnACharacterBoundary) {
  Session s = make();
  s.title = std::string(79, 'a') + "\xD0\xB6\xD0\xB6";  // the 80-byte cut would split the first letter
  Session back;
  ASSERT_TRUE(decodeLine(encodeLine(s), back));
  EXPECT_EQ(back.title.size(), 79u);
}

TEST(ReadingSessions, LongPathIsDroppedInsteadOfTheLine) {
  Session s = make();
  s.path = "/" + std::string(600, 'p');
  const std::string line = encodeLine(s);
  ASSERT_FALSE(line.empty());
  EXPECT_LE(line.size(), MAX_LINE);
  Session back;
  ASSERT_TRUE(decodeLine(line, back));
  EXPECT_TRUE(back.path.empty());
  EXPECT_EQ(back.secs, 600u);
}

TEST(ReadingSessions, UploadableRules) {
  EXPECT_TRUE(uploadable(make(), 1760001000));
  EXPECT_FALSE(uploadable(make(1760000000, 9), 0));            // too short
  EXPECT_FALSE(uploadable(make(1760000000, 8 * 86400), 0));    // too long
  EXPECT_FALSE(uploadable(make(100, 600), 0));                 // clock never set
  EXPECT_FALSE(uploadable(make(1760001000, 600), 1760000000));  // from the future
  EXPECT_TRUE(uploadable(make(1760001000, 600), 0));           // unknown clock: no future check
}

TEST(ReadingSessions, SessionIdContainsStartAndMac) {
  EXPECT_EQ(sessionId(1760000000, MAC), "1760000000-98c377beea30");
}

TEST(ReadingSessions, BatchSkipsInvalidAndDuplicateStarts) {
  std::vector<Session> list = {make(1760000000), make(1760000000), make(1760001000, 5), make(1760002000)};
  std::vector<std::string> ids;
  const std::string body = buildBatch("dev", list, MAC, 1770000000, ids);
  ASSERT_EQ(ids.size(), 2u);
  EXPECT_EQ(ids[0], "1760000000-98c377beea30");
  EXPECT_EQ(ids[1], "1760002000-98c377beea30");
  EXPECT_NE(body.find("\"device_id\":\"dev\""), std::string::npos);
  EXPECT_NE(body.find("\"ended_at\":1760000600"), std::string::npos);
  EXPECT_NE(body.find("\"utc_offset_minutes\":180"), std::string::npos);
  EXPECT_EQ(body.find("title"), std::string::npos);  // titles and paths stay on the device
  EXPECT_EQ(body.find("/books"), std::string::npos);
}

TEST(ReadingSessions, BatchIsLimited) {
  std::vector<Session> list;
  for (int i = 0; i < 30; i++) list.push_back(make(1760000000 + i * 1000));
  std::vector<std::string> ids;
  buildBatch("dev", list, MAC, 1770000000, ids);
  EXPECT_EQ(ids.size(), BATCH_SIZE);
}

TEST(ReadingSessions, AcceptanceNeedsEveryId) {
  const std::vector<std::string> ids = {"a", "b"};
  EXPECT_TRUE(allAccepted(R"({"accepted_session_ids":["b","a","c"]})", ids));
  EXPECT_FALSE(allAccepted(R"({"accepted_session_ids":["a"]})", ids));
  EXPECT_FALSE(allAccepted(R"({"ok":true})", ids));  // an old server that stores nothing
  EXPECT_FALSE(allAccepted("garbage", ids));
}
