#include "ButtonSettingsBackup.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <string>
#include <vector>

#include "InkMODSettings.h"
#include "KeyValueBackup.h"
#include "SettingsList.h"

namespace button_backup {

namespace {

struct Field {
  const char* key;
  uint8_t InkMODSettings::* ptr;
  uint8_t fallbackLimit;  // exclusive upper bound when the settings list has no enum entry for it
};

const Field kFields[] = {
    {"shortPwrBtn", &InkMODSettings::shortPwrBtn, 0},
    {"longPwrBtn", &InkMODSettings::longPwrBtn, 0},
    {"doublePwrBtn", &InkMODSettings::doublePwrBtn, 0},
    {"homeDoublePressAction", &InkMODSettings::homeDoublePressAction, 0},
    {"homeLongPressAction", &InkMODSettings::homeLongPressAction, 0},
    {"longPressButtonBehavior", &InkMODSettings::longPressButtonBehavior, 0},
    {"longPressMenuAction", &InkMODSettings::longPressMenuAction, 0},
    {"longPressBackAction", &InkMODSettings::longPressBackAction, 0},
    {"sideButtonLayout", &InkMODSettings::sideButtonLayout, InkMODSettings::SIDE_BUTTON_LAYOUT_COUNT},
    {"sideButtonLongPress", &InkMODSettings::sideButtonLongPress, InkMODSettings::SIDE_LONG_PRESS_COUNT},
    {"frontButtonOrientationAware", &InkMODSettings::frontButtonOrientationAware,
     InkMODSettings::FRONT_ORIENTATION_AWARE_COUNT},
    {"sideButtonOrientationAware", &InkMODSettings::sideButtonOrientationAware, 2},
    {"frontButtonBack", &InkMODSettings::frontButtonBack, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"frontButtonConfirm", &InkMODSettings::frontButtonConfirm, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"frontButtonLeft", &InkMODSettings::frontButtonLeft, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"frontButtonRight", &InkMODSettings::frontButtonRight, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"readerFrontButtonsEnabled", &InkMODSettings::readerFrontButtonsEnabled, 2},
    {"readerFrontButtonBack", &InkMODSettings::readerFrontButtonBack, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"readerFrontButtonConfirm", &InkMODSettings::readerFrontButtonConfirm,
     InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"readerFrontButtonLeft", &InkMODSettings::readerFrontButtonLeft, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
    {"readerFrontButtonRight", &InkMODSettings::readerFrontButtonRight, InkMODSettings::FRONT_BUTTON_HARDWARE_COUNT},
};

// A restored value must be one the UI could have produced; otherwise it is ignored.
bool valueIsValid(const Field& field, const std::vector<SettingInfo>& all, const uint8_t value) {
  for (const auto& info : all) {
    if (info.valuePtr != field.ptr) continue;
    const size_t count = settingEnumOptionCount(info);
    if (count == 0) break;  // not an enum entry: use the fallback bound
    if (info.enumRawValues.empty()) return value < count;
    return std::find(info.enumRawValues.begin(), info.enumRawValues.end(), value) != info.enumRawValues.end();
  }
  return field.fallbackLimit != 0 && value < field.fallbackLimit;
}

}  // namespace

bool save() {
  std::vector<std::pair<std::string, uint8_t>> entries;
  entries.reserve(sizeof(kFields) / sizeof(kFields[0]));
  for (const auto& f : kFields) entries.emplace_back(f.key, SETTINGS.*(f.ptr));
  const std::string text = kv_backup::serialize(entries);

  Storage.mkdir("/.inkmod");
  FsFile file;
  if (!Storage.openFileForWrite("BTNB", kPath, file)) return false;
  const size_t written = file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
  return written == text.size();
}

bool exists() { return Storage.exists(kPath); }

int restore() {
  if (!Storage.exists(kPath)) return -1;
  const String content = Storage.readFile(kPath);
  if (content.isEmpty()) return -1;

  const auto entries = kv_backup::parse(std::string_view(content.c_str(), content.length()));
  const auto all = getSettingsList();
  int applied = 0;
  for (const auto& entry : entries) {
    for (const auto& f : kFields) {
      if (entry.first != f.key) continue;
      if (valueIsValid(f, all, entry.second)) {
        SETTINGS.*(f.ptr) = entry.second;
        ++applied;
      } else {
        LOG_ERR("BTNB", "Ignoring invalid value for %s: %u", f.key, static_cast<unsigned>(entry.second));
      }
      break;
    }
  }
  if (applied > 0) SETTINGS.saveToFile();
  return applied;
}

}  // namespace button_backup
