#include <BufferedWriter.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

struct FakeFile {
  std::string data;
  int calls = 0;
  size_t failAfterBytes = static_cast<size_t>(-1);  // accept this many bytes in total, then write short

  size_t write(const uint8_t* p, size_t len) {
    ++calls;
    const size_t room = failAfterBytes > data.size() ? failAfterBytes - data.size() : 0;
    const size_t n = len < room ? len : room;
    data.append(reinterpret_cast<const char*>(p), n);
    return n;
  }
};

}  // namespace

TEST(BufferedWriter, BytesComeOutInOrderAndIdentical) {
  FakeFile f;
  BufferedWriter<FakeFile, 16> w(f);
  std::string expected;
  for (int i = 0; i < 100; ++i) {
    const uint8_t b = static_cast<uint8_t>(i * 7);
    ASSERT_TRUE(w.writeByte(b));
    expected.push_back(static_cast<char>(b));
    const std::string chunk(static_cast<size_t>(i % 11), static_cast<char>('a' + i % 26));
    ASSERT_TRUE(w.write(chunk.data(), chunk.size()));
    expected += chunk;
  }
  ASSERT_TRUE(w.flush());
  EXPECT_EQ(f.data, expected);
}

TEST(BufferedWriter, ManySmallWritesBecomeFewCalls) {
  FakeFile f;
  BufferedWriter<FakeFile, 1024> w(f);
  for (int i = 0; i < 5000; ++i) ASSERT_TRUE(w.writeByte(1));
  ASSERT_TRUE(w.flush());
  EXPECT_EQ(f.data.size(), 5000u);
  EXPECT_EQ(f.calls, 5);  // 4 full blocks + the tail
}

TEST(BufferedWriter, NothingIsWrittenBeforeFlushUnlessFull) {
  FakeFile f;
  BufferedWriter<FakeFile, 64> w(f);
  ASSERT_TRUE(w.write("abc", 3));
  EXPECT_EQ(f.calls, 0);
  ASSERT_TRUE(w.flush());
  EXPECT_EQ(f.data, "abc");
  ASSERT_TRUE(w.flush());  // empty flush is a no-op
  EXPECT_EQ(f.calls, 1);
}

TEST(BufferedWriter, LargeBlockGoesStraightThroughAfterBufferedBytes) {
  FakeFile f;
  BufferedWriter<FakeFile, 16> w(f);
  ASSERT_TRUE(w.write("xy", 2));
  const std::string big(100, 'z');
  ASSERT_TRUE(w.write(big.data(), big.size()));
  ASSERT_TRUE(w.flush());
  EXPECT_EQ(f.data, "xy" + big);
}

TEST(BufferedWriter, ExactBlockSizeIsOneCall) {
  FakeFile f;
  BufferedWriter<FakeFile, 16> w(f);
  const std::string block(16, 'q');
  ASSERT_TRUE(w.write(block.data(), block.size()));
  EXPECT_EQ(f.calls, 1);
  EXPECT_EQ(f.data, block);
}

TEST(BufferedWriter, ShortWriteFailsForGood) {
  FakeFile f;
  f.failAfterBytes = 20;
  BufferedWriter<FakeFile, 16> w(f);
  bool anyFailed = false;
  for (int i = 0; i < 10; ++i) anyFailed |= !w.write("0123456789", 10);
  EXPECT_TRUE(anyFailed);
  EXPECT_FALSE(w.ok());
  EXPECT_FALSE(w.flush());
  EXPECT_FALSE(w.writeByte(1));
  const int callsAtFailure = f.calls;
  w.write("more", 4);
  EXPECT_EQ(f.calls, callsAtFailure);  // no further storage calls once failed
}

TEST(BufferedWriter, FailureOnFinalFlushIsReported) {
  FakeFile f;
  f.failAfterBytes = 2;
  BufferedWriter<FakeFile, 64> w(f);
  ASSERT_TRUE(w.write("abcdef", 6));  // still buffered
  EXPECT_FALSE(w.flush());
  EXPECT_FALSE(w.ok());
}
