#include <gtest/gtest.h>

#include <cstring>

#include "CssStyle.h"

namespace {
uint32_t h(const char* s) { return cssFamilyHash(s, strlen(s)); }
}  // namespace

TEST(CssFontFamily, CaseAndQuotesFold) {
  EXPECT_EQ(h("Literata"), h("literata"));
  EXPECT_EQ(h("Literata"), h("'Literata'"));
  EXPECT_EQ(h("Literata"), h("  \"LITERATA\" "));
  EXPECT_NE(h("Literata"), h("Literata Bold"));
}

TEST(CssFontFamily, EmptyIsZero) {
  EXPECT_EQ(h(""), 0u);
  EXPECT_EQ(h("  ''  "), 0u);
  EXPECT_NE(h("a"), 0u);
}

TEST(CssFontFamily, ApplyOverKeepsInherited) {
  CssStyle base;
  base.fontFamily = h("Serif One");
  CssStyle none;
  base.applyOver(none);
  EXPECT_EQ(base.fontFamily, h("Serif One"));
  CssStyle other;
  other.fontFamily = h("Other");
  base.applyOver(other);
  EXPECT_EQ(base.fontFamily, h("Other"));
}
