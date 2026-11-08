#include <gtest/gtest.h>

#include "ReleaseNotesPreview/ReleaseNotesPreview.h"

using release_notes::plainLine;
using release_notes::previewLines;

TEST(ReleaseNotesPreview, StripsHeadingsBulletsAndEmphasis) {
  EXPECT_EQ(plainLine("## What's new"), "What's new");
  EXPECT_EQ(plainLine("- **Library** tabs"), "- Library tabs");
  EXPECT_EQ(plainLine("* use `code` here"), "- use code here");
  EXPECT_EQ(plainLine("  plain text  "), "plain text");
}

TEST(ReleaseNotesPreview, DropsLinesWithoutText) {
  EXPECT_EQ(plainLine(""), "");
  EXPECT_EQ(plainLine("   "), "");
  EXPECT_EQ(plainLine("---"), "");
  EXPECT_EQ(plainLine("```cpp"), "");
  EXPECT_EQ(plainLine("##"), "");
}

TEST(ReleaseNotesPreview, KeepsFirstLinesOnly) {
  const auto lines = previewLines("## Title\n\n- one\n- two\n- three\n- four", 3);
  ASSERT_EQ(lines.size(), 3u);
  EXPECT_EQ(lines[0], "Title");
  EXPECT_EQ(lines[1], "- one");
  EXPECT_EQ(lines[2], "- two");
}

TEST(ReleaseNotesPreview, HandlesEmptyAndNoTrailingNewline) {
  EXPECT_TRUE(previewLines("", 3).empty());
  EXPECT_TRUE(previewLines("\n\n\n", 3).empty());
  const auto lines = previewLines("only line", 3);
  ASSERT_EQ(lines.size(), 1u);
  EXPECT_EQ(lines[0], "only line");
  EXPECT_EQ(previewLines("a\r\nb\r\n", 5).size(), 2u);
}
