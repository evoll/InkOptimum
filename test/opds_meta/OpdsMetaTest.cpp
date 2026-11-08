#include <gtest/gtest.h>

#include "OpdsMeta.h"

using namespace opds_meta;

TEST(OpdsMeta, Isbn) {
  auto a = parseIdentifier("urn:isbn:978-0-306-40615-7");
  EXPECT_EQ(a.kind, IdKind::Isbn);
  EXPECT_EQ(a.value, "9780306406157");
  EXPECT_EQ(parseIdentifier("0-306-40615-2").value, "0306406152");
  EXPECT_EQ(parseIdentifier("ISBN:080442957X").value, "080442957X");
  EXPECT_EQ(parseIdentifier("urn:isbn:9780306406158").kind, IdKind::None);  // bad checksum
}

TEST(OpdsMeta, Asin) {
  auto a = parseIdentifier("urn:amazon:b00abcdefg");
  EXPECT_EQ(a.kind, IdKind::Asin);
  EXPECT_EQ(a.value, "B00ABCDEFG");
  EXPECT_EQ(parseIdentifier("asin:xx").kind, IdKind::None);
}

TEST(OpdsMeta, IgnoresOtherIds) {
  EXPECT_EQ(parseIdentifier("urn:uuid:123e4567-e89b-12d3-a456-426614174000").kind, IdKind::None);
  EXPECT_EQ(parseIdentifier("12345").kind, IdKind::None);
  EXPECT_EQ(parseIdentifier("").kind, IdKind::None);
}

TEST(OpdsMeta, Series) {
  EXPECT_EQ(formatSeries("Dune", "3.0"), "Dune #3");
  EXPECT_EQ(formatSeries(" Dune ", "2.5"), "Dune #2.5");
  EXPECT_EQ(formatSeries("Dune", ""), "Dune");
  EXPECT_EQ(formatSeries("", "3"), "");
  EXPECT_EQ(formatSeries("Dune", "0"), "Dune");
}
