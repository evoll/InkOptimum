#pragma once
// User shelves: named lists of book paths kept in /.crosspoint/shelves.txt. Pure logic (no storage access) so it is
// testable natively; the activities load the file on entry and drop the object on exit.
// File format, one record per line: "S\t<shelf name>" starts a shelf, "B\t<book path>" adds a book to it.

#include <cstddef>
#include <string>
#include <vector>

namespace shelves {

struct Shelf {
  std::string name;
  std::vector<std::string> books;
};

class ShelfStore {
 public:
  static constexpr size_t kMaxShelves = 16;
  static constexpr size_t kMaxBooksPerShelf = 200;
  static constexpr size_t kMaxNameBytes = 40;

  const std::vector<Shelf>& shelves() const { return shelves_; }

  // Returns the new shelf index, or -1 (empty name after cleaning, duplicate name ignoring ASCII case, limit hit).
  int create(const std::string& name);
  bool rename(size_t index, const std::string& name);  // false on empty/duplicate/out of range
  bool removeShelf(size_t index);
  bool addBook(size_t index, const std::string& path);  // false when already there, full, or bad index
  bool removeBook(size_t index, const std::string& path);
  bool contains(size_t index, const std::string& path) const;
  // A book was renamed/moved (a folder rename passes the folder prefix with a trailing '/'), or deleted.
  void renamePath(const std::string& from, const std::string& to);
  void forgetPath(const std::string& path);

  std::string serialize() const;
  void load(const std::string& text);

  static std::string cleanName(const std::string& in);

 private:
  bool nameTaken(const std::string& name, size_t ignore) const;
  std::vector<Shelf> shelves_;
};

}  // namespace shelves
