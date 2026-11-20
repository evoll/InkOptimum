#pragma once

#include <string>

// Custom book covers picked from the SD card (idea: Paperio-Reader BookCoverUtils, own implementation).
//
// The picked JPG/PNG is copied next to the book as <book>.cover.jpg / .cover.png (for "book.fb2.zip" the
// stem is "book"), so it never overwrites an unrelated sidecar like <book>.jpg. Epub::generateCoverBmp and
// the thumbnail generators prefer this file over the cover embedded in the book. The embedded cover is
// never touched, so resetting simply deletes the copy.
namespace BookCover {

// Path without the book extension ("/b/x.epub" -> "/b/x", "/b/x.fb2.zip" -> "/b/x").
std::string basePath(const std::string& bookPath);

// Empty when the book has no custom cover.
std::string findCustom(const std::string& bookPath);
inline bool hasCustom(const std::string& bookPath) { return !findCustom(bookPath).empty(); }

// True for the picture formats install() accepts (JPG/JPEG/PNG).
bool isSupportedImage(const std::string& imagePath);

// Copies imagePath next to the book (staged as .part and size-verified before replacing the old one).
// Callers must clear the book's derived cover files afterwards (Epub::invalidateCoverCache()).
bool install(const std::string& bookPath, const std::string& imagePath);

// Removes the custom cover (all variants and leftovers).
bool reset(const std::string& bookPath);

}  // namespace BookCover
