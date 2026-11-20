#include "BookOverridesSettings.h"

#include <Logging.h>

#include "InkMODSettings.h"

namespace book_overrides {

namespace {
std::string g_cacheDir;

Values liveValues() { return captureLive(); }

bool applyToSettings(const Record& record) {
  Values live = liveValues();
  const bool changed = session().apply(live, record);
  storeLive(live);
  return changed;
}
}  // namespace

Values captureLive() {
  Values v;
  v.fontSize = SETTINGS.fontSize;
  v.lineHeightPercent = SETTINGS.lineHeightPercent;
  v.screenMargin = SETTINGS.screenMargin;
  v.paragraphAlignment = SETTINGS.paragraphAlignment;
  v.extraParagraphSpacing = SETTINGS.extraParagraphSpacing;
  v.hyphenationEnabled = SETTINGS.hyphenationEnabled;
  v.screenMarginTop = SETTINGS.screenMarginTop;
  v.screenMarginBottom = SETTINGS.screenMarginBottom;
  v.bionicReadingEnabled = SETTINGS.bionicReadingEnabled;
  v.guideReadingEnabled = SETTINGS.guideReadingEnabled;
  return v;
}

void storeLive(const Values& v) {
  SETTINGS.fontSize = v.fontSize;
  SETTINGS.lineHeightPercent = v.lineHeightPercent;
  SETTINGS.screenMargin = v.screenMargin;
  SETTINGS.paragraphAlignment = v.paragraphAlignment;
  SETTINGS.extraParagraphSpacing = v.extraParagraphSpacing;
  SETTINGS.hyphenationEnabled = v.hyphenationEnabled;
  SETTINGS.screenMarginTop = v.screenMarginTop;
  SETTINGS.screenMarginBottom = v.screenMarginBottom;
  SETTINGS.bionicReadingEnabled = v.bionicReadingEnabled;
  SETTINGS.guideReadingEnabled = v.guideReadingEnabled;
}

Record currentAsRecord() {
  Record record;
  record.present = true;
  record.values = captureLive();
  return record;
}

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
  if (session().restore(live)) storeLive(live);
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
  // Fields still equal to the global value stay "follow global", so later global changes keep reaching the book.
  const Values& g = session().globals();
  auto keepInherit = [](uint8_t& field, const uint8_t global) {
    if (field == global) field = kInherit;
  };
  keepInherit(record.values.screenMargin, g.screenMargin);
  keepInherit(record.values.paragraphAlignment, g.paragraphAlignment);
  keepInherit(record.values.extraParagraphSpacing, g.extraParagraphSpacing);
  keepInherit(record.values.hyphenationEnabled, g.hyphenationEnabled);
  keepInherit(record.values.screenMarginTop, g.screenMarginTop);
  keepInherit(record.values.screenMarginBottom, g.screenMarginBottom);
  keepInherit(record.values.bionicReadingEnabled, g.bionicReadingEnabled);
  keepInherit(record.values.guideReadingEnabled, g.guideReadingEnabled);
  save(g_cacheDir, record);
}

bool bookHasOverride() { return session().active(); }

}  // namespace book_overrides
