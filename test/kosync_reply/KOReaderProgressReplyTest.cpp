#include <KOReaderProgressReply.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {
struct Progress {
  std::string document;
  std::string progress;
  float percentage = -1.0f;
  std::string device;
  std::string deviceId;
  int64_t timestamp = 0;
};

bool read(const char* body, Progress& out) {
  JsonDocument doc;
  if (deserializeJson(doc, body)) return false;
  return kosync::readProgressRecord(doc, "hash", out);
}
}  // namespace

TEST(KOReaderProgressReply, FullRecord) {
  Progress p;
  ASSERT_TRUE(read(R"({"document":"hash","progress":"/body/DocFragment[3]/body/p[2]/text().0","percentage":0.42,
                     "device":"Kobo","device_id":"abc","timestamp":1700000000})", p));
  EXPECT_EQ(p.document, "hash");
  EXPECT_EQ(p.progress, "/body/DocFragment[3]/body/p[2]/text().0");
  EXPECT_FLOAT_EQ(p.percentage, 0.42f);
  EXPECT_EQ(p.device, "Kobo");
  EXPECT_EQ(p.deviceId, "abc");
  EXPECT_EQ(p.timestamp, 1700000000);
}

TEST(KOReaderProgressReply, PercentageOnlyRecordIsRead) {
  Progress p;
  ASSERT_TRUE(read(R"({"progress":null,"percentage":0.5,"device":"Web"})", p));
  EXPECT_EQ(p.progress, "");  // not the string "null"
  EXPECT_FLOAT_EQ(p.percentage, 0.5f);
  EXPECT_EQ(p.deviceId, "");
}

TEST(KOReaderProgressReply, EmptyPayloadsAreNothingStored) {
  Progress p;
  EXPECT_FALSE(read("{}", p));
  EXPECT_FALSE(read("[]", p));
  EXPECT_FALSE(read("\"\"", p));
  EXPECT_FALSE(read("null", p));
  EXPECT_FALSE(read(R"({"progress":null})", p));
  EXPECT_FALSE(read(R"({"position_kinds_available":["percentage"]})", p));
  EXPECT_EQ(p.document, "");  // nothing was written
}

TEST(KOReaderProgressReply, ProgressWithoutPercentageStillCounts) {
  Progress p;
  ASSERT_TRUE(read(R"({"progress":"/body/DocFragment[1]/body/p[1]/text().0"})", p));
  EXPECT_FLOAT_EQ(p.percentage, 0.0f);
}
