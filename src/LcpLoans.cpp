#include "LcpLoans.h"

#include <ArduinoJson.h>
#include <HalStorage.h>

#include <algorithm>

namespace lcploans {

namespace {
constexpr char FILE_PATH[] = "/.inkmod/lcp_loans.json";

std::vector<std::string> load() {
  std::vector<std::string> out;
  const String json = Storage.readFile(FILE_PATH);
  if (json.isEmpty()) return out;
  JsonDocument doc;
  if (deserializeJson(doc, json.c_str()) != DeserializationError::Ok) return out;
  for (JsonVariantConst v : doc["books"].as<JsonArrayConst>()) {
    const char* p = v.as<const char*>();
    if (p && *p && out.size() < MAX_LOANS) out.emplace_back(p);
  }
  return out;
}

bool save(const std::vector<std::string>& books) {
  JsonDocument doc;
  JsonArray arr = doc["books"].to<JsonArray>();
  for (const auto& b : books) arr.add(b);
  String out;
  serializeJson(doc, out);
  Storage.mkdir("/.inkmod");
  return Storage.writeFile(FILE_PATH, out);
}
}  // namespace

bool add(const std::string& bookPath) {
  auto books = load();
  if (std::find(books.begin(), books.end(), bookPath) != books.end()) return true;
  if (books.size() >= MAX_LOANS) books.erase(books.begin());
  books.push_back(bookPath);
  return save(books);
}

std::vector<std::string> list() {
  auto books = load();
  const size_t before = books.size();
  books.erase(std::remove_if(books.begin(), books.end(), [](const std::string& p) { return !Storage.exists(p.c_str()); }),
              books.end());
  if (books.size() != before) save(books);
  return books;
}

bool remove(const std::string& bookPath) {
  auto books = load();
  const auto it = std::find(books.begin(), books.end(), bookPath);
  if (it == books.end()) return true;
  books.erase(it);
  return save(books);
}

}  // namespace lcploans
