#include "XtAppUtil.h"

#include <algorithm>
#include <cstring>

#include "XtApp.h"

namespace xtapp {

namespace {
inline uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
inline uint32_t ror(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

std::string pad(const std::string& d) {
  std::string m = d;
  m += static_cast<char>(0x80);
  while (m.size() % 64 != 56) m += '\0';
  const uint64_t bits = static_cast<uint64_t>(d.size()) * 8;
  for (int i = 7; i >= 0; --i) m += static_cast<char>((bits >> (i * 8)) & 0xFF);
  return m;
}
}  // namespace

std::string sha1(const std::string& data) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  const std::string m = pad(data);
  for (size_t o = 0; o < m.size(); o += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint8_t>(m[o + i * 4]) << 24) | (static_cast<uint8_t>(m[o + i * 4 + 1]) << 16) |
             (static_cast<uint8_t>(m[o + i * 4 + 2]) << 8) | static_cast<uint8_t>(m[o + i * 4 + 3]);
    }
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out;
  for (uint32_t v : h)
    for (int i = 3; i >= 0; --i) out += static_cast<char>((v >> (i * 8)) & 0xFF);
  return out;
}

std::string sha256(const std::string& data) {
  static const uint32_t K[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98,
      0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8,
      0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819,
      0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
      0xc67178f2};
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  const std::string m = pad(data);
  for (size_t o = 0; o < m.size(); o += 64) {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<uint8_t>(m[o + i * 4]) << 24) | (static_cast<uint8_t>(m[o + i * 4 + 1]) << 16) |
             (static_cast<uint8_t>(m[o + i * 4 + 2]) << 8) | static_cast<uint8_t>(m[o + i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i) {
      const uint32_t s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
      const uint32_t s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t v[8];
    memcpy(v, h, sizeof(v));
    for (int i = 0; i < 64; ++i) {
      const uint32_t S1 = ror(v[4], 6) ^ ror(v[4], 11) ^ ror(v[4], 25);
      const uint32_t ch = (v[4] & v[5]) ^ (~v[4] & v[6]);
      const uint32_t t1 = v[7] + S1 + ch + K[i] + w[i];
      const uint32_t S0 = ror(v[0], 2) ^ ror(v[0], 13) ^ ror(v[0], 22);
      const uint32_t mj = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);
      const uint32_t t2 = S0 + mj;
      v[7] = v[6]; v[6] = v[5]; v[5] = v[4]; v[4] = v[3] + t1; v[3] = v[2]; v[2] = v[1]; v[1] = v[0]; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; ++i) h[i] += v[i];
  }
  std::string out;
  for (uint32_t x : h)
    for (int i = 3; i >= 0; --i) out += static_cast<char>((x >> (i * 8)) & 0xFF);
  return out;
}

// ------------------------------------------------------------------------------------------------ inflate

namespace {
struct Bits {
  const uint8_t* p;
  size_t n, pos = 0;
  uint32_t buf = 0;
  int cnt = 0;
  bool err = false;
  uint32_t get(int need) {
    while (cnt < need) {
      if (pos >= n) { err = true; return 0; }
      buf |= static_cast<uint32_t>(p[pos++]) << cnt;
      cnt += 8;
    }
    const uint32_t v = buf & ((1u << need) - 1);
    buf >>= need;
    cnt -= need;
    return v;
  }
};

struct Huff {
  uint16_t count[16];
  uint16_t symbol[288];
};

void build(Huff& h, const uint8_t* lens, int n) {
  memset(h.count, 0, sizeof(h.count));
  for (int i = 0; i < n; ++i) h.count[lens[i]]++;
  h.count[0] = 0;
  uint16_t offs[16];
  offs[1] = 0;
  for (int i = 1; i < 15; ++i) offs[i + 1] = offs[i] + h.count[i];
  for (int i = 0; i < n; ++i)
    if (lens[i]) h.symbol[offs[lens[i]]++] = static_cast<uint16_t>(i);
}

int decodeSym(Bits& b, const Huff& h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; ++len) {
    code |= static_cast<int>(b.get(1));
    if (b.err) return -1;
    const int c = h.count[len];
    if (code - c < first) return h.symbol[index + (code - first)];
    index += c;
    first += c;
    first <<= 1;
    code <<= 1;
  }
  return -1;
}

const uint16_t kLenBase[] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const uint16_t kLenExtra[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const uint16_t kDistBase[] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const uint16_t kDistExtra[] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
}  // namespace

bool inflateData(const std::string& in, std::string& out, size_t maxOut) {
  out.clear();
  const uint8_t* p = reinterpret_cast<const uint8_t*>(in.data());
  size_t n = in.size();
  if (n >= 2 && (p[0] & 0x0F) == 8 && ((p[0] << 8) | p[1]) % 31 == 0) {
    p += 2;
    n -= 2;
  }
  Bits b{p, n};
  bool last;
  do {
    last = b.get(1);
    const uint32_t type = b.get(2);
    if (b.err) return false;
    if (type == 0) {
      b.buf = 0;
      b.cnt = 0;
      if (b.pos + 4 > b.n) return false;
      const uint32_t len = b.p[b.pos] | (b.p[b.pos + 1] << 8);
      const uint32_t nlen = b.p[b.pos + 2] | (b.p[b.pos + 3] << 8);
      b.pos += 4;
      if ((len ^ 0xFFFF) != nlen || b.pos + len > b.n || out.size() + len > maxOut) return false;
      out.append(reinterpret_cast<const char*>(b.p + b.pos), len);
      b.pos += len;
    } else if (type == 1 || type == 2) {
      static Huff dummy;
      Huff lit, dist;
      if (type == 1) {
        uint8_t l[288];
        for (int i = 0; i < 144; ++i) l[i] = 8;
        for (int i = 144; i < 256; ++i) l[i] = 9;
        for (int i = 256; i < 280; ++i) l[i] = 7;
        for (int i = 280; i < 288; ++i) l[i] = 8;
        build(lit, l, 288);
        uint8_t d[30];
        memset(d, 5, sizeof(d));
        build(dist, d, 30);
      } else {
        const int nlen = static_cast<int>(b.get(5)) + 257, ndist = static_cast<int>(b.get(5)) + 1,
                  ncode = static_cast<int>(b.get(4)) + 4;
        if (b.err || nlen > 286 || ndist > 30) return false;
        static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        uint8_t lens[320];
        memset(lens, 0, sizeof(lens));
        for (int i = 0; i < ncode; ++i) lens[order[i]] = static_cast<uint8_t>(b.get(3));
        Huff lc;
        build(lc, lens, 19);
        uint8_t all[320];
        memset(all, 0, sizeof(all));
        int idx = 0;
        while (idx < nlen + ndist) {
          const int sym = decodeSym(b, lc);
          if (sym < 0) return false;
          if (sym < 16) {
            all[idx++] = static_cast<uint8_t>(sym);
          } else {
            int rep, val = 0;
            if (sym == 16) {
              if (idx == 0) return false;
              val = all[idx - 1];
              rep = 3 + static_cast<int>(b.get(2));
            } else if (sym == 17) {
              rep = 3 + static_cast<int>(b.get(3));
            } else {
              rep = 11 + static_cast<int>(b.get(7));
            }
            if (idx + rep > nlen + ndist) return false;
            while (rep--) all[idx++] = static_cast<uint8_t>(val);
          }
        }
        build(lit, all, nlen);
        build(dist, all + nlen, ndist);
      }
      (void)dummy;
      for (;;) {
        const int sym = decodeSym(b, lit);
        if (sym < 0) return false;
        if (sym < 256) {
          if (out.size() >= maxOut) return false;
          out += static_cast<char>(sym);
        } else if (sym == 256) {
          break;
        } else {
          const int li = sym - 257;
          if (li >= 29) return false;
          const int len = kLenBase[li] + static_cast<int>(b.get(kLenExtra[li]));
          const int ds = decodeSym(b, dist);
          if (ds < 0 || ds >= 30) return false;
          const size_t d = kDistBase[ds] + b.get(kDistExtra[ds]);
          if (b.err || d > out.size() || out.size() + len > maxOut) return false;
          for (int i = 0; i < len; ++i) out += out[out.size() - d];
        }
      }
    } else {
      return false;
    }
    if (b.err) return false;
  } while (!last);
  return true;
}

// ------------------------------------------------------------------------------------------------ image blit

void blitImage(Canvas& c, const ImageData& img, int x, int y, int dw, int dh) {
  if (img.w <= 0 || img.h <= 0 || dw <= 0 || dh <= 0 || img.gray.size() < static_cast<size_t>(img.w) * img.h) return;
  static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(c.width(), x + dw), y1 = std::min(c.height(), y + dh);
  for (int yy = y0; yy < y1; ++yy) {
    const int sy = static_cast<int>((static_cast<int64_t>(yy - y) * img.h) / dh);
    const uint8_t* row = &img.gray[static_cast<size_t>(sy) * img.w];
    for (int xx = x0; xx < x1; ++xx) {
      const int sx = static_cast<int>((static_cast<int64_t>(xx - x) * img.w) / dw);
      const int v = row[sx];
      const int thr = bayer[yy & 3][xx & 3] * 16 + 8;
      c.pixel(xx, yy, v < thr);
    }
  }
}

}  // namespace xtapp
