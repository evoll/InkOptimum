// DocxXml.h - small WordprocessingML helpers shared by the package layer and the
// parser. Element/attribute names are matched by LOCAL name so the namespace
// prefix a producer chose ("w:", none, ...) does not matter.
#pragma once

#include <cctype>
#include <cstdlib>
#include <string>
#include <string_view>

#include <OpcNames.h>
#include <XmlPullReader.h>

namespace docx {

// Attribute by local name ("val" finds w:val, val, or any other prefix).
inline const char* attrLocal(const XmlPullReader& xml, std::string_view local) {
  for (const XmlAttr& a : xml.attrs()) {
    const size_t colon = a.name.find_last_of(':');
    const std::string_view n = colon == std::string::npos ? std::string_view(a.name)
                                                           : std::string_view(a.name).substr(colon + 1);
    if (n == local) return a.value.c_str();
  }
  return nullptr;
}

// Word's on/off properties: <w:b/> is on, w:val="0|false|off|none" turns it off.
inline bool toggleOn(const XmlPullReader& xml) {
  const char* v = attrLocal(xml, "val");
  if (!v) return true;
  const std::string s = opc::asciiLowerCopy(v);
  return !(s == "0" || s == "false" || s == "off" || s == "none");
}

inline uint32_t attrUint(const XmlPullReader& xml, std::string_view local, uint32_t fallback = 0) {
  const char* v = attrLocal(xml, local);
  if (!v || !*v) return fallback;
  char* end = nullptr;
  const unsigned long n = std::strtoul(v, &end, 10);
  return end == v ? fallback : static_cast<uint32_t>(n);
}

inline bool isStart(XmlToken t) { return t == XmlToken::StartTag || t == XmlToken::SelfClosing; }

}  // namespace docx
