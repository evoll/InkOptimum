#pragma once

#include <string>
#include <vector>

// Index of LCP books this reader holds a key for (/.inkmod/lcp_loans.json), so the loans screen does not
// have to scan the card. Entries are added when a key is stored and dropped when the book or key is gone.
namespace lcploans {

constexpr size_t MAX_LOANS = 64;

// Adds the book path (no duplicates, oldest dropped when full).
bool add(const std::string& bookPath);
// Existing books only; entries whose book file is gone are pruned from the index.
std::vector<std::string> list();
bool remove(const std::string& bookPath);

}  // namespace lcploans
