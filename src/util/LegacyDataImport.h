#pragma once

// "Import from another firmware": copies what CrossPoint, CrossInk, Paperio-Reader and witchhunt-reader keep in
// /.crosspoint (saved Wi-Fi networks, KOReader/OPDS/LCP accounts, recent books, all-time statistics, bookmarks,
// clippings, and every book's reading position and statistics) into /.inkmod. Existing files are never overwritten,
// so running it again, or on a device that is already in use, is safe. Rules live in lib/LegacyImport.
namespace LegacyDataImport {

struct Result {
  bool sourceFound = false;  // /.crosspoint exists
  int copied = 0;            // files written
  int books = 0;             // books that got at least one file
  int failed = 0;            // files that could not be copied
};

Result run();

}  // namespace LegacyDataImport
