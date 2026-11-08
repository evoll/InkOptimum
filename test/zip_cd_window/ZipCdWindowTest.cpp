#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "ZipCentralDirWindow.h"

namespace {

// An in-memory file that counts its storage calls, as the SD card's lock-taking calls would be.
class FakeFile {
 public:
  explicit FakeFile(std::vector<uint8_t> bytes) : bytes_(std::move(bytes)) {}
  bool seek(size_t pos) {
    ++calls;
    if (pos > bytes_.size()) return false;
    pos_ = pos;
    return true;
  }
  int read(void* out, size_t n) {
    ++calls;
    const size_t take = std::min(n, bytes_.size() - pos_);
    std::memcpy(out, bytes_.data() + pos_, take);
    pos_ += take;
    return static_cast<int>(take);
  }
  int calls = 0;

 private:
  std::vector<uint8_t> bytes_;
  size_t pos_ = 0;
};

void put16(std::vector<uint8_t>& v, uint16_t x) {
  v.push_back(x & 0xFF);
  v.push_back(x >> 8);
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  for (int i = 0; i < 4; ++i) v.push_back((x >> (8 * i)) & 0xFF);
}

struct Spec {
  std::string name;
  uint32_t size;
  uint16_t extra = 0;
  uint16_t comment = 0;
};

// A central directory of the given entries, preceded by `lead` junk bytes.
std::vector<uint8_t> makeDirectory(const std::vector<Spec>& specs, const size_t lead = 0) {
  std::vector<uint8_t> v(lead, 0xAA);
  uint32_t localOffset = 0;
  for (const auto& s : specs) {
    put32(v, 0x02014b50);
    put16(v, 20);
    put16(v, 20);
    put16(v, 0);
    put16(v, 8);  // method
    put32(v, 0);
    put32(v, 0);
    put32(v, s.size / 2);  // compressed
    put32(v, s.size);      // uncompressed
    put16(v, static_cast<uint16_t>(s.name.size()));
    put16(v, s.extra);
    put16(v, s.comment);
    put16(v, 0);
    put16(v, 0);
    put32(v, 0);
    put32(v, localOffset);
    localOffset += 100;
    v.insert(v.end(), s.name.begin(), s.name.end());
    v.insert(v.end(), s.extra, 0xEE);
    v.insert(v.end(), s.comment, 0xCC);
  }
  return v;
}

std::vector<Spec> manySpecs(const int count) {
  std::vector<Spec> specs;
  for (int i = 0; i < count; ++i) {
    Spec s;
    s.name = "OEBPS/Text/chapter" + std::to_string(i) + ".xhtml";
    s.size = 1000 + i;
    s.extra = static_cast<uint16_t>(i % 7 == 0 ? 24 : 0);
    s.comment = static_cast<uint16_t>(i % 11 == 0 ? 5 : 0);
    specs.push_back(s);
  }
  return specs;
}

std::vector<std::string> walk(FakeFile& file, const size_t offset, const size_t capacity,
                              std::vector<uint32_t>* sizes = nullptr) {
  std::vector<uint8_t> buffer(capacity);
  zip_cd::Window<FakeFile> window(file, buffer.data(), capacity);
  window.begin(static_cast<uint32_t>(offset));
  std::vector<std::string> names;
  zip_cd::Entry e;
  while (window.next(e)) {
    names.emplace_back(e.name ? std::string(e.name, e.nameLen) : std::string("<long>"));
    if (sizes) sizes->push_back(e.uncompressedSize);
  }
  return names;
}

}  // namespace

TEST(ZipCdWindow, ReadsEveryEntryWhateverTheBufferSize) {
  const auto specs = manySpecs(300);
  for (const size_t capacity : {size_t{301}, size_t{512}, size_t{1000}, size_t{4096}, size_t{100000}}) {
    FakeFile file(makeDirectory(specs, 17));
    std::vector<uint32_t> sizes;
    const auto names = walk(file, 17, capacity, &sizes);
    ASSERT_EQ(names.size(), specs.size()) << "capacity " << capacity;
    for (size_t i = 0; i < specs.size(); ++i) {
      EXPECT_EQ(names[i], specs[i].name) << "entry " << i << " capacity " << capacity;
      EXPECT_EQ(sizes[i], specs[i].size);
    }
  }
}

TEST(ZipCdWindow, ReportsEntryPositions) {
  const auto specs = manySpecs(5);
  FakeFile file(makeDirectory(specs, 10));
  std::vector<uint8_t> buffer(512);
  zip_cd::Window<FakeFile> window(file, buffer.data(), buffer.size());
  window.begin(10);
  zip_cd::Entry e;
  uint32_t expected = 10;
  for (const auto& s : specs) {
    ASSERT_TRUE(window.next(e));
    EXPECT_EQ(e.start, expected);
    expected += static_cast<uint32_t>(46 + s.name.size() + s.extra + s.comment);
    EXPECT_EQ(e.nextStart, expected);
  }
}

TEST(ZipCdWindow, CanResumeFromAnEntryStart) {
  const auto specs = manySpecs(40);
  FakeFile file(makeDirectory(specs));
  std::vector<uint8_t> buffer(512);
  zip_cd::Window<FakeFile> window(file, buffer.data(), buffer.size());
  window.begin(0);
  zip_cd::Entry e;
  uint32_t resumeAt = 0;
  for (int i = 0; i <= 12; ++i) {
    ASSERT_TRUE(window.next(e));
    resumeAt = e.nextStart;
  }
  window.begin(resumeAt);
  ASSERT_TRUE(window.next(e));
  EXPECT_EQ(std::string(e.name, e.nameLen), specs[13].name);
}

TEST(ZipCdWindow, StopsAtTheFirstNonEntry) {
  auto bytes = makeDirectory(manySpecs(3));
  const uint8_t eocd[] = {0x50, 0x4b, 0x05, 0x06, 0, 0, 0, 0};
  bytes.insert(bytes.end(), std::begin(eocd), std::end(eocd));
  FakeFile file(bytes);
  EXPECT_EQ(walk(file, 0, 512).size(), 3u);
}

TEST(ZipCdWindow, SkipsOverlongNamesButKeepsWalking) {
  std::vector<Spec> specs = {{"a.xhtml", 1}, {std::string(600, 'x'), 2}, {"c.xhtml", 3}};
  FakeFile file(makeDirectory(specs));
  const auto names = walk(file, 0, 512);
  ASSERT_EQ(names.size(), 3u);
  EXPECT_EQ(names[0], "a.xhtml");
  EXPECT_EQ(names[1], "<long>");
  EXPECT_EQ(names[2], "c.xhtml");
}

TEST(ZipCdWindow, ASkippedLongEntryDoesNotDisturbTheNext) {
  // Extra/comment fields far larger than the buffer, between two ordinary entries.
  std::vector<Spec> specs = {{"first.xhtml", 1}, {"middle.xhtml", 2, 3000, 2000}, {"last.xhtml", 3}};
  FakeFile file(makeDirectory(specs));
  const auto names = walk(file, 0, 512);
  ASSERT_EQ(names.size(), 3u);
  EXPECT_EQ(names[2], "last.xhtml");
}

TEST(ZipCdWindow, TruncatedDirectoryEndsTheWalkCleanly) {
  auto bytes = makeDirectory(manySpecs(10));
  bytes.resize(bytes.size() - 20);  // cut inside the last entry's name
  FakeFile file(bytes);
  const auto names = walk(file, 0, 512);
  EXPECT_LE(names.size(), 10u);
  EXPECT_GE(names.size(), 9u);
}

TEST(ZipCdWindow, StorageCallsGrowWithBlocksNotEntries) {
  const auto specs = manySpecs(2000);
  const auto bytes = makeDirectory(specs);

  FakeFile file(bytes);
  const auto names = walk(file, 0, 4096);
  ASSERT_EQ(names.size(), specs.size());
  // The old walk made about three calls an entry (header, name, skip): ~6000 here.
  const size_t blocks = bytes.size() / 4096 + 2;
  EXPECT_LE(static_cast<size_t>(file.calls), blocks * 2);
  EXPECT_LT(file.calls, 500);
}

TEST(ZipCdWindow, TooSmallABufferReadsNothing) {
  FakeFile file(makeDirectory(manySpecs(3)));
  std::vector<uint8_t> buffer(100);
  zip_cd::Window<FakeFile> window(file, buffer.data(), buffer.size());
  window.begin(0);
  zip_cd::Entry e;
  EXPECT_FALSE(window.next(e));
}
