#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "GifDecoder.h"

namespace {

// Minimal GIF LZW encoder (test-only) producing a valid sub-blocked stream.
std::vector<uint8_t> lzwEncode(const std::vector<uint8_t>& px, int minCode) {
  std::vector<uint8_t> bits;
  uint32_t acc = 0;
  int nacc = 0;
  auto put = [&](int code, int size) {
    acc |= static_cast<uint32_t>(code) << nacc;
    nacc += size;
    while (nacc >= 8) {
      bits.push_back(acc & 0xFF);
      acc >>= 8;
      nacc -= 8;
    }
  };
  const int clear = 1 << minCode, eoi = clear + 1;
  std::map<std::string, int> dict;
  auto reset = [&] {
    dict.clear();
    for (int i = 0; i < clear; i++) dict[std::string(1, static_cast<char>(i))] = i;
  };
  reset();
  int next = eoi + 1, size = minCode + 1;
  put(clear, size);
  std::string cur;
  for (uint8_t p : px) {
    std::string nx = cur + static_cast<char>(p);
    if (dict.count(nx)) {
      cur = nx;
      continue;
    }
    put(dict[cur], size);
    if (next < 4096) {
      dict[nx] = next++;
      if (next > (1 << size) && size < 12) size++;
    } else {
      put(clear, size);
      reset();
      next = eoi + 1;
      size = minCode + 1;
    }
    cur = std::string(1, static_cast<char>(p));
  }
  if (!cur.empty()) put(dict[cur], size);
  put(eoi, size);
  if (nacc > 0) bits.push_back(acc & 0xFF);

  std::vector<uint8_t> out;
  out.push_back(static_cast<uint8_t>(minCode));
  for (size_t i = 0; i < bits.size(); i += 255) {
    const size_t n = std::min<size_t>(255, bits.size() - i);
    out.push_back(static_cast<uint8_t>(n));
    out.insert(out.end(), bits.begin() + i, bits.begin() + i + n);
  }
  out.push_back(0);
  return out;
}

std::vector<uint8_t> makeGif(int w, int h, const std::vector<uint8_t>& indices, bool interlaced = false,
                             int transparent = -1) {
  std::vector<uint8_t> g = {'G', 'I', 'F', '8', '9', 'a', uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8),
                            0xF1, 0, 0};  // GCT, 4 colours
  const uint8_t pal[4][3] = {{0, 0, 0}, {255, 255, 255}, {255, 0, 0}, {0, 0, 255}};
  for (auto& c : pal) g.insert(g.end(), c, c + 3);
  if (transparent >= 0) {
    g.insert(g.end(), {0x21, 0xF9, 4, 1, 0, 0, uint8_t(transparent), 0});
  }
  g.insert(g.end(), {0x2C, 0, 0, 0, 0, uint8_t(w), uint8_t(w >> 8), uint8_t(h), uint8_t(h >> 8),
                     uint8_t(interlaced ? 0x40 : 0)});
  auto data = lzwEncode(indices, 2);
  g.insert(g.end(), data.begin(), data.end());
  g.push_back(0x3B);
  return g;
}

struct Collected {
  std::vector<uint8_t> pixels;
  std::vector<int> ys;
  int w = 0, h = 0;
};

int decodeAll(const std::vector<uint8_t>& gif, Collected& c) {
  auto* d = new GifDecoder();
  MemoryGifSource src(gif.data(), gif.size());
  int rows = -2;
  if (d->open(src)) {
    c.w = d->frameWidth();
    c.h = d->frameHeight();
    c.pixels.assign(c.w * c.h, 0xEE);
    std::vector<uint8_t> row(c.w);
    struct Ctx {
      Collected* c;
    } ctx{&c};
    rows = d->decode(
        src, row.data(),
        [](void* u, int y, const uint8_t* idx, int w) {
          auto* cc = static_cast<Ctx*>(u)->c;
          cc->ys.push_back(y);
          std::copy(idx, idx + w, cc->pixels.begin() + y * w);
          return true;
        },
        &ctx);
  }
  delete d;
  return rows;
}

}  // namespace

TEST(Gif, ParseDimensions) {
  const uint8_t hdr[10] = {'G', 'I', 'F', '8', '9', 'a', 0x34, 0x01, 0x20, 0x00};
  int w = 0, h = 0;
  ASSERT_TRUE(GifDecoder::parseDimensions(hdr, 10, w, h));
  EXPECT_EQ(w, 308);
  EXPECT_EQ(h, 32);
  EXPECT_FALSE(GifDecoder::parseDimensions(hdr, 9, w, h));
  const uint8_t bad[10] = {'P', 'N', 'G'};
  EXPECT_FALSE(GifDecoder::parseDimensions(bad, 10, w, h));
  const uint8_t zero[10] = {'G', 'I', 'F', '8', '9', 'a', 0, 0, 1, 0};
  EXPECT_FALSE(GifDecoder::parseDimensions(zero, 10, w, h));
}

TEST(Gif, RoundTripSmall) {
  std::vector<uint8_t> px = {0, 1, 2, 3, 3, 2, 1, 0, 0, 0, 1, 1};
  Collected c;
  EXPECT_EQ(decodeAll(makeGif(4, 3, px), c), 3);
  EXPECT_EQ(c.pixels, px);
}

TEST(Gif, RoundTripLargeForcesTableGrowthAndReset) {
  const int w = 200, h = 150;
  std::vector<uint8_t> px(w * h);
  uint32_t s = 12345;
  for (auto& p : px) {
    s = s * 1103515245u + 12345u;
    p = (s >> 16) & 3;
  }
  Collected c;
  EXPECT_EQ(decodeAll(makeGif(w, h, px), c), h);
  EXPECT_EQ(c.pixels, px);
}

TEST(Gif, RunsExerciseKwKwK) {
  std::vector<uint8_t> px(64 * 8, 2);
  Collected c;
  EXPECT_EQ(decodeAll(makeGif(64, 8, px), c), 8);
  EXPECT_EQ(c.pixels, px);
}

TEST(Gif, InterlacedRowOrder) {
  const int w = 3, h = 10;
  std::vector<uint8_t> stored(w * h);
  // Storage order is pass order: rows 0,8 | 4 | 2,6 | 1,3,5,7,9.
  const int order[] = {0, 8, 4, 2, 6, 1, 3, 5, 7, 9};
  for (int i = 0; i < h; i++)
    for (int x = 0; x < w; x++) stored[i * w + x] = order[i] & 3;
  Collected c;
  EXPECT_EQ(decodeAll(makeGif(w, h, stored, true), c), h);
  ASSERT_EQ(c.ys.size(), static_cast<size_t>(h));
  for (int i = 0; i < h; i++) EXPECT_EQ(c.ys[i], order[i]);
  for (int y = 0; y < h; y++) EXPECT_EQ(c.pixels[y * w], y & 3);
}

TEST(Gif, GrayAndTransparency) {
  std::vector<uint8_t> px = {0, 1, 2, 3};
  auto gif = makeGif(4, 1, px, false, 3);
  auto* d = new GifDecoder();
  MemoryGifSource src(gif.data(), gif.size());
  ASSERT_TRUE(d->open(src));
  uint8_t g[4];
  d->toGray(px.data(), g, 4);
  EXPECT_EQ(g[0], 0);
  EXPECT_GE(g[1], 254);
  EXPECT_NEAR(g[2], 77 * 255 >> 8, 1);
  EXPECT_EQ(g[3], 255);  // transparent index -> white
  delete d;
}

TEST(Gif, TruncatedAndGarbageDoNotCrash) {
  std::vector<uint8_t> px(20 * 20, 1);
  auto gif = makeGif(20, 20, px);
  for (size_t cut = 0; cut < gif.size(); cut += 3) {
    std::vector<uint8_t> t(gif.begin(), gif.begin() + cut);
    Collected c;
    int rows = decodeAll(t, c);
    EXPECT_LE(rows, 20);
  }
  uint32_t s = 7;
  for (int iter = 0; iter < 200; iter++) {
    auto g = gif;
    for (int k = 0; k < 8; k++) {
      s = s * 1664525u + 1013904223u;
      g[13 + (s >> 8) % (g.size() - 13)] = s & 0xFF;
    }
    Collected c;
    decodeAll(g, c);
  }
}

TEST(Gif, AbortFromCallback) {
  std::vector<uint8_t> px(8 * 8, 0);
  auto gif = makeGif(8, 8, px);
  auto* d = new GifDecoder();
  MemoryGifSource src(gif.data(), gif.size());
  ASSERT_TRUE(d->open(src));
  std::vector<uint8_t> row(8);
  int rows = d->decode(src, row.data(), [](void*, int, const uint8_t*, int) { return false; }, nullptr);
  EXPECT_EQ(rows, -1);
  delete d;
}

TEST(Gif, NoImageFails) {
  std::vector<uint8_t> g = {'G', 'I', 'F', '8', '9', 'a', 1, 0, 1, 0, 0, 0, 0, 0x3B};
  auto* d = new GifDecoder();
  MemoryGifSource src(g.data(), g.size());
  EXPECT_FALSE(d->open(src));
  delete d;
}
