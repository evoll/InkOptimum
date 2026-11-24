#include "CompanionAuth.h"

#include <cstdio>

#include <XtAppUtil.h>

namespace companion {

std::string toHex(const std::string& raw) {
  static const char* d = "0123456789abcdef";
  std::string o;
  o.reserve(raw.size() * 2);
  for (const unsigned char c : raw) {
    o += d[c >> 4];
    o += d[c & 15];
  }
  return o;
}

static int nib(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool fromHex(const std::string& hex, std::string& raw) {
  raw.clear();
  if (hex.size() % 2) return false;
  for (size_t i = 0; i < hex.size(); i += 2) {
    const int a = nib(hex[i]), b = nib(hex[i + 1]);
    if (a < 0 || b < 0) return false;
    raw += static_cast<char>(a * 16 + b);
  }
  return true;
}

// Wrap-safe "now >= t".
static bool reached(uint32_t now, uint32_t t) { return static_cast<int32_t>(now - t) >= 0; }

bool Pairing::locked(uint32_t nowMs) const { return hasLock_ && !reached(nowMs, lockedUntil_); }

bool Pairing::active(uint32_t nowMs) const { return hasSession_ && !reached(nowMs, expiresAt_); }

bool Pairing::begin(uint32_t entropy, uint32_t nowMs) {
  if (locked(nowMs)) return false;
  hasLock_ = false;
  char buf[8];
  snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(entropy % 1000000u));
  code_ = buf;
  expiresAt_ = nowMs + kCodeLifetimeMs;
  hasSession_ = true;
  wrong_ = 0;
  return true;
}

void Pairing::cancel() {
  hasSession_ = false;
  code_.clear();
}

Pairing::Result Pairing::confirm(const std::string& code, uint32_t nowMs) {
  if (locked(nowMs)) return Result::Locked;
  if (!active(nowMs)) {
    cancel();
    return Result::NoSession;
  }
  // Compare all bytes regardless of where they differ.
  unsigned diff = code.size() ^ code_.size();
  for (size_t i = 0; i < code.size() && i < code_.size(); i++) diff |= static_cast<unsigned char>(code[i] ^ code_[i]);
  if (diff == 0) {
    cancel();
    wrong_ = 0;
    return Result::Ok;
  }
  if (++wrong_ >= kMaxWrong) {
    cancel();
    wrong_ = 0;
    hasLock_ = true;
    lockedUntil_ = nowMs + kLockoutMs;
    return Result::Locked;
  }
  return Result::Wrong;
}

static std::string cleanName(const std::string& in) {
  std::string o;
  for (const unsigned char c : in) {
    if (c < 0x20 || c == 0x7F) continue;
    if (o.size() >= 40) break;
    o += static_cast<char>(c);
  }
  return o;
}

std::string TokenStore::issue(const std::string& token32, const std::string& name) {
  if (token32.size() != 32) return "";
  TokenInfo t;
  t.hash = xtapp::sha256(token32);
  t.id = toHex(t.hash).substr(0, 8);
  t.name = cleanName(name);
  if (t.name.empty()) t.name = "device";
  tokens_.push_back(t);
  if (tokens_.size() > kMaxTokens) tokens_.erase(tokens_.begin());
  return toHex(token32);
}

bool TokenStore::verify(const std::string& bearerHex) const {
  std::string raw;
  if (bearerHex.size() != 64 || !fromHex(bearerHex, raw)) return false;
  const std::string h = xtapp::sha256(raw);
  bool found = false;
  for (const auto& t : tokens_) {
    unsigned diff = 0;
    for (size_t i = 0; i < 32; i++) diff |= static_cast<unsigned char>(h[i] ^ t.hash[i]);
    found |= diff == 0;
  }
  return found;
}

bool TokenStore::revoke(const std::string& id) {
  for (size_t i = 0; i < tokens_.size(); i++) {
    if (tokens_[i].id == id) {
      tokens_.erase(tokens_.begin() + static_cast<long>(i));
      return true;
    }
  }
  return false;
}

std::string TokenStore::serialize() const {
  std::string o;
  for (const auto& t : tokens_) o += t.id + "\t" + t.name + "\t" + toHex(t.hash) + "\n";
  return o;
}

size_t TokenStore::load(const std::string& text) {
  tokens_.clear();
  size_t pos = 0;
  while (pos < text.size() && tokens_.size() < kMaxTokens) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    const std::string line = text.substr(pos, e - pos);
    pos = e + 1;
    const size_t a = line.find('\t');
    const size_t b = a == std::string::npos ? a : line.find('\t', a + 1);
    if (b == std::string::npos) continue;
    TokenInfo t;
    t.id = line.substr(0, a);
    t.name = cleanName(line.substr(a + 1, b - a - 1));
    if (!fromHex(line.substr(b + 1), t.hash) || t.hash.size() != 32) continue;
    if (toHex(t.hash).substr(0, 8) != t.id) continue;
    tokens_.push_back(t);
  }
  return tokens_.size();
}

std::string bearerFrom(const std::string& v) {
  static const std::string p = "Bearer ";
  if (v.size() <= p.size() || v.compare(0, p.size(), p) != 0) return "";
  return v.substr(p.size());
}

}  // namespace companion
