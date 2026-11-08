#include <FileReplace.h>
#include <gtest/gtest.h>

#include <map>
#include <string>

namespace {
struct FakeFs {
  std::map<std::string, std::string> files;
  std::string failRenameTo;  // a rename whose target is this path fails
  bool exists(const char* p) { return files.count(p) != 0; }
  bool remove(const char* p) { return files.erase(p) != 0; }
  bool rename(const char* a, const char* b) {
    if (failRenameTo == b || !files.count(a)) return false;
    files[b] = files[a];
    files.erase(a);
    return true;
  }
};
}  // namespace

TEST(FileReplace, NoExistingFileJustRenames) {
  FakeFs fs;
  fs.files["/a.tmp"] = "new";
  EXPECT_TRUE(FileReplace::replace(fs, "/a.tmp", "/a"));
  EXPECT_EQ(fs.files["/a"], "new");
  EXPECT_FALSE(fs.exists("/a.tmp"));
}

TEST(FileReplace, ReplacesAndDropsBackup) {
  FakeFs fs;
  fs.files["/a"] = "old";
  fs.files["/a.tmp"] = "new";
  EXPECT_TRUE(FileReplace::replace(fs, "/a.tmp", "/a"));
  EXPECT_EQ(fs.files["/a"], "new");
  EXPECT_FALSE(fs.exists("/a.old"));
  EXPECT_FALSE(fs.exists("/a.tmp"));
}

TEST(FileReplace, FailedInstallKeepsOldContent) {
  struct OneShot : FakeFs {
    int calls = 0;
    bool rename(const char* a, const char* b) {
      if (++calls == 2) return false;  // tmp -> dest fails, the restore (call 3) works
      return FakeFs::rename(a, b);
    }
  } fs;
  fs.files["/a"] = "old";
  fs.files["/a.tmp"] = "new";
  EXPECT_FALSE(FileReplace::replace(fs, "/a.tmp", "/a"));
  EXPECT_EQ(fs.files["/a"], "old");
  EXPECT_EQ(fs.files["/a.tmp"], "new");
}

TEST(FileReplace, BackupRenameFailureLeavesEverythingAsIs) {
  FakeFs fs;
  fs.files["/a"] = "old";
  fs.files["/a.tmp"] = "new";
  fs.failRenameTo = "/a.old";
  EXPECT_FALSE(FileReplace::replace(fs, "/a.tmp", "/a"));
  EXPECT_EQ(fs.files["/a"], "old");
}

TEST(FileReplace, MissingTempFails) {
  FakeFs fs;
  fs.files["/a"] = "old";
  EXPECT_FALSE(FileReplace::replace(fs, "/a.tmp", "/a"));
  EXPECT_EQ(fs.files["/a"], "old");
}
