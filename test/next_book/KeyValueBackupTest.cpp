#include <gtest/gtest.h>

#include "util/KeyValueBackup.h"

TEST(KeyValueBackup, RoundTrip) {
  const std::vector<std::pair<std::string, uint8_t>> in = {{"a", 1}, {"b", 255}, {"c", 0}};
  const auto text = kv_backup::serialize(in);
  EXPECT_EQ(kv_backup::parse(text), in);
}

TEST(KeyValueBackup, SkipsGarbage) {
  const auto out = kv_backup::parse("# c\n\nx=1\r\nbad\n=3\ny=\nz=300\nw=-1\nq=2x\nok=7");
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], (std::pair<std::string, uint8_t>{"x", 1}));
  EXPECT_EQ(out[1], (std::pair<std::string, uint8_t>{"ok", 7}));
}
