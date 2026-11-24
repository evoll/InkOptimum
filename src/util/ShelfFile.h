#pragma once
// Loads/saves the user shelves (lib/Shelves) at /.crosspoint/shelves.txt. Nothing stays resident: callers load on
// entry and drop the store on exit.

#include <Shelves.h>

namespace ShelfFile {
constexpr const char* PATH = "/.crosspoint/shelves.txt";
void load(shelves::ShelfStore& store);
bool save(const shelves::ShelfStore& store);
// For code that renames, moves or deletes books/folders (a folder path ends with '/').
void onPathRenamed(const char* from, const char* to);
void onPathDeleted(const char* path);
}  // namespace ShelfFile
