#include "Shelves.h"

#include <algorithm>

namespace shelves {

static std::string lower(std::string s) {
  for (auto& c : s) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return s;
}

std::string ShelfStore::cleanName(const std::string& in) {
  std::string out;
  for (const unsigned char c : in) {
    if (c < 0x20 || c == 0x7F) continue;  // tabs/newlines would break the file format
    out += static_cast<char>(c);
  }
  size_t b = 0;
  while (b < out.size() && out[b] == ' ') b++;
  size_t e = out.size();
  while (e > b && out[e - 1] == ' ') e--;
  out = out.substr(b, e - b);
  if (out.size() > kMaxNameBytes) {
    size_t cut = kMaxNameBytes;
    while (cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) cut--;  // do not split a UTF-8 sequence
    out.resize(cut);
  }
  return out;
}

bool ShelfStore::nameTaken(const std::string& name, const size_t ignore) const {
  const std::string key = lower(name);
  for (size_t i = 0; i < shelves_.size(); i++) {
    if (i != ignore && lower(shelves_[i].name) == key) return true;
  }
  return false;
}

int ShelfStore::create(const std::string& name) {
  const std::string n = cleanName(name);
  if (n.empty() || shelves_.size() >= kMaxShelves || nameTaken(n, static_cast<size_t>(-1))) return -1;
  shelves_.push_back({n, {}});
  return static_cast<int>(shelves_.size()) - 1;
}

bool ShelfStore::rename(const size_t index, const std::string& name) {
  const std::string n = cleanName(name);
  if (index >= shelves_.size() || n.empty() || nameTaken(n, index)) return false;
  shelves_[index].name = n;
  return true;
}

bool ShelfStore::removeShelf(const size_t index) {
  if (index >= shelves_.size()) return false;
  shelves_.erase(shelves_.begin() + static_cast<long>(index));
  return true;
}

bool ShelfStore::contains(const size_t index, const std::string& path) const {
  if (index >= shelves_.size()) return false;
  const auto& b = shelves_[index].books;
  return std::find(b.begin(), b.end(), path) != b.end();
}

bool ShelfStore::addBook(const size_t index, const std::string& path) {
  if (index >= shelves_.size() || path.empty() || contains(index, path)) return false;
  for (const unsigned char c : path) {
    if (c < 0x20) return false;
  }
  if (shelves_[index].books.size() >= kMaxBooksPerShelf) return false;
  shelves_[index].books.push_back(path);
  return true;
}

bool ShelfStore::removeBook(const size_t index, const std::string& path) {
  if (index >= shelves_.size()) return false;
  auto& b = shelves_[index].books;
  const auto it = std::find(b.begin(), b.end(), path);
  if (it == b.end()) return false;
  b.erase(it);
  return true;
}

void ShelfStore::renamePath(const std::string& from, const std::string& to) {
  if (from.empty()) return;
  const bool folder = from.back() == '/';
  for (auto& s : shelves_) {
    for (auto& p : s.books) {
      if (folder ? p.compare(0, from.size(), from) == 0 : p == from) p = folder ? to + p.substr(from.size()) : to;
    }
  }
}

void ShelfStore::forgetPath(const std::string& path) {
  if (path.empty()) return;
  const bool folder = path.back() == '/';
  for (auto& s : shelves_) {
    s.books.erase(std::remove_if(s.books.begin(), s.books.end(),
                                 [&](const std::string& p) {
                                   return folder ? p.compare(0, path.size(), path) == 0 : p == path;
                                 }),
                  s.books.end());
  }
}

std::string ShelfStore::serialize() const {
  std::string o;
  for (const auto& s : shelves_) {
    o += "S\t" + s.name + "\n";
    for (const auto& b : s.books) o += "B\t" + b + "\n";
  }
  return o;
}

void ShelfStore::load(const std::string& text) {
  shelves_.clear();
  size_t pos = 0;
  while (pos < text.size()) {
    size_t e = text.find('\n', pos);
    if (e == std::string::npos) e = text.size();
    std::string line = text.substr(pos, e - pos);
    pos = e + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() < 3 || line[1] != '\t') continue;
    const std::string value = line.substr(2);
    if (line[0] == 'S') {
      create(value);
    } else if (line[0] == 'B' && !shelves_.empty()) {
      addBook(shelves_.size() - 1, value);
    }
  }
}

}  // namespace shelves
