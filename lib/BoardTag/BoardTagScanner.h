#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Platform-free scanner that finds the "INKMOD_BOARD=<variant>;" marker inside a firmware image.
//
// X4 Pro and X4 Classic both use the ESP32-S3, so the ESP image header (chip_id) cannot tell
// their firmwares apart. Every build therefore embeds a marker string (see BoardTag.h); the
// flasher streams the candidate image through this scanner during validation and refuses to
// touch flash if the marker names another board.
//
// The marker prefix is assembled at runtime from two halves so this code never contains the
// complete prefix as a literal (otherwise every firmware would "contain" a marker by itself).
namespace board_tag {

class Scanner {
 public:
  static constexpr size_t MAX_VARIANT_LEN = 24;

  // Feed the next chunk of image bytes (any chunk size; matches may span chunks).
  void feed(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len && !done_; ++i) step(data[i]);
  }

  bool found() const { return done_; }
  const char* variant() const { return variant_; }

 private:
  static constexpr char HEAD[] = "INKMOD_";
  static constexpr char TAIL[] = "BOARD=";
  static constexpr size_t HEAD_LEN = sizeof(HEAD) - 1;
  static constexpr size_t PREFIX_LEN = HEAD_LEN + sizeof(TAIL) - 1;

  static char prefixChar(size_t i) { return i < HEAD_LEN ? HEAD[i] : TAIL[i - HEAD_LEN]; }

  void step(uint8_t c) {
    if (matched_ < PREFIX_LEN) {
      if (c == static_cast<uint8_t>(prefixChar(matched_))) {
        ++matched_;
      } else {
        // The prefix starts with a character that does not recur inside it, so a plain restart
        // is a correct (KMP-equivalent) fallback.
        matched_ = (c == static_cast<uint8_t>(prefixChar(0))) ? 1 : 0;
      }
      return;
    }
    // Reading the variant name up to ';'.
    if (c == ';') {
      if (len_ > 0) {
        variant_[len_] = '\0';
        done_ = true;
      } else {
        matched_ = 0;
      }
      return;
    }
    const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    if (!ok || len_ >= MAX_VARIANT_LEN) {
      // Garbage after the prefix: abandon this candidate and resume searching.
      len_ = 0;
      matched_ = (c == static_cast<uint8_t>(prefixChar(0))) ? 1 : 0;
      return;
    }
    variant_[len_++] = static_cast<char>(c);
  }

  size_t matched_ = 0;
  size_t len_ = 0;
  bool done_ = false;
  char variant_[MAX_VARIANT_LEN + 1] = {};
};

enum class Verdict {
  MATCH,     // image is built for this board
  MISMATCH,  // image is explicitly built for another board
  UNTAGGED,  // no marker (stock / third-party / pre-guard build)
};

inline Verdict evaluate(const Scanner& scanner, const char* expectedVariant) {
  if (!scanner.found()) return Verdict::UNTAGGED;
  return std::strcmp(scanner.variant(), expectedVariant) == 0 ? Verdict::MATCH : Verdict::MISMATCH;
}

// Whether a flash may proceed. Untagged images are only acceptable where no sibling board shares
// the chip (ESP32-C3 X3/X4, where stock images are legitimately untagged).
inline bool allowed(Verdict v, bool allowUntagged) {
  return v == Verdict::MATCH || (v == Verdict::UNTAGGED && allowUntagged);
}

}  // namespace board_tag
