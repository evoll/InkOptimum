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
