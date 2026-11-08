#pragma once

#include <FsHelpers.h>

#include <cstddef>
#include <string>
#include <string_view>

// Last-resort cover search: a book that names no cover anywhere (no meta, no guide, no manifest
// property or id, no landmark) usually still ships a picture called cover.jpg. Fed with every path of the
// archive, the scanner keeps the best JPEG/PNG whose file name says cover. Names are compared without
// case and without the extension; a folder called "covers" or a name like "discover" does not count.
class CoverNameScan {
 public:
  void feed(const std::string_view path) {
    const int score = nameScore(path);
    if (score == 0) return;
    if (score > bestScore_ || (score == bestScore_ && better(path))) {
      bestScore_ = score;
      best_.assign(path);
    }
  }

  const std::string& result() const { return best_; }

  // 0 for a path that is not a cover picture; higher is better. Public for the tests.
  static int nameScore(const std::string_view path) {
    if (!FsHelpers::hasJpgExtension(path) && !FsHelpers::hasPngExtension(path)) return 0;
    const size_t slash = path.rfind('/');
    std::string stem(slash == std::string_view::npos ? path : path.substr(slash + 1));
    const size_t dot = stem.rfind('.');
    if (dot != std::string::npos) stem.resize(dot);
    for (auto& c : stem) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (stem == "cover") return 3;
    if (stem == "cover-image" || stem == "cover_image" || stem == "coverimage" || stem == "front-cover" ||
        stem == "front_cover" || stem == "frontcover" || stem == "book-cover" || stem == "book_cover" ||
        stem == "bookcover") {
      return 2;
    }
    return 0;
  }

 private:
  static size_t depth(const std::string_view p) {
    size_t d = 0;
    for (const char c : p) d += c == '/';
    return d;
  }
  // Between equal names the shallower path wins, then the shorter, then the earlier in the alphabet.
  bool better(const std::string_view path) const {
    const size_t a = depth(path);
    const size_t b = depth(best_);
    if (a != b) return a < b;
    if (path.size() != best_.size()) return path.size() < best_.size();
    return path < best_;
  }

  std::string best_;
  int bestScore_ = 0;
};
