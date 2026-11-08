#include <gtest/gtest.h>
#include <util/SerialLineBuffer.h>

#include <string>

namespace {
// Feeds text and returns the lines it completed.
std::vector<std::string> run(SerialLineBuffer& buf, const std::string& text) {
  std::vector<std::string> lines;
  for (const char c : text) {
    if (buf.feed(c)) lines.push_back(buf.take());
  }
  return lines;
}
}  // namespace

TEST(SerialLineBuffer, LineAcrossSeveralPasses) {
  SerialLineBuffer buf;
  EXPECT_TRUE(run(buf, "CMD:SCRE").empty());
  const auto lines = run(buf, "ENSHOT\n");
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "CMD:SCREENSHOT");
}

TEST(SerialLineBuffer, LfCrAndCrlfAllEndALine) {
  SerialLineBuffer buf;
  EXPECT_EQ(run(buf, "a\nb\rc\r\n"), (std::vector<std::string>{"a", "b", "c"}));
}

TEST(SerialLineBuffer, StrayByteWithoutNewlineJustWaits) {
  SerialLineBuffer buf;
  EXPECT_TRUE(run(buf, "x").empty());
  EXPECT_EQ(run(buf, "y\n"), (std::vector<std::string>{"xy"}));
}

TEST(SerialLineBuffer, OverlongLineIsDroppedWhole) {
  SerialLineBuffer buf;
  const std::string noise(SerialLineBuffer::MAX_LINE_BYTES + 50, 'z');
  EXPECT_TRUE(run(buf, noise + "\n").empty());
  EXPECT_EQ(run(buf, "ok\n"), (std::vector<std::string>{"ok"}));
}

TEST(SerialLineBuffer, ResetDropsAPartLine) {
  SerialLineBuffer buf;
  run(buf, "CMD:PA");
  buf.reset();
  EXPECT_EQ(run(buf, "next\n"), (std::vector<std::string>{"next"}));
}
