#include "PalmDoc.h"

namespace mobi {

bool palmDocDecompress(const uint8_t* in, size_t len, std::string& out, size_t maxOut) {
  const size_t base = out.size();
  size_t p = 0;
  while (p < len) {
    const unsigned c = in[p++];
    if (c >= 1 && c <= 8) {
      const size_t n = c;
      if (p + n > len) return true;  // truncated literal run: keep what we have
      if (out.size() - base + n > maxOut) return false;
      out.append(reinterpret_cast<const char*>(in + p), n);
      p += n;
    } else if (c < 128) {
      if (out.size() - base + 1 > maxOut) return false;
      out.push_back(static_cast<char>(c));  // 0x00 and 0x09..0x7F are literal
    } else if (c >= 192) {
      if (out.size() - base + 2 > maxOut) return false;
      out.push_back(' ');
      out.push_back(static_cast<char>(c ^ 0x80));
    } else {
      if (p >= len) return true;
      const unsigned pair = (c << 8) | in[p++];
      const size_t dist = (pair >> 3) & 0x07FF;
      const size_t n = (pair & 7) + 3;
      const size_t have = out.size() - base;
      if (dist == 0 || dist > have) return true;  // bad back-reference: stop this record
      if (have + n > maxOut) return false;
      size_t from = out.size() - dist;
      for (size_t i = 0; i < n; ++i) out.push_back(out[from + i]);  // may overlap: byte-wise copy
    }
  }
  return true;
}

}  // namespace mobi
