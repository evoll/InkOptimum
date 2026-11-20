#pragma once
// Minimal JSON document model for the XtApp host: manifest parsing without a Lua state, plus the value tree the
// Lua bridge converts to and from. Integers are kept apart from doubles so a state table round-trips exactly.

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace xtapp {

struct JValue {
  enum class Type { Null, Bool, Int, Num, Str, Arr, Obj };
  Type type = Type::Null;
  bool b = false;
  int64_t i = 0;
  double n = 0;
  std::string s;
  std::vector<JValue> a;
  std::vector<std::pair<std::string, JValue>> o;

  const JValue* get(const char* key) const;  // object member or nullptr
  std::string str(const char* key, const std::string& fallback = "") const;
};

// Parses RFC 8259 JSON (depth <= 32, \uXXXX incl. surrogate pairs -> UTF-8). On failure returns false and fills err.
bool parseJson(const std::string& text, JValue& out, std::string* err = nullptr);
void appendJsonString(std::string& out, const std::string& s);

}  // namespace xtapp
