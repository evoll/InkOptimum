#include "XtAppLocator.h"

#include <HalStorage.h>
#include <XtApp.h>

namespace XtAppLocator {
namespace {
constexpr size_t kMaxManifest = 8 * 1024;
}

std::string findDir(const std::string& idOrFolder) {
  if (idOrFolder.empty() || idOrFolder.find('/') != std::string::npos || idOrFolder.find("..") != std::string::npos) {
    return "";
  }
  HalFile root = Storage.open("/XTApps");
  if (!root || !root.isDirectory()) return "";
  for (HalFile entry = root.openNextFile(); entry; entry = root.openNextFile()) {
    char name[128];
    if (!entry.isDirectory() || entry.getName(name, sizeof(name)) == 0 || name[0] == '.') continue;
    const std::string dir = std::string("/XTApps/") + name;
    std::string buffer(kMaxManifest, '\0');
    const size_t n = Storage.readFileToBuffer((dir + "/manifest.json").c_str(), buffer.data(), buffer.size(),
                                              kMaxManifest - 1);
    if (n == 0 || n >= kMaxManifest - 1) continue;
    buffer.resize(n);
    xtapp::Manifest m;
    if (!xtapp::parseManifest(buffer, m) || m.encrypted) continue;
    if (idOrFolder != name && idOrFolder != m.appId) continue;
    if (!Storage.exists((dir + "/" + m.entry).c_str())) continue;
    return dir;
  }
  return "";
}
}  // namespace XtAppLocator
