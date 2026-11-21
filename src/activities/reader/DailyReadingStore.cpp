#include "DailyReadingStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include <string>
#include <vector>

namespace {
constexpr char DAILY_PATH[] = "/.inkmod/daily_seconds.bin";
constexpr char DAILY_TMP_PATH[] = "/.inkmod/daily_seconds.bin.tmp";
}  // namespace

DailyReadingSeconds DailyReadingStore::load() {
  DailyReadingSeconds daily;
  FsFile f;
  if (!Storage.openFileForRead("DAILY", DAILY_PATH, f)) return daily;
  const size_t size = f.fileSize();
  if (size >= 3 && size <= 3 + DailyReadingSeconds::MAX_DAYS * 8) {
    std::vector<uint8_t> buf(size);
    if (f.read(buf.data(), size) != static_cast<int>(size) ||
        !DailyReadingSeconds::deserialize(buf.data(), buf.size(), daily)) {
      LOG_ERR("DAILY", "Damaged daily reading file, starting empty");
      daily = DailyReadingSeconds();
    }
  }
  f.close();
  return daily;
}

bool DailyReadingStore::save(const DailyReadingSeconds& daily) {
  const std::vector<uint8_t> bytes = daily.serialize();
  if (Storage.exists(DAILY_TMP_PATH) && !Storage.remove(DAILY_TMP_PATH)) return false;
  FsFile f;
  if (!Storage.openFileForWrite("DAILY", DAILY_TMP_PATH, f)) return false;
  const bool wrote = f.write(bytes.data(), bytes.size()) == bytes.size();
  f.flush();
  const bool synced = wrote && f.sync();
  const bool closed = f.close();
  if (!wrote || !synced || !closed) {
    Storage.remove(DAILY_TMP_PATH);
    return false;
  }
  if (Storage.exists(DAILY_PATH) && !Storage.remove(DAILY_PATH)) {
    Storage.remove(DAILY_TMP_PATH);
    return false;
  }
  if (!Storage.rename(DAILY_TMP_PATH, DAILY_PATH)) {
    LOG_ERR("DAILY", "Could not replace %s", DAILY_PATH);
    return false;
  }
  return true;
}

void DailyReadingStore::recordSpan(const ReadingStatsDateTime& localStart, const uint32_t seconds) {
  if (!localStart.isValid() || seconds == 0) return;
  DailyReadingSeconds daily = load();
  const uint32_t secondOfDay = static_cast<uint32_t>(localStart.hour) * 3600u +
                               static_cast<uint32_t>(localStart.minute) * 60u + localStart.second;
  daily.record(readingStatsDayIndex(localStart.date), secondOfDay, seconds);
  save(daily);
}
