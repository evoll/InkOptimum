#include <ImageEntryName.h>
#include <gtest/gtest.h>

TEST(ImageEntryName, NameWithoutExtension) {
  EXPECT_TRUE(hasNoFileExtension("images/img_32"));
  EXPECT_TRUE(hasNoFileExtension("img_32"));
  EXPECT_TRUE(hasNoFileExtension("img.d/img_35"));  // a dot in a folder name is no extension
}

TEST(ImageEntryName, NameWithExtension) {
  EXPECT_FALSE(hasNoFileExtension("images/cover.jpg"));
  EXPECT_FALSE(hasNoFileExtension("a.b/cover.svg"));
  EXPECT_FALSE(hasNoFileExtension("cover.PNG"));
}
