#pragma once
// What a downloaded file really is, judged from its first bytes. Servers advertise the wrong type often
// enough (FB2 sent as ZIP, an HTML or JSON error page saved with status 200), so the OPDS browser checks.
#include <cstdint>
#include <cstring>

enum class DownloadedBookKind : uint8_t { UNKNOWN, ZIP, FB2_XML, MOBI, RTF };

inline DownloadedBookKind sniffBookBytes(const uint8_t* buf, const int count) {
  if (!buf || count < 4) return DownloadedBookKind::UNKNOWN;

  // PalmDB header: type/creator "BOOKMOBI" (MOBI/AZW/AZW3) at offset 60.
  if (count >= 68 && memcmp(buf + 60, "BOOKMOBI", 8) == 0) return DownloadedBookKind::MOBI;
  if (count >= 5 && memcmp(buf, "{\\rtf", 5) == 0) return DownloadedBookKind::RTF;

  // ZIP local header / empty archive / spanned archive signatures.
  if (buf[0] == 'P' && buf[1] == 'K' &&
      ((buf[2] == 0x03 && buf[3] == 0x04) || (buf[2] == 0x05 && buf[3] == 0x06) ||
       (buf[2] == 0x07 && buf[3] == 0x08))) {
    return DownloadedBookKind::ZIP;
  }

  // Raw FB2 may start with BOM/whitespace/XML declaration. Search the small prefix for the FictionBook root
  // instead of requiring it at byte zero.
  char ascii[513] = {};
  const int n = count < 512 ? count : 512;
  for (int i = 0; i < n; ++i) {
    unsigned char c = buf[i];
    if (c >= 'A' && c <= 'Z') c = static_cast<unsigned char>(c - 'A' + 'a');
    ascii[i] = static_cast<char>(c);
  }
  if (strstr(ascii, "<fictionbook") != nullptr || strstr(ascii, ":fictionbook") != nullptr) {
    return DownloadedBookKind::FB2_XML;
  }
  return DownloadedBookKind::UNKNOWN;
}
