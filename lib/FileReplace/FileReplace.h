#pragma once

#include <string>

// Install a finished temporary file over an existing one without ever leaving the destination
// empty: the old file is renamed to "<dest>.old", the new one takes its name, and only then is the
// old one deleted. If any step fails the old file is put back, so a failed upload, MOVE or
// prepared-cache install keeps what was there before.
//
// `Fs` needs exists(const char*), remove(const char*) and rename(const char*, const char*), all
// returning bool (HalStorage does). Header-only so it can be tested against an in-memory fake.
namespace FileReplace {

template <class Fs>
bool replace(Fs& fs, const char* tempPath, const char* destPath) {
  if (!fs.exists(tempPath)) return false;
  if (!fs.exists(destPath)) return fs.rename(tempPath, destPath);

  const std::string backup = std::string(destPath) + ".old";
  if (fs.exists(backup.c_str()) && !fs.remove(backup.c_str())) return false;
  if (!fs.rename(destPath, backup.c_str())) return false;  // old file untouched

  if (!fs.rename(tempPath, destPath)) {
    fs.rename(backup.c_str(), destPath);  // put the old file back
    return false;
  }
  fs.remove(backup.c_str());  // best effort; a leftover .old is replaced next time
  return true;
}

}  // namespace FileReplace
