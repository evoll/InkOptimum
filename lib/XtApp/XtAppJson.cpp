#include "XtAppJson.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace xtapp {

const JValue* JValue::get(const char* key) const {
  if (type != Type::Obj) return nullptr;
  for (const auto& kv : o) {
    if (kv.first == key) return &kv.second;
  }
  return nullptr;
}

std::string JValue::str(const char* key, const std::string& fallback) const {
  const JValue* v = get(key);
  return (v && v->type == Type::Str) ? v->s : fallback;
}

namespace {

struct Parser {
  const char* p;
  const char* end;
  std::string err;

  bool fail(const char* m) {
    if (err.empty()) err = m;
    return false;
  }
  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
  }
  static void putUtf8(std::string& s, uint32_t cp) {
    if (cp < 0x80) {
      s += static_cast<char>(cp);
    } else if (cp < 0x800) {
      s += static_cast<char>(0xC0 | (cp >> 6));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      s += static_cast<char>(0xE0 | (cp >> 12));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      s += static_cast<char>(0xF0 | (cp >> 18));
      s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
      s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
      s += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }
  bool hex4(uint32_t& v) {
    if (end - p < 4) return fail("bad \\u escape");
    v = 0;
    for (int k = 0; k < 4; ++k) {
      const char c = *p++;
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return fail("bad \\u escape");
    }
    return true;
  }
  bool string(std::string& out) {
    if (p >= end || *p != '"') return fail("string expected");
    ++p;
    while (p < end) {
      const char c = *p++;
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (p >= end) break;
      const char e = *p++;
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!hex4(cp)) return false;
          if (cp >= 0xD800 && cp < 0xDC00 && end - p >= 6 && p[0] == '\\' && p[1] == 'u') {
            p += 2;
            uint32_t lo;
            if (!hex4(lo)) return false;
            if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          putUtf8(out, cp);
          break;
        }
        default: return fail("bad escape");
      }
    }
    return fail("unterminated string");
  }
  bool value(JValue& v, int depth) {
    if (depth > 32) return fail("nesting too deep");
    ws();
    if (p >= end) return fail("unexpected end");
    const char c = *p;
    if (c == '{') {
      ++p;
      v.type = JValue::Type::Obj;
      ws();
      if (p < end && *p == '}') {
        ++p;
        return true;
      }
      for (;;) {
        ws();
        std::string key;
        if (!string(key)) return false;
        ws();
        if (p >= end || *p != ':') return fail("':' expected");
        ++p;
        JValue child;
        if (!value(child, depth + 1)) return false;
        v.o.emplace_back(std::move(key), std::move(child));
        ws();
        if (p < end && *p == ',') {
          ++p;
          continue;
        }
        if (p < end && *p == '}') {
          ++p;
          return true;
        }
        return fail("',' or '}' expected");
      }
    }
    if (c == '[') {
      ++p;
      v.type = JValue::Type::Arr;
      ws();
      if (p < end && *p == ']') {
        ++p;
        return true;
      }
      for (;;) {
        JValue child;
        if (!value(child, depth + 1)) return false;
        v.a.push_back(std::move(child));
        ws();
        if (p < end && *p == ',') {
          ++p;
          continue;
        }
        if (p < end && *p == ']') {
          ++p;
          return true;
        }
        return fail("',' or ']' expected");
      }
    }
    if (c == '"') {
      v.type = JValue::Type::Str;
      return string(v.s);
    }
    if (end - p >= 4 && !strncmp(p, "true", 4)) {
      p += 4;
      v.type = JValue::Type::Bool;
      v.b = true;
      return true;
    }
    if (end - p >= 5 && !strncmp(p, "false", 5)) {
      p += 5;
      v.type = JValue::Type::Bool;
      return true;
    }
    if (end - p >= 4 && !strncmp(p, "null", 4)) {
      p += 4;
      v.type = JValue::Type::Null;
      return true;
    }
    if (c == '-' || (c >= '0' && c <= '9')) {
      const char* s = p;
      bool isFloat = false;
      if (*p == '-') ++p;
      while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '+' || *p == '-')) {
        if (*p == '.' || *p == 'e' || *p == 'E') isFloat = true;
        ++p;
      }
      const std::string tok(s, p);
      if (tok == "-" || tok.empty()) return fail("bad number");
      char* e = nullptr;
      if (!isFloat) {
        errno = 0;
        const long long ll = strtoll(tok.c_str(), &e, 10);
        if (e && *e == '\0' && errno == 0) {
          v.type = JValue::Type::Int;
          v.i = ll;
          return true;
        }
      }
      const double d = strtod(tok.c_str(), &e);
      if (!e || *e != '\0') return fail("bad number");
      v.type = JValue::Type::Num;
      v.n = d;
      return true;
    }
    return fail("unexpected character");
  }
};

}  // namespace

bool parseJson(const std::string& text, JValue& out, std::string* err) {
  Parser ps{text.data(), text.data() + text.size(), {}};
  out = JValue{};
  if (!ps.value(out, 0)) {
    if (err) *err = ps.err;
    return false;
  }
  ps.ws();
  if (ps.p != ps.end) {
    if (err) *err = "trailing characters";
    return false;
  }
  return true;
}

void appendJsonString(std::string& out, const std::string& s) {
  out += '"';
  for (const unsigned char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += static_cast<char>(c);
        }
    }
  }
  out += '"';
}

}  // namespace xtapp
