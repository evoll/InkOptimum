#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "ZipFile.h"

namespace {

void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}

struct Item {
  std::string name;
  std::string data;
};

// A ZIP with every entry stored (no compression), `prefix` junk bytes in front of the first header.
std::vector<uint8_t> makeZip(const std::vector<Item>& items, const size_t prefix = 0) {
  std::vector<uint8_t> out(prefix, '\r');
  std::vector<uint32_t> offsets;
  for (const auto& it : items) {
    offsets.push_back(static_cast<uint32_t>(out.size() - prefix));
    put32(out, 0x04034b50);
    put16(out, 20);
    put16(out, 0);
    put16(out, 0);  // stored
    put32(out, 0);
    put32(out, 0);
    put32(out, static_cast<uint32_t>(it.data.size()));
    put32(out, static_cast<uint32_t>(it.data.size()));
    put16(out, static_cast<uint16_t>(it.name.size()));
    put16(out, 0);
    out.insert(out.end(), it.name.begin(), it.name.end());
    out.insert(out.end(), it.data.begin(), it.data.end());
  }
  const uint32_t cdStart = static_cast<uint32_t>(out.size() - prefix);
  for (size_t i = 0; i < items.size(); ++i) {
    const auto& it = items[i];
    put32(out, 0x02014b50);
    put16(out, 20);
    put16(out, 20);
    put16(out, 0);
    put16(out, 0);  // stored
    put32(out, 0);
    put32(out, 0);
    put32(out, static_cast<uint32_t>(it.data.size()));
    put32(out, static_cast<uint32_t>(it.data.size()));
    put16(out, static_cast<uint16_t>(it.name.size()));
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put16(out, 0);
    put32(out, 0);
    put32(out, offsets[i]);
    out.insert(out.end(), it.name.begin(), it.name.end());
  }
  const uint32_t cdSize = static_cast<uint32_t>(out.size() - prefix) - cdStart;
  put32(out, 0x06054b50);
  put16(out, 0);
  put16(out, 0);
  put16(out, static_cast<uint16_t>(items.size()));
  put16(out, static_cast<uint16_t>(items.size()));
  put32(out, cdSize);
  put32(out, cdStart);
  put16(out, 0);
  return out;
}

std::string writeTemp(const std::vector<uint8_t>& bytes, const char* tag) {
  const std::string path = std::string("/tmp/zipfile_test_") + tag + ".zip";
  FILE* f = std::fopen(path.c_str(), "wb");
  std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  return path;
}

std::vector<Item> chapters(const int n) {
  std::vector<Item> items;
  items.push_back({"mimetype", "application/epub+zip"});
  for (int i = 0; i < n; ++i) {
    items.push_back({"OEBPS/Text/ch" + std::to_string(i) + ".xhtml", std::string(100 + i, static_cast<char>('a' + i % 26))});
  }
  return items;
}

}  // namespace

TEST(ZipFileWindow, SizesInDirectoryOrderAndOutOfOrder) {
  const auto items = chapters(400);
  const std::string path = writeTemp(makeZip(items), "sizes");
  ZipFile zip(path);

  for (size_t i = 0; i < items.size(); ++i) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(items[i].name.c_str(), &size)) << items[i].name;
    EXPECT_EQ(size, items[i].data.size());
  }
  // Out of order: forces the wrap-around of the resumed walk.
  for (const int i : {300, 3, 399, 0, 150, 149, 151}) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(items[i].name.c_str(), &size)) << i;
    EXPECT_EQ(size, items[i].data.size());
  }
  size_t size = 0;
  EXPECT_FALSE(zip.getInflatedFileSize("OEBPS/missing.xhtml", &size));
  // A miss must not poison later lookups.
  EXPECT_TRUE(zip.getInflatedFileSize(items[7].name.c_str(), &size));
  EXPECT_EQ(size, items[7].data.size());
}

TEST(ZipFileWindow, NameMatchIsExact) {
  const std::vector<Item> items = {{"a/b.xhtml", "1"}, {"a/b.xhtml.bak", "22"}, {"a/b.xht", "333"}, {"a/b.xhtml2", "4444"}};
  const std::string path = writeTemp(makeZip(items), "exact");
  ZipFile zip(path);
  size_t size = 0;
  ASSERT_TRUE(zip.getInflatedFileSize("a/b.xhtml", &size));
  EXPECT_EQ(size, 1u);
  ASSERT_TRUE(zip.getInflatedFileSize("a/b.xht", &size));
  EXPECT_EQ(size, 3u);
  EXPECT_FALSE(zip.getInflatedFileSize("a/b.xhtm", &size));
}

TEST(ZipFileWindow, ReadsStoredContent) {
  const auto items = chapters(50);
  const std::string path = writeTemp(makeZip(items), "content");
  ZipFile zip(path);
  for (const int i : {0, 17, 49}) {
    size_t size = 0;
    uint8_t* data = zip.readFileToMemory(items[i].name.c_str(), &size);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(std::string(reinterpret_cast<char*>(data), size), items[i].data);
    free(data);
  }
}

TEST(ZipFileWindow, ArchiveWithAPrefixStillResolves) {
  const auto items = chapters(20);
  const std::string path = writeTemp(makeZip(items, 2), "prefix");
  ZipFile zip(path);
  size_t size = 0;
  uint8_t* data = zip.readFileToMemory(items[5].name.c_str(), &size);
  ASSERT_NE(data, nullptr);
  EXPECT_EQ(std::string(reinterpret_cast<char*>(data), size), items[5].data);
  free(data);
}

TEST(ZipFileWindow, EnumeratesEveryPath) {
  const auto items = chapters(300);
  const std::string path = writeTemp(makeZip(items), "enum");
  ZipFile zip(path);
  std::vector<std::string> seen;
  ASSERT_TRUE(zip.enumerateFilePaths([&](std::string_view name) { seen.emplace_back(name); }));
  ASSERT_EQ(seen.size(), items.size());
  for (size_t i = 0; i < items.size(); ++i) EXPECT_EQ(seen[i], items[i].name);
}

TEST(ZipFileWindow, StatCacheMatchesDirectLookups) {
  const auto items = chapters(120);
  const std::string path = writeTemp(makeZip(items), "cache");
  ZipFile zip(path);
  ASSERT_TRUE(zip.loadAllFileStatSlims());
  for (const auto& it : items) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(it.name.c_str(), &size));
    EXPECT_EQ(size, it.data.size());
  }
}

TEST(ZipFileWindow, BatchSizeLookup) {
  const auto items = chapters(200);
  const std::string path = writeTemp(makeZip(items), "batch");
  ZipFile zip(path);

  std::deque<ZipFile::SizeTarget> targets;
  std::deque<uint32_t> sizes(items.size(), 0);
  for (size_t i = 1; i < items.size(); i += 3) {
    const auto& n = items[i].name;
    targets.push_back({ZipFile::fnvHash64(n.data(), n.size()), static_cast<uint16_t>(n.size()), static_cast<uint16_t>(i)});
  }
  std::sort(targets.begin(), targets.end(), [](const auto& a, const auto& b) {
    return a.hash < b.hash || (a.hash == b.hash && a.len < b.len);
  });
  const int matched = zip.fillUncompressedSizes(targets, sizes);
  EXPECT_EQ(matched, static_cast<int>(targets.size()));
  for (size_t i = 1; i < items.size(); i += 3) EXPECT_EQ(sizes[i], items[i].data.size());
  EXPECT_EQ(sizes[0], 0u);
}

TEST(ZipFileWindow, WalksCostBlocksNotEntries) {
  const auto items = chapters(3000);
  const std::string path = writeTemp(makeZip(items), "calls");
  ZipFile zip(path);

  g_halCalls = 0;
  std::set<std::string> names;
  ASSERT_TRUE(zip.enumerateFilePaths([&](std::string_view n) { names.emplace(n); }));
  const int enumerateCalls = g_halCalls;
  EXPECT_EQ(names.size(), items.size());
  // The directory is ~160 KB: about 40 blocks plus the end-of-archive search, nowhere near 3 calls an entry.
  EXPECT_LT(enumerateCalls, 120);

  // Open archive, lookups in directory order: each resumes where the previous one ended.
  ASSERT_TRUE(zip.open());
  g_halCalls = 0;
  for (size_t i = 0; i < items.size(); i += 2) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(items[i].name.c_str(), &size));
  }
  EXPECT_LE(g_halCalls, 2 * 1501 + 50);  // 1501 ordered lookups, one seek+read block each
  zip.close();

  // Closed between lookups the resume position is lost (close() resets it), so each lookup walks from the
  // start - still in 4 KB blocks, never several storage calls per entry.
  g_halCalls = 0;
  for (size_t i = 0; i < items.size(); i += 100) {
    size_t size = 0;
    ASSERT_TRUE(zip.getInflatedFileSize(items[i].name.c_str(), &size));
  }
  EXPECT_LT(g_halCalls, 30 * 60);
}
