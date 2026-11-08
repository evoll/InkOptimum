#include "BookOverridesSettings.h"

#include <Logging.h>

#include "InkMODSettings.h"

namespace book_overrides {

namespace {
std::string g_cacheDir;

Values liveValues() { return Values{SETTINGS.fontSize, SETTINGS.lineHeightPercent}; }

bool applyToSettings(const Record& record) {
  Values live = liveValues();
  const bool changed = session().apply(live, record);
  SETTINGS.fontSize = live.fontSize;
  SETTINGS.lineHeightPercent = live.lineHeightPercent;
  return changed;
}
}  // namespace

bool enterBook(const std::string& cacheDir) {
  // A previous book that was not closed cleanly must not leak its override.
  exitBook();
  g_cacheDir = cacheDir;
  Record record;
  if (!load(cacheDir, record)) return false;
  LOG_DBG("BKO", "Applying per-book overrides: font=%u line=%u", static_cast<unsigned>(record.values.fontSize),
          static_cast<unsigned>(record.values.lineHeightPercent));
  return applyToSettings(record);
}

void exitBook() {
  Values live = liveValues();
  if (session().restore(live)) {
    SETTINGS.fontSize = live.fontSize;
    SETTINGS.lineHeightPercent = live.lineHeightPercent;
  }
  g_cacheDir.clear();
}

bool setForBook(const Record& record) {
  if (g_cacheDir.empty()) return false;
  if (!save(g_cacheDir, record)) LOG_ERR("BKO", "Failed to persist book overrides");
  return applyToSettings(record);
}

void persistLiveIfActive() {
  if (g_cacheDir.empty() || !session().active()) return;
  Record record;
  record.present = true;
  record.values = liveValues();
  save(g_cacheDir, record);
}

bool bookHasOverride() { return session().active(); }

}  // namespace book_overrides
