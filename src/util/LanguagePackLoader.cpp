#include "util/LanguagePackLoader.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <cstring>
#include <memory>

namespace {
constexpr char kDir[] = "/.inkmod/languages";
constexpr size_t kMaxPacks = 8;

bool validCode(const char* code) {
  if (!code || !*code || strlen(code) > 11) return false;
  for (const char* p = code; *p; ++p) {
    const char c = *p;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
  }
  return true;
}
}  // namespace

namespace LanguagePackLoader {

std::vector<PackInfo> scan() {
  std::vector<PackInfo> packs;
  if (!Storage.ready()) return packs;
  HalFile dir = Storage.open(kDir);
  if (!dir || !dir.isDirectory()) return packs;
  for (HalFile entry = dir.openNextFile(); entry && packs.size() < kMaxPacks; entry = dir.openNextFile()) {
    if (entry.isDirectory()) continue;
    char fileName[48];
    if (entry.getName(fileName, sizeof(fileName)) == 0) continue;
    const size_t len = strlen(fileName);
    if (len < 6 || strcmp(fileName + len - 5, ".lang") != 0) continue;
    const std::string code(fileName, len - 5);
    if (!validCode(code.c_str())) continue;
    // Header only: magic, version, count, hash, then the display name.
    uint8_t head[I18nPack::kHeaderSize + I18nPack::kMaxNameLength];
    const int got = entry.read(head, sizeof(head));
    if (got < static_cast<int>(I18nPack::kHeaderSize + 1)) continue;
    if (memcmp(head, "ILNG", 4) != 0 || head[4] != 1) continue;
    if (I18nPack::readU16(head + 6) != static_cast<uint16_t>(StrId::_COUNT) ||
        I18nPack::readU32(head + 8) != I18N_KEY_HASH) {
      continue;  // built for another firmware version
    }
    const size_t nameLen = I18nPack::readU16(head + 12);
    if (nameLen == 0 || nameLen > I18nPack::kMaxNameLength || I18nPack::kHeaderSize + nameLen > static_cast<size_t>(got))
      continue;
    packs.push_back({code, std::string(reinterpret_cast<const char*>(head + I18nPack::kHeaderSize), nameLen)});
  }
  return packs;
}

bool load(const char* code) {
  if (!validCode(code) || !Storage.ready()) return false;
  const std::string path = std::string(kDir) + "/" + code + ".lang";
  FsFile file;
  if (!Storage.openFileForRead("LANG", path, file)) return false;
  const size_t size = file.size();
  if (size < I18nPack::kHeaderSize + 2 || size > I18nPack::kMaxFileSize) {
    file.close();
    return false;
  }
  std::unique_ptr<uint8_t[]> buf(new (std::nothrow) uint8_t[size]);
  if (!buf) {
    file.close();
    LOG_ERR("LANG", "No memory for language pack %s (%u bytes)", code, static_cast<unsigned>(size));
    return false;
  }
  const bool readOk = file.read(buf.get(), size) == static_cast<int>(size);
  file.close();
  return readOk && I18N.setExternalPack(std::move(buf), size);
}

}  // namespace LanguagePackLoader
