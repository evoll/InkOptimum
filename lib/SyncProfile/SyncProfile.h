#pragma once
// Named KOReader-sync server profiles (server URL, account, optional extra HTTP header).
// Pure C++: list handling and validation only; persistence lives in src/KOReaderProfileStore.
#include <cstddef>
#include <string>
#include <vector>

namespace sync_profile {

inline constexpr size_t MAX_PROFILES = 8;
inline constexpr size_t MAX_NAME = 24;

struct Profile {
  std::string name;
  std::string serverUrl;  // empty = default sync server
  std::string username;
  std::string password;
  std::string header;  // "Name: value" or empty
};

inline std::string trimmed(const std::string& s) {
  size_t a = 0, b = s.size();
  while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
  while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
  return s.substr(a, b - a);
}

// 1..MAX_NAME bytes, no control characters. Returns the trimmed name or "" when invalid.
inline std::string sanitizeName(const std::string& raw) {
  const std::string n = trimmed(raw);
  if (n.empty() || n.size() > MAX_NAME) return "";
  for (const char c : n) {
    if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) return "";
  }
  return n;
}

// Only http(s) URLs (or empty for the default server); trailing slashes are dropped.
inline bool normalizeServerUrl(const std::string& raw, std::string& out) {
  std::string u = trimmed(raw);
  while (!u.empty() && u.back() == '/') u.pop_back();
  if (u.empty()) {
    out.clear();
    return true;
  }
  if (u.size() > 200) return false;
  if (u.rfind("http://", 0) != 0 && u.rfind("https://", 0) != 0) return false;
  for (const char c : u) {
    if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F) return false;
  }
  out = u;
  return true;
}

inline int indexOf(const std::vector<Profile>& list, const std::string& name) {
  for (size_t i = 0; i < list.size(); ++i) {
    if (list[i].name == name) return static_cast<int>(i);
  }
  return -1;
}

// Inserts or replaces by name. False when the list is full and the name is new.
inline bool upsert(std::vector<Profile>& list, const Profile& p) {
  const int i = indexOf(list, p.name);
  if (i >= 0) {
    list[i] = p;
    return true;
  }
  if (list.size() >= MAX_PROFILES) return false;
  list.push_back(p);
  return true;
}

inline bool remove(std::vector<Profile>& list, const std::string& name) {
  const int i = indexOf(list, name);
  if (i < 0) return false;
  list.erase(list.begin() + i);
  return true;
}

// Next profile after `current` (wraps); -1 for an empty list.
inline int nextIndex(const std::vector<Profile>& list, const std::string& current) {
  if (list.empty()) return -1;
  const int i = indexOf(list, current);
  return (i + 1) % static_cast<int>(list.size());
}

}  // namespace sync_profile
