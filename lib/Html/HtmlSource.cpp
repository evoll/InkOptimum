#include "HtmlSource.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>

#include <BufferedFileWriter.h>
#include <Codepage.h>

#include <algorithm>
#include <vector>

#include "HtmlFormat.h"

namespace html {

namespace {

// Charset named in the first bytes: <meta charset=...>, <meta ... content="...;
// charset=...">, or an XML prolog encoding="...". Empty when none.
std::string declaredCharset(const char* head, size_t len) {
  std::string lower(head, len);
  for (char& c : lower) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  for (const char* key : {"charset=", "encoding="}) {
    const size_t at = lower.find(key);
    if (at == std::string::npos) continue;
    size_t i = at + strlen(key);
    while (i < lower.size() && (lower[i] == '"' || lower[i] == '\'' || lower[i] == ' ')) ++i;
    size_t j = i;
    while (j < lower.size() && lower[j] != '"' && lower[j] != '\'' && lower[j] != ';' && lower[j] != ' ' &&
           lower[j] != '>' && lower[j] != '/' && lower[j] != '?') {
      ++j;
    }
    if (j > i) return lower.substr(i, j - i);
  }
  return std::string();
}

std::string joinPath(const std::string& dir, const std::string& rel) {
  std::vector<std::string> parts;
  auto addAll = [&](const std::string& path) {
    size_t i = 0;
    while (i <= path.size()) {
      size_t j = path.find('/', i);
      if (j == std::string::npos) j = path.size();
      const std::string seg = path.substr(i, j - i);
      if (seg == "..") {
        if (!parts.empty()) parts.pop_back();
      } else if (!seg.empty() && seg != ".") {
        parts.push_back(seg);
      }
      i = j + 1;
    }
  };
  addAll(dir);
  addAll(rel);
  std::string out;
  for (const auto& p : parts) out += "/" + p;
  return out;
}

bool streamFile(const std::string& path, const FictionBookParser::BinaryOutputFn& out) {
  HalFile file;
  if (!Storage.openFileForRead("HTML", path, file)) return false;
  uint8_t buf[512];
  bool any = false;
  for (;;) {
    const int n = file.read(buf, sizeof(buf));
    if (n <= 0) break;
    out(buf, static_cast<size_t>(n));
    any = true;
  }
  file.close();
  return any;
}

}  // namespace

bool isBodySource(const std::string& sourcePath) { return hasHtmlExtension(sourcePath) || isConvertedBody(sourcePath); }

bool prepareSource(const std::string& bookPath, const std::string& cachePath,
                   const std::function<void(int percent)>& onProgress, PreparedSource& out) {
  out = PreparedSource();
  if (!Storage.exists(bookPath.c_str())) return false;

  char head[2048];
  const size_t headLen = Storage.readFileToBuffer(bookPath.c_str(), head, sizeof(head));
  const auto* h = reinterpret_cast<const uint8_t*>(head);
  const bool utf16le = headLen >= 2 && h[0] == 0xFF && h[1] == 0xFE;
  const bool utf16be = headLen >= 2 && h[0] == 0xFE && h[1] == 0xFF;
  int codepage = 0;
  if (!utf16le && !utf16be && !(headLen >= 3 && h[0] == 0xEF && h[1] == 0xBB && h[2] == 0xBF)) {
    codepage = fictionbook::codepageFromName(declaredCharset(head, headLen));
  }
  if (!utf16le && !utf16be && codepage == 0) {
    out.bodyPath = bookPath;  // UTF-8 / unknown: read in place
    if (onProgress) onProgress(30);
    return true;
  }

  HalFile in;
  if (!Storage.openFileForRead("HTML", bookPath, in)) return false;
  const std::string bodyPath = cachePath + kConvertedBodyName;
  HalFile body;
  if (!Storage.openFileForWrite("HTML", bodyPath, body)) {
    in.close();
    return false;
  }
  const size_t total = in.fileSize();
  size_t done = 0;
  bool ok = true;
  {
    BufferedFileWriter writer(body);
    uint8_t buf[1024];
    std::string conv;
    int pendingByte = -1;
    uint32_t highSurrogate = 0;
    bool first = true;
    int chunks = 0;
    for (;;) {
      const int got = in.read(buf, sizeof(buf));
      if (got <= 0) break;
      done += static_cast<size_t>(got);
      conv.clear();
      if (codepage != 0) {
        for (int i = 0; i < got; ++i) {
          if (buf[i] < 0x80) {
            conv.push_back(static_cast<char>(buf[i]));
          } else {
            fictionbook::appendUtf8CodePoint(conv, fictionbook::codepageToUnicode(codepage, buf[i]));
          }
        }
      } else {
        int i = 0;
        if (first && got >= 2) i = 2;  // BOM
        for (; i < got; ++i) {
          if (pendingByte < 0) {
            pendingByte = buf[i];
            continue;
          }
          uint32_t unit = utf16le ? (static_cast<uint32_t>(buf[i]) << 8) | static_cast<uint32_t>(pendingByte)
                                  : (static_cast<uint32_t>(pendingByte) << 8) | buf[i];
          pendingByte = -1;
          if (unit >= 0xD800 && unit < 0xDC00) {
            highSurrogate = unit;
            continue;
          }
          if (unit >= 0xDC00 && unit < 0xE000) {
            unit = highSurrogate ? 0x10000 + ((highSurrogate - 0xD800) << 10) + (unit - 0xDC00) : 0xFFFD;
          }
          highSurrogate = 0;
          fictionbook::appendUtf8CodePoint(conv, unit);
        }
      }
      first = false;
      if (writer.write(reinterpret_cast<const uint8_t*>(conv.data()), conv.size()) != conv.size()) {
        ok = false;
        break;
      }
      if (++chunks % 32 == 0) {
        vTaskDelay(1);
        if (onProgress && total > 0) onProgress(10 + static_cast<int>(done * 20 / total));
      }
    }
    ok = writer.finish() && ok;
  }
  body.close();
  in.close();
  if (!ok) {
    Storage.remove(bodyPath.c_str());
    LOG_ERR("HTML", "Transcode failed: %s", bookPath.c_str());
    return false;
  }
  out.bodyPath = bodyPath;
  return true;
}

std::unique_ptr<HtmlParser> makeRenderParser(const std::string& originalBookPath) {
  std::unique_ptr<HtmlParser> parser(new HtmlParser());
  const size_t slash = originalBookPath.find_last_of('/');
  const std::string dir = slash == std::string::npos ? std::string() : originalBookPath.substr(0, slash);
  parser->setExternalImageLoader([dir](const std::string& src, const FictionBookParser::BinaryOutputFn& out) {
    return streamFile(joinPath(dir, src), out);
  });
  return parser;
}

}  // namespace html
