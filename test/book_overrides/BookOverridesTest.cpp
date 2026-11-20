#include <gtest/gtest.h>

#define BOOK_OVERRIDES_NO_STORAGE 1
#include "reader/overrides/BookOverrides.cpp"

using namespace book_overrides;

TEST(BookOverrides, SerializeParseRoundTrip) {
  Record r;
  r.present = true;
  r.values = {4, 130};
  const std::string s = serialize(r);
  Record back;
  ASSERT_TRUE(parse(s.data(), s.size(), back));
  EXPECT_TRUE(back.present);
  EXPECT_EQ(back.values, r.values);
}

TEST(BookOverrides, ParseRejectsGarbage) {
  Record r;
  EXPECT_FALSE(parse("hello", 5, r));
  EXPECT_FALSE(parse("v2 1 100\n", 9, r));
  EXPECT_FALSE(parse("v1 1\n", 5, r));
  EXPECT_FALSE(parse("v1 1 10\n", 8, r));   // line spacing below 50%
  EXPECT_FALSE(parse("v1 300 100\n", 11, r));
  EXPECT_FALSE(r.present);
}

TEST(BookOverrides, ApplyAndRestoreKeepsGlobals) {
  Session s;
  Values live{2, 100};
  Record r{true, {4, 130}};
  EXPECT_TRUE(s.apply(live, r));
  EXPECT_EQ(live, (Values{4, 130}));
  EXPECT_TRUE(s.active());
  EXPECT_EQ(s.globals(), (Values{2, 100}));

  // Re-applying a different override must not overwrite the remembered globals.
  Record r2{true, {1, 90}};
  EXPECT_TRUE(s.apply(live, r2));
  EXPECT_EQ(s.globals(), (Values{2, 100}));

  EXPECT_TRUE(s.restore(live));
  EXPECT_EQ(live, (Values{2, 100}));
  EXPECT_FALSE(s.active());
  EXPECT_FALSE(s.restore(live));
}

TEST(BookOverrides, NotPresentRecordRestores) {
  Session s;
  Values live{2, 100};
  s.apply(live, Record{true, {4, 130}});
  EXPECT_TRUE(s.apply(live, Record{}));
  EXPECT_EQ(live, (Values{2, 100}));
  EXPECT_FALSE(s.active());
}

TEST(BookOverrides, NoChangeWhenOverrideEqualsGlobals) {
  Session s;
  Values live{2, 100};
  EXPECT_FALSE(s.apply(live, Record{true, {2, 100}}));
  EXPECT_TRUE(s.active());
}

TEST(BookOverrides, V2RoundTripKeepsEveryField) {
  Record r;
  r.present = true;
  r.values = {3, 120, 12, 2, 0, 1};
  const std::string s = serialize(r);
  EXPECT_EQ(s.substr(0, 3), "v3 ");
  Record back;
  ASSERT_TRUE(parse(s.data(), s.size(), back));
  EXPECT_EQ(back.values, r.values);
}

TEST(BookOverrides, V3RoundTripAndV2FollowsGlobalForNewFields) {
  Record r;
  r.present = true;
  r.values = {3, 120, 12, 2, 0, 1, 20, 0, 1, kInherit};
  const std::string s = serialize(r);
  Record back;
  ASSERT_TRUE(parse(s.data(), s.size(), back));
  EXPECT_EQ(back.values, r.values);
  Record old;
  ASSERT_TRUE(parse("v2 3 100 5 1 1 0\n", 17, old));
  EXPECT_EQ(old.values.screenMarginTop, kInherit);
  EXPECT_EQ(old.values.guideReadingEnabled, kInherit);
  Record bad;
  EXPECT_FALSE(parse("v3 3 100 5 1 1 0 99 0 0 0\n", 26, bad));  // top margin above 60
}

TEST(BookOverrides, V1FileStillReadsAndFollowsGlobalForNewFields) {
  Record back;
  ASSERT_TRUE(parse("v1 4 130\n", 9, back));
  EXPECT_EQ(back.values.fontSize, 4);
  EXPECT_EQ(back.values.lineHeightPercent, 130);
  EXPECT_EQ(back.values.screenMargin, kInherit);
  EXPECT_EQ(back.values.hyphenationEnabled, kInherit);
}

TEST(BookOverrides, V2RejectsOutOfRangeFields) {
  Record r;
  EXPECT_FALSE(parse("v2 3 100 99 1 1 0\n", 18, r));   // margin above 60
  EXPECT_FALSE(parse("v2 3 100 5 20 1 0\n", 18, r));   // alignment above 10
  EXPECT_FALSE(parse("v2 3 100 5 1 1\n", 15, r));      // field missing
  EXPECT_FALSE(r.present);
}

TEST(BookOverrides, InheritedFieldsFollowGlobalsNotPreviousOverride) {
  Session s;
  Values live{2, 100, 5, 1, 1, 0};
  Record margin{true, {2, 100, 20, kInherit, kInherit, kInherit}};
  EXPECT_TRUE(s.apply(live, margin));
  EXPECT_EQ(live.screenMargin, 20);
  EXPECT_EQ(live.paragraphAlignment, 1);

  // A second override that does not touch the margin goes back to the global margin, not the previous 20.
  Record other{true, {2, 100, kInherit, 3, kInherit, kInherit}};
  EXPECT_TRUE(s.apply(live, other));
  EXPECT_EQ(live.screenMargin, 5);
  EXPECT_EQ(live.paragraphAlignment, 3);

  EXPECT_TRUE(s.restore(live));
  EXPECT_EQ(live, (Values{2, 100, 5, 1, 1, 0}));
}
