#include <Arduino.h>
#include <BlePageTurner.h>
#include <BootLog.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalScreenCalibration.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalFrontlight.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <MemoryBudget.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <TrustedTime.h>
#include <WiFi.h>
#include <XteinkDetect.h>
#include <builtinFonts/all.h>
#include <atomic>
#include <ctime>
#include <new>

#include "platform/UsbSerialJtagHandoff.h"

#ifdef SIMULATOR
using esp_reset_reason_t = int;
using esp_sleep_wakeup_cause_t = int;
enum : int {
  ESP_RST_UNKNOWN = 0,
  ESP_RST_POWERON,
  ESP_RST_EXT,
  ESP_RST_SW,
  ESP_RST_PANIC,
  ESP_RST_INT_WDT,
  ESP_RST_TASK_WDT,
  ESP_RST_WDT,
  ESP_RST_DEEPSLEEP,
  ESP_RST_BROWNOUT,
  ESP_RST_SDIO,
  ESP_RST_USB,
  ESP_RST_JTAG,
  ESP_RST_EFUSE,
  ESP_RST_PWR_GLITCH,
  ESP_RST_CPU_LOCKUP
};
enum : int {
  ESP_SLEEP_WAKEUP_UNDEFINED = 0,
  ESP_SLEEP_WAKEUP_ALL,
  ESP_SLEEP_WAKEUP_EXT0,
  ESP_SLEEP_WAKEUP_EXT1,
  ESP_SLEEP_WAKEUP_TIMER,
  ESP_SLEEP_WAKEUP_TOUCHPAD,
  ESP_SLEEP_WAKEUP_ULP,
  ESP_SLEEP_WAKEUP_GPIO,
  ESP_SLEEP_WAKEUP_UART,
  ESP_SLEEP_WAKEUP_WIFI,
  ESP_SLEEP_WAKEUP_COCPU,
  ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG,
  ESP_SLEEP_WAKEUP_BT
};
inline esp_reset_reason_t esp_reset_reason() { return ESP_RST_UNKNOWN; }
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return ESP_SLEEP_WAKEUP_UNDEFINED; }
#else
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <esp_system.h>
#endif

#include <algorithm>
#include <cstring>

#include "AppVersion.h"
#include "InkMODSettings.h"
#include <TlsTrust.h>

#include "util/PluginEventBus.h"
#include "util/SleepSync.h"
#include "util/ClockStandby.h"
#include "activities/apps/ClockFace.h"
#include "util/PluginLocations.h"
#include "weather/WeatherService.h"
#include "InkMODState.h"
#include "GlobalActions.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "WifiCredentialStore.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EpubReaderUtils.h"
#include "activities/reader/KOReaderSyncActivity.h"
#include "activities/reader/ReadingStatsUtils.h"
#include "activities/reader/ReaderUtils.h"
#include "activities/reader/StatsBackup.h"
#include "activities/settings/KOReaderSettingsActivity.h"
#include "activities/pin/PinEntryActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "util/PinGate.h"
#ifndef SIMULATOR
#include "network/FirmwareFlasher.h"
#ifndef SIMULATOR
#include "network/BootHealth.h"
#endif
#endif
#include "components/UITheme.h"
#include "fontIds.h"
#include "UiTextSize.h"
#ifdef SIMULATOR
#include "simulator/SimulatorSmokeTest.h"
#endif
#include "SdCardFontSystem.h"
#include "util/LanguagePackLoader.h"
#include <Epub/hyphenation/SdHyphenationPack.h>
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"
#include "util/SerialLineBuffer.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, &renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;

// Fonts
// Only the system UI font (Inter) is built in; it is used for both the
// interface and book text.
EpdFont smallFont(&inter_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&inter_10_regular);
EpdFont ui10BoldFont(&inter_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui11RegularFont(&inter_11_regular);
EpdFont ui11BoldFont(&inter_11_bold);
EpdFontFamily ui11FontFamily(&ui11RegularFont, &ui11BoldFont);
EpdFontFamily ui11BoldFamily(&ui11BoldFont, &ui11BoldFont);

EpdFont ui12RegularFont(&inter_12_regular);
EpdFont ui12BoldFont(&inter_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

EpdFont ui14RegularFont(&inter_14_regular);
EpdFont ui14BoldFont(&inter_14_bold);
EpdFontFamily ui14FontFamily(&ui14RegularFont, &ui14BoldFont);

// "Dark" interface text: real bold faces for regular and explicit-bold draws alike, so width measuring,
// wrapping and drawing agree without synthetic thickening. (No Inter 8 bold exists: SMALL stays regular.)
EpdFontFamily ui10BoldFamily(&ui10BoldFont, &ui10BoldFont);
EpdFontFamily ui12BoldFamily(&ui12BoldFont, &ui12BoldFont);
EpdFontFamily ui14BoldFamily(&ui14BoldFont, &ui14BoldFont);

EpdFont ipaFallback12Font(&inter_12_ipa_fallback);
EpdFontFamily ipaFallback12Family(&ipaFallback12Font);

EpdFont ipaFallback14Font(&inter_14_ipa_fallback);
EpdFontFamily ipaFallback14Family(&ipaFallback14Font);

EpdFont testFonts12Regular(&dejavu_sans_12_regular);
EpdFont testFonts12Bold(&dejavu_sans_12_bold);
EpdFont testFonts12Italic(&dejavu_sans_12_italic);
EpdFont testFonts12BoldItalic(&dejavu_sans_12_bolditalic);
EpdFontFamily testFonts12Family(&testFonts12Regular, &testFonts12Bold, &testFonts12Italic, &testFonts12BoldItalic);

// See UiTextSize.h. Only UI_10_FONT_ID (menu/list body text) grows; two roles
// are deliberately left untouched:
//  - UI_12_FONT_ID has no larger built-in size to grow into.
//  - SMALL_FONT_ID backs the status bar clock and header title, which are
//    vertically centered inside a FIXED-height bar via
//    "(statusBarHeight - lineHeight) / 2" (see BaseTheme::drawTopStatusBarClock).
//    That bar's height doesn't grow with the font, so a taller SMALL_FONT_ID
//    pushes the centered text above the bar - visibly off the top of the
//    screen. Fixing that properly means growing statusBarHeight itself
//    (and everything positioned relative to it) per theme, which needs
//    on-device verification this build can't do; leaving SMALL_FONT_ID alone
//    avoids the regression entirely.
//
// UI_10_FONT_ID's LARGE size is theme-dependent: BaseTheme::drawList() draws
// each row's text flush with the top of a FIXED-height row (no vertical
// centering), so a font taller than the row just crowds into the row below
// instead of vanishing - less severe than the status-bar case, but still not
// something to ship blind everywhere. Measured line heights (EpdFontData's
// advanceY) vs. each theme's ThemeMetrics::listRowHeight:
//   Inter 10pt ~26px / Inter 12pt ~30px / LexendDeca 14pt ~36px
//   BaseMetrics (Classic/Dashboard/Minimal): listRowHeight = 30  -> no room past 12pt
//   LyraCarouselTheme: listRowHeight = 35 / LyraTheme: 36        -> no clean room past 12pt
//   RoundedRaffTheme: listRowHeight = 42                          -> fits LexendDeca 14pt with margin
// So only RoundedRaff gets the bigger LexendDeca bump for now; everything
// else keeps the previously-verified Inter 12pt bump. If you confirm on a
// device that another theme's rows have room too, add it to the switch below.
void applyUiTextSize(GfxRenderer& r) {
  const bool bold = SETTINGS.uiFontStyle == InkMODSettings::UI_FONT_BOLD;
  if (SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_LARGE) {
    // NOTE: RoundedRaff used to get a bigger LexendDeca 14pt bump here instead of the
    // Inter 12pt one every other theme uses (its taller list rows have room for it - see
    // the metrics comment above). Reverted to Inter 12pt for all themes: the checked-in
    // lexenddeca_14_bold.h/lexenddeca_14_regular.h are missing several Cyrillic glyphs
    // (и, ш, л, е, я and others - confirmed against the font's own additional-intervals
    // fallback list in lib/EpdFont/scripts/convert-builtin-fonts.sh) that a ChareInk7
    // fallback was meant to supply at generation time but evidently didn't end up in
    // these particular committed headers, so Cyrillic UI text renders with missing-glyph
    // boxes for those letters. Re-enable the RoundedRaff branch once lexenddeca_14_*.h
    // are regenerated (needs the LexendDeca + ChareInk7 source fonts, not present in
    // this checkout) and verified to have full Cyrillic coverage.
    r.replaceFont(UI_10_FONT_ID, bold ? ui12BoldFamily : ui12FontFamily);
  } else if (SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_MEDIUM) {
    // Inter 11 (~28 px lines) fits every theme's list row (30 px and up) and has the same Latin/Cyrillic coverage.
    r.replaceFont(UI_10_FONT_ID, bold ? ui11BoldFamily : ui11FontFamily);
  } else {
    r.replaceFont(UI_10_FONT_ID, bold ? ui10BoldFamily : ui10FontFamily);
  }
  r.replaceFont(UI_12_FONT_ID, bold ? ui12BoldFamily : ui12FontFamily);
  r.replaceFont(UI_14_FONT_ID, bold ? ui14BoldFamily : ui14FontFamily);
}

int uiControlFontId() {
  return SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_LARGE ? UI_14_FONT_ID : UI_12_FONT_ID;
}

int uiHintFontId() {
  return SETTINGS.uiTextSize == InkMODSettings::UI_TEXT_SIZE_LARGE ? UI_10_FONT_ID : SMALL_FONT_ID;
}

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Set when the screenshot combo (Power + Volume Down) fires, so the subsequent
// power button release does not also trigger a short-press action (e.g. sleep).
static bool screenshotComboHandled = false;

const char* resetReasonName(const esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_USB:
      return "USB";
    case ESP_RST_JTAG:
      return "JTAG";
    case ESP_RST_EFUSE:
      return "EFUSE";
    case ESP_RST_PWR_GLITCH:
      return "PWR_GLITCH";
    case ESP_RST_CPU_LOCKUP:
      return "CPU_LOCKUP";
    case ESP_RST_UNKNOWN:
    default:
      return "UNKNOWN";
  }
}

const char* wakeupCauseName(const esp_sleep_wakeup_cause_t cause) {
  switch (cause) {
    case ESP_SLEEP_WAKEUP_UNDEFINED:
      return "UNDEFINED";
    case ESP_SLEEP_WAKEUP_ALL:
      return "ALL";
    case ESP_SLEEP_WAKEUP_EXT0:
      return "EXT0";
    case ESP_SLEEP_WAKEUP_EXT1:
      return "EXT1";
    case ESP_SLEEP_WAKEUP_TIMER:
      return "TIMER";
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      return "TOUCHPAD";
    case ESP_SLEEP_WAKEUP_ULP:
      return "ULP";
    case ESP_SLEEP_WAKEUP_GPIO:
      return "GPIO";
    case ESP_SLEEP_WAKEUP_UART:
      return "UART";
    case ESP_SLEEP_WAKEUP_WIFI:
      return "WIFI";
    case ESP_SLEEP_WAKEUP_COCPU:
      return "COCPU";
    case ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG:
      return "COCPU_TRAP";
    case ESP_SLEEP_WAKEUP_BT:
      return "BT";
    default:
      return "UNKNOWN";
  }
}

const char* wakeupRouteName(const HalGPIO::WakeupReason reason) {
  switch (reason) {
    case HalGPIO::WakeupReason::PowerButton:
      return "PowerButton";
    case HalGPIO::WakeupReason::AfterFlash:
      return "AfterFlash";
    case HalGPIO::WakeupReason::AfterUSBPower:
      return "AfterUSBPower";
    case HalGPIO::WakeupReason::Other:
    default:
      return "Other";
  }
}

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
// Plugin events at sleep entry on a board that cannot hold Wi-Fi + TLS beside two framebuffers (ESP32-C3): the sleep
// screen is painted, the chip restarts into SEND (this boot sends the queue before the display allocates), which
// restarts into SLEEP (the sleep that finishes without painting again). Two targets each, so fromTimeout survives.
constexpr uint32_t SILENT_REBOOT_TARGET_SLEEP_SEND = 2;
constexpr uint32_t SILENT_REBOOT_TARGET_SLEEP_SEND_TIMEOUT = 3;
constexpr uint32_t SILENT_REBOOT_TARGET_SLEEP = 4;
constexpr uint32_t SILENT_REBOOT_TARGET_SLEEP_TIMEOUT = 5;
// Upper bound for the cold-boot sanity check on silentRebootTarget (RTC_NOINIT is uninitialized on power-up).
// Must equal the highest target above.
constexpr uint32_t SILENT_REBOOT_TARGET_MAX = SILENT_REBOOT_TARGET_SLEEP_TIMEOUT;
// True in the sleep a clean-boot send restarted into: the events went (or were tried) and the screen painted before the
// first restart is still on the panel, so this pass neither queues sleep.enter again nor paints.
static bool sleepEventsSentPass = false;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,       // cold boot, flash, panic, or plain reboot
  Silent,       // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  QuickResume,  // wake from a quick-resume deep sleep (SD flag; survives power loss)
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}


void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  // X4 Pro USB MSC uses the ESP32-S3 OTG peripheral on the same physical
  // USB pins as Serial/JTAG. A plain ESP.restart() can leave the host/PHY
  // handoff in a bad state, so restore Serial/JTAG ownership first.
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

struct EmergencyFirmwareFile {
  const char* path;
  const char* appliedPath;
  const char* incompatiblePath;
  const char* failedPath;
  // Board-specific trigger: the user explicitly names the target board in the file name, so an image
  // without an inkOPTIMUM board marker (stock / third-party build) is accepted without confirmation.
  // A marker naming a *different* board is still refused.
  bool allowUntagged;
};

// Both names are supported as headless, power-on recovery triggers. Keep the
// inkMOD-specific name first so an explicitly prepared recovery image wins if
// both files happen to be present on the same SD card.
static constexpr EmergencyFirmwareFile EMERGENCY_FIRMWARE_FILES[] = {
// Board-specific names are honoured only by the matching build, so a file prepared for the other
// ESP32-S3 board is simply ignored instead of being flashed.
#if defined(FREEINK_DEVICE_X4PRO)
    {"/inkoptimum-x4pro-recovery.bin", "/inkoptimum-x4pro-recovery.applied.bin",
     "/inkoptimum-x4pro-recovery.incompatible.bin", "/inkoptimum-x4pro-recovery.failed.bin", true},
    {"/x4pro-update.bin", "/x4pro-update.applied.bin", "/x4pro-update.incompatible.bin", "/x4pro-update.failed.bin",
     true},
#elif defined(FREEINK_DEVICE_X4CLASSIC)
    {"/inkoptimum-x4c-recovery.bin", "/inkoptimum-x4c-recovery.applied.bin",
     "/inkoptimum-x4c-recovery.incompatible.bin", "/inkoptimum-x4c-recovery.failed.bin", true},
    {"/x4c-update.bin", "/x4c-update.applied.bin", "/x4c-update.incompatible.bin", "/x4c-update.failed.bin", true},
#endif
    {"/inkmod-recovery.bin", "/inkmod-recovery.applied.bin", "/inkmod-recovery.incompatible.bin",
     "/inkmod-recovery.failed.bin", false},
    {"/update.bin", "/update.applied.bin", "/update.incompatible.bin", "/update.failed.bin", false},
};

static bool renameEmergencyFirmware(const char* source, const char* destination) {
  Storage.remove(destination);
  if (Storage.rename(source, destination)) return true;
  LOG_ERR("FW", "Could not rename recovery trigger %s -> %s", source, destination);
  return false;
}

static void tryEmergencyFirmwareUpdate() {
#ifndef SIMULATOR
  for (const auto& fw : EMERGENCY_FIRMWARE_FILES) {
    if (!Storage.exists(fw.path)) continue;

    LOG_INF("FW", "Emergency SD firmware detected: %s%s", fw.path,
            fw.allowUntagged ? " (board-named trigger: unmarked image allowed, no confirmation)" : "");
    const auto result = firmware_flash::flashFromSdPath(fw.path, nullptr, nullptr, /*alreadyValidated=*/false, fw.allowUntagged);
    if (result != firmware_flash::Result::OK) {
      LOG_ERR("FW", "Emergency SD firmware failed: %s (%s)", fw.path, firmware_flash::resultName(result));

      // A C3/S3 or X4 Pro/X4 Classic mismatch is not a broken image: mark it explicitly so the user
      // can see the reason directly on the SD card and so boot does not retry it
      // forever. Most importantly, FirmwareFlasher returns these results before
      // any erase/write operation touches the OTA partition.
      if (result == firmware_flash::Result::INCOMPATIBLE_CHIP ||
          result == firmware_flash::Result::INCOMPATIBLE_BOARD ||
          result == firmware_flash::Result::UNTAGGED_IMAGE) {
        if (renameEmergencyFirmware(fw.path, fw.incompatiblePath)) {
          LOG_ERR("FW", "Incompatible recovery firmware renamed to %s", fw.incompatiblePath);
        }
        return;
      }

      // Preserve the existing behavior for corrupt/truncated/I/O-failed files.
      // If rename itself fails, remove the trigger as the old implementation did
      // to prevent an endless failing recovery loop.
      if (!renameEmergencyFirmware(fw.path, fw.failedPath)) {
        Storage.remove(fw.path);
      }
      return;
    }

    bool disarmed = renameEmergencyFirmware(fw.path, fw.appliedPath);
    if (!disarmed) {
      disarmed = Storage.remove(fw.path);
    }
    if (!disarmed) {
      LOG_ERR("FW", "Emergency firmware applied but trigger could not be disarmed; refusing auto-restart");
      return;
    }

    LOG_INF("FW", "Emergency SD firmware applied from %s; restarting", fw.path);
    delay(500);
    ESP.restart();
    return;
  }
#endif
}

static constexpr char EMERGENCY_FIRMWARE_PATH[] = "/inkmod-recovery.bin";
static constexpr char EMERGENCY_FIRMWARE_APPLIED_PATH[] = "/inkmod-recovery.applied.bin";
static constexpr char EMERGENCY_FIRMWARE_FAILED_PATH[] = "/inkmod-recovery.failed.bin";

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

bool isGlobalPowerButtonAction(const InkMODSettings::SHORT_PWRBTN action) {
  return isPowerButtonActionAvailableOutsideReader(action);
}

// UNUSED as of the SYNC_PROGRESS removal from the power-button action lists
// (see SettingsList.h and the SYNC_PROGRESS case in
// handleGlobalShortcutAction() below) - kept only in case a
// reader-independent sync entry point is worth revisiting later. Do not wire
// this back up to a button without also re-solving why it kept hanging: it
// went through peak-memory OOM, then a RenderLock deadlock, then a
// render-task SD-card race, each fixed in turn and each still followed by a
// hang on real hardware. The safe, working equivalent is
// EpubReaderMenuActivity::MenuAction::SYNC, reached only from inside the
// reader, which reuses the already-loaded Epub instead of loading a second
// one and never leaves the reader running in the background while it does.
bool startGlobalSyncProgress() {
  // If a reader is currently open (e.g. this was triggered by a long-press
  // while reading, not from the in-reader menu), its decoded Section is
  // still fully resident at this point. Free it now, before loading a second
  // independent Epub for potentially the same file below - otherwise both
  // the reader's full working set AND this fresh load compete for RAM at
  // once, which is exactly the peak that was crashing/hanging the device.
  // (The in-reader "Sync" menu item never hit this because it reuses the
  // already-loaded Epub and frees its own Section itself before proceeding.)
  activityManager.releaseCurrentActivityHeavyResources();

  if (!KOREADER_STORE.hasCredentials()) {
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  const std::string epubPath = APP_STATE.openEpubPath;
  if (epubPath.empty() || !FsHelpers::hasEpubExtension(epubPath) || !Storage.exists(epubPath.c_str())) {
    LOG_DBG("MAIN", "No syncable EPUB open, opening KOReader settings instead");
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  auto epub = std::make_shared<Epub>(epubPath, "/.inkmod");
  if (!epub->load(true, SETTINGS.embeddedStyle == 0)) {
    LOG_ERR("MAIN", "Failed to load EPUB for global sync: %s", epubPath.c_str());
    activityManager.pushActivity(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInputManager));
    return true;
  }

  epub->setupCacheDir();

  int spineIndex = 0;
  int pageNumber = 0;
  int totalPagesInSpine = 1;
  EpubReaderUtils::Progress progress;
  if (EpubReaderUtils::loadProgress(*epub, progress, "MAIN")) {
    spineIndex = progress.spineIndex;
    pageNumber = progress.pageNumber;
    if (progress.hasPageCount) {
      totalPagesInSpine = std::max(1, progress.pageCount);
    }
  }

  if (spineIndex < 0 || spineIndex >= epub->getSpineItemsCount()) {
    spineIndex = 0;
  }

  InkMODPosition localPos = {spineIndex, pageNumber, totalPagesInSpine};
  KOReaderPosition localKoPos = ProgressMapper::toKOReader(epub, localPos);
  const int tocIdx = epub->getTocIndexForSpineIndex(spineIndex);
  std::string localChapterName = (tocIdx >= 0) ? epub->getTocItem(tocIdx).title : "";

  activityManager.pushActivity(
      std::make_unique<KOReaderSyncActivity>(renderer, mappedInputManager, epubPath, spineIndex, pageNumber,
                                             totalPagesInSpine, std::move(localKoPos), std::move(localChapterName)));
  return true;
}

InkMODSettings::SHORT_PWRBTN getPowerButtonAction() {
  static bool longPowerButtonHandled = false;

  if (mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    if (longPowerButtonHandled) {
      longPowerButtonHandled = false;
      screenshotComboHandled = false;
      return InkMODSettings::SHORT_PWRBTN::IGNORE;
    }

    if (screenshotComboHandled) {
      screenshotComboHandled = false;
      return InkMODSettings::SHORT_PWRBTN::IGNORE;
    }

    return mappedInputManager.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()
               ? static_cast<InkMODSettings::SHORT_PWRBTN>(SETTINGS.shortPwrBtn)
               : static_cast<InkMODSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn);
  }

  if (longPowerButtonHandled || !mappedInputManager.isPressed(MappedInputManager::Button::Power) ||
      mappedInputManager.getHeldTime() < SETTINGS.getPowerButtonLongPressDuration()) {
    return InkMODSettings::SHORT_PWRBTN::IGNORE;
  }

  const auto action = static_cast<InkMODSettings::SHORT_PWRBTN>(SETTINGS.longPwrBtn);
  if (!isGlobalPowerButtonAction(action)) {
    return InkMODSettings::SHORT_PWRBTN::IGNORE;
  }

  longPowerButtonHandled = true;
  return action;
}

bool handleGlobalShortcutAction(const InkMODSettings::SHORT_PWRBTN action) {
  switch (action) {
    case InkMODSettings::SHORT_PWRBTN::SLEEP:
      enterDeepSleep();
      return true;
    case InkMODSettings::SHORT_PWRBTN::QUICK_RESUME_SLEEP:
      enterDeepSleep(true);
      return true;
    case InkMODSettings::SHORT_PWRBTN::FORCE_REFRESH: {
      LOG_DBG("MAIN", "Manual full screen refresh triggered");
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::FULL_REFRESH);
      return true;
    }
    case InkMODSettings::SHORT_PWRBTN::REINFORCE_SCREEN: {
      // Exactly like YACP: readers route the action through their normal render
      // path so AA/grayscale stays coherent. Outside a reader, reinforce the
      // currently displayed B/W framebuffer directly.
      if (activityManager.isCurrentReaderActivity()) {
        return false;
      }
      LOG_DBG("MAIN", "Manual X3 no-flash screen reinforcement triggered");
      RenderLock lock;
      if (gpio.deviceIsX3()) {
        ReaderUtils::displayX3MaximumNoFlash(renderer);
      } else {
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
      return true;
    }
    case InkMODSettings::SHORT_PWRBTN::SCREENSHOT: {
      // Screenshot feedback must also work on Home, dictionary and note editor.
      // canSnapshotForSleepOverlay() is a sleep-overlay capability check, not
      // a reason to suppress explicit user-requested screenshots.
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
      return true;
    }
    case InkMODSettings::SHORT_PWRBTN::SYNC_PROGRESS:
      // No longer assignable from Settings (see SettingsList.h) after
      // repeated hangs on this path despite several attempted fixes
      // (peak-memory OOM, then a RenderLock deadlock, then a render-task
      // race - all specific to reaching sync from *outside* the reader).
      // A pre-existing saved setting could still carry this raw value, so
      // handle it explicitly rather than relying on it simply not being
      // selectable going forward: do nothing, same as IGNORE. The identical
      // sync action remains available and reliable from the in-reader menu
      // (EpubReaderMenuActivity::MenuAction::SYNC), which reuses the
      // already-loaded Epub instead of loading a second one.
      return false;
    case InkMODSettings::SHORT_PWRBTN::FILE_TRANSFER:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToFileTransfer();
      return true;
    case InkMODSettings::SHORT_PWRBTN::CALIBRE_WIRELESS:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToCalibreWireless();
      return true;
    case InkMODSettings::SHORT_PWRBTN::JOIN_NETWORK:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToJoinNetworkFileTransfer();
      return true;
    case InkMODSettings::SHORT_PWRBTN::CREATE_HOTSPOT:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToHotspotFileTransfer();
      return true;
    case InkMODSettings::SHORT_PWRBTN::FILE_BROWSER:
      if (activityManager.canSnapshotForSleepOverlay()) {
        return false;
      }
      activityManager.goToFileBrowser();
      return true;
    case InkMODSettings::SHORT_PWRBTN::TOGGLE_FRONTLIGHT:
      // Global action so a short Power press can light the device even from
      // Home/menus, before the user can see the on-screen frontlight control.
      // On boards without a frontlight this is a harmless no-op.
      if (!Frontlight.present()) {
        return false;
      }
      Frontlight.setOn(!Frontlight.isOn());
      return true;
    default:
      return false;
  }
}

namespace {
constexpr uint16_t POST_SLEEP_SCREEN_SETTLE_MS = 500;
constexpr uint8_t TILT_SLEEP_MAX_ATTEMPTS = 3;
constexpr uint16_t TILT_SLEEP_RETRY_DELAY_MS = 10;

void putTiltSensorToSleepForDeepSleep() {
  if (!halTiltSensor.isAvailable()) {
    return;
  }

  for (uint8_t attempt = 0; attempt < TILT_SLEEP_MAX_ATTEMPTS; ++attempt) {
    if (halTiltSensor.deepSleep()) {
      return;
    }
    delay(TILT_SLEEP_RETRY_DELAY_MS);
  }
  LOG_ERR("MAIN", "Tilt sensor did not confirm sleep before deep sleep");
}
}  // namespace

constexpr char SLEEP_FRAME_FILE[] = "/.inkmod/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Bluetooth page turner (lib/BlePageTurner): what the module may ask of this firmware. The module
// owns the radio; loop() tells it what is on screen, and the screen changes and sleep ask it to stop.
namespace pageturner {
std::atomic<bool> holdsCpu{false};

bleturner::Heap heap() {
  return {heap_caps_get_free_size(MALLOC_CAP_INTERNAL), heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)};
}

// Runs on the radio's start task. Only TRIES the render lock: the render task can hold it through a
// chapter build that is waiting for this very radio.
bool releaseCaches() {
  RenderLock lock(RenderLock::Mode::Try);
  if (!lock.ownsLock()) return false;
  const int fontId = SETTINGS.getReaderFontId();
  if (renderer.isSdCardFont(fontId)) renderer.trimSdCardFontForLowMemory(fontId);
  return true;
}

bool deliver(const bleturner::Action action) {
  switch (action) {
    case bleturner::Action::NextPage:
      return activityManager.remoteTurn(true, false);
    case bleturner::Action::PrevPage:
      return activityManager.remoteTurn(false, false);
    case bleturner::Action::NextChapter:
      return activityManager.remoteTurn(true, true);
    case bleturner::Action::PrevChapter:
      return activityManager.remoteTurn(false, true);
    case bleturner::Action::Confirm:
      return activityManager.remoteClick(RemoteButton::Confirm);
    case bleturner::Action::Back:
      return activityManager.remoteClick(RemoteButton::Back);
    case bleturner::Action::ReaderMenu:
      return activityManager.remoteShortcut(true);
    case bleturner::Action::SaveQuote:
      return activityManager.remoteShortcut(false);
    case bleturner::Action::None:
      return false;
  }
  return false;
}

bool yieldForRadio() { return true; }        // the reader keeps no parser alive between pages
bool fileTransferActive() { return false; }  // file transfer is a screen of its own: the radio is off there
void restartIntoBook() { silentRestartToReader(); }

// The radio needs a steady clock while it is up or changing state.
void holdFullSpeed(const bool hold) {
  holdsCpu.store(hold);
  if (hold) powerManager.setPowerSaving(false);
}

void log(const bool error, const char* format, va_list args) {
#ifdef ENABLE_SERIAL_LOG
  char line[160];
  vsnprintf(line, sizeof(line), format, args);
  if (error) {
    LOG_ERR("BLE", "%s", line);
  } else {
    LOG_DBG("BLE", "%s", line);
  }
#else
  (void)error;
  (void)format;
  (void)args;
#endif
}

const bleturner::Host host{heap,          releaseCaches, deliver, yieldForRadio, fileTransferActive, restartIntoBook,
                           holdFullSpeed, log,           nullptr};
}  // namespace pageturner

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  // A configured X4 Pro Home shortcut enters sleep after the activity dispatch
  // finishes. Persist the actual reader path and a clean resume guard BEFORE
  // ActivityManager replaces/destroys the reader. Otherwise a stale load-count
  // or missing last-book path can route the next wake to Home.
  // The sleep a clean-boot send restarted into has no activity: APP_STATE already holds what the first pass saved.
  const bool sleepFromReader = !sleepEventsSentPass && activityManager.isReaderActivity();
  const std::string sleepBookPath = sleepFromReader ? activityManager.getCurrentBookPath() : std::string{};
  if (!sleepEventsSentPass) {
    APP_STATE.lastSleepFromReader = sleepFromReader;
    if (sleepFromReader && !sleepBookPath.empty()) {
      APP_STATE.openEpubPath = sleepBookPath;
      APP_STATE.readerActivityLoadCount = 0;  // normal sleep is not a reader crash
    }
  }

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == InkMODSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == InkMODSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Any automatic timeout sleep should wake like a real resume, not a cold boot.
  // The selected timeout sleep screen is still rendered; we simply preserve it
  // and skip the inkMOD splash on wake. Manual/power-button sleep keeps the splash.
  const bool resumeWithoutSplash = fromTimeout || isQuickResumeSleep;
  APP_STATE.showBootScreen = !resumeWithoutSplash;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  // Stop Bluetooth before plugin delivery can start WiFi; a timeout defers delivery.
  const bool bluetoothStopped = bleturner::beforeSleep(2000);
  if (!bluetoothStopped) LOG_ERR("SLP", "Bluetooth did not stop; deferring sleep-event delivery");
  // Sleep may end in a power-off: persist the clock floor so a later cold boot resumes from it.
  trustedtime::note();
  bool sendEventsFromCleanBoot = false;
  if (!sleepEventsSentPass) {
    // sleep.enter: queue it and, when Wi-Fi is already up, deliver before the sleep screen renders (a handler
    // may fetch /sleep.bmp). A handler with "connect": true may bring Wi-Fi up for this (see drainAtSleep).
    plugincat::Vars vars;
    if (activityManager.isReaderActivity()) {
      vars.emplace_back("book", activityManager.getCurrentBookPath());
      const ScreenshotInfo info = activityManager.getScreenshotInfo();
      vars.emplace_back("percent", std::to_string(info.progressPercent));
      vars.emplace_back("end_percent", std::to_string(info.endProgressPercent));
    }
    PluginEventBus::emit("sleep.enter", vars);
    // With Bluetooth still up there is no heap to start Wi-Fi in: the queued events retry on the next drain.
    if (bluetoothStopped) {
#if FREEINK_MCU_C3
      // C3: no room for Wi-Fi + TLS beside the framebuffers. Paint first, then send from a clean boot (below).
      if (PluginEventBus::wantsCleanBootSend(powerManager.getBatteryPercentage())) {
        sendEventsFromCleanBoot = true;
      } else {
        PluginEventBus::drainAtSleep(powerManager.getBatteryPercentage(), tr(STR_PLUGIN_CONNECTING));
      }
#else
      PluginEventBus::drainAtSleep(powerManager.getBatteryPercentage(), tr(STR_PLUGIN_CONNECTING));
#endif
    }
  }
  if (!sleepEventsSentPass) activityManager.goToSleep(fromTimeout);

  // Sync On Sleep: the reader has closed (its position snapshot is saved) and the sleep screen is on the panel.
  // C3 has no heap for Wi-Fi + TLS beside the framebuffers, so it goes through the clean-boot restart as well.
  if (!sleepEventsSentPass && bluetoothStopped && SleepSync::wanted(powerManager.getBatteryPercentage())) {
#if FREEINK_MCU_C3
    sendEventsFromCleanBoot = true;
#else
    SleepSync::run();
#endif
  }

  if (sleepEventsSentPass) {
    // The panel already holds the sleep screen, and the frame buffer saved for Quick Resume was written before the restart.
  } else if (resumeWithoutSplash) {
    saveSleepFrameBuffer();
  } else {
    delay(POST_SLEEP_SCREEN_SETTLE_MS);
  }

  // The screen is up and the reader closed (its reader.exit queued): the queue goes from a clean boot, which restarts
  // into this function to finish the sleep.
  if (sendEventsFromCleanBoot) {
    silentRebootTarget = fromTimeout ? SILENT_REBOOT_TARGET_SLEEP_SEND_TIMEOUT : SILENT_REBOOT_TARGET_SLEEP_SEND;
    silentRebootMagic = SILENT_REBOOT_MAGIC;
    LOG_INF("PEVT", "events wait for a connection: sending them from a clean boot");
    delay(50);
    ESP.restart();
    return;
  }

  if (SETTINGS.autoBackupStats != 0) {
    ReadingStatsDateTime now;
    if (getCurrentLocalReadingStatsDateTime(now) && !backupGlobalStats(false)) {
      LOG_ERR("MAIN", "Automatic reading-stats backup failed before deep sleep");
    }
  }

  putTiltSensorToSleepForDeepSleep();

  // X3: if a BQ27220 Data Memory update is in progress, leave CONFIG UPDATE
  // cleanly and reseal the gauge before power is removed.
  if (gpio.deviceIsX3() && powerManager.isGaugeCapacityLoadPending()) {
    RenderLock lock;
    powerManager.abandonGaugeCapacityLoad();
  }

  display.deepSleep();

  // Fully shut down SD/SDMMC before the board rails are cut. On X4 Pro the
  // card is on a switched rail, but leaving the SDMMC peripheral and its pins
  // active can back-power the card through IO during deep sleep. CrossPoint's
  // current sleep path performs the same teardown before entering S3 sleep.
  Storage.prepareForDeepSleep();

  LOG_DBG("MAIN", "Entering deep sleep");
  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
#if !FREEINK_MCU_C3 && !defined(SIMULATOR)
  // CrossPoint 1.6.0 parity for X4 Pro / ESP32-S3: the controller probe must
  // run before display.begin() chooses and initializes the panel driver.
  // C3 devices already do this from HalGPIO::begin().
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    BootLog::step("MAIN", "X4 Pro: probing display controller before display.begin()");
    const bool promoted = freeink::applyXteinkDisplayController();
    BootLog::stepf("MAIN", "X4 Pro: display probe promoted=%d controller=%d", promoted ? 1 : 0,
                   static_cast<int>(BoardConfig::ACTIVE.displayController));
    if (promoted) {
      LOG_DBG("MAIN", "Panel controller: UltraChip UC81xx variant detected");
    }
  }
#endif

  BootLog::step("MAIN", "setupDisplayAndFonts: calling display.begin()");
#ifdef SIMULATOR
  (void)seamless;
  display.begin();
#else
  display.begin(seamless);
#endif
  BootLog::step("MAIN", "setupDisplayAndFonts: display.begin() returned");
  renderer.begin();
  renderer.setViewableInsets(HalScreenCalibration::load());
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");
  BootLog::step("MAIN", "renderer/activityManager begin() returned");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);

  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(UI_14_FONT_ID, ui14FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.insertFont(IPA_FALLBACK_12_FONT_ID, ipaFallback12Family);
  renderer.insertFont(IPA_FALLBACK_14_FONT_ID, ipaFallback14Family);
  renderer.insertFont(TEST_FONTS_12_FONT_ID, testFonts12Family);

  // Missing glyphs in a user reading font are drawn from the IPA-capable
  // built-in Inter fallback. Normal glyphs stay in the selected book font.
  renderer.setMissingGlyphFallbackFonts(IPA_FALLBACK_12_FONT_ID, IPA_FALLBACK_14_FONT_ID);
  // Chinese/Japanese/Korean interface text (and the language names in the language list) borrows its
  // ideographs from the SD reading font; with no CJK font loaded those characters show as missing glyphs.
  renderer.setUiCjkEnabled(true);
  TlsTrust::setModeProvider([]() { return static_cast<tlspolicy::Mode>(SETTINGS.tlsMode); });

  applyUiTextSize(renderer);

  // Discover and load SD card fonts (font-pack removed, so this will find none,
  // but other activities still depend on sdFontSystem being initialized).
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}


// ---- Clock power saving (Apps > Clock > Eco) ---------------------------------------------------------------------
namespace ClockStandby {
namespace {
constexpr int kWakeLeadMs = 1500;  // wake this long before the minute boundary: boot and the RTC read take ~0.5 s
constexpr int kBatteryFloorPercent = 8;

// Draws the clock face for the current RTC minute (full screen, no button hints). False when there is no valid time.
bool paintFrame(const bool fullRefresh) {
  halClock.invalidateCache();
  char buf[9] = {};
  int hour = 0;
  int minute = 0;
  if (!halClock.formatTime(buf, sizeof(buf), SETTINGS.clockUtcOffsetQ, false) ||
      sscanf(buf, "%d:%d", &hour, &minute) != 2) {
    return false;
  }
  char date[13] = {};
  const bool hasDate = halClock.formatDate(date, sizeof(date), SETTINGS.clockUtcOffsetQ);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.clearScreen();
  const int height = renderer.getScreenHeight();
  ClockFace::draw(renderer, std::clamp<int>(SETTINGS.clockFace, 0, ClockFace::kFaceCount - 1),
                  SETTINGS.clockFormat == 1, hour, minute, hasDate ? date : nullptr, 24, height - 48);
  renderer.displayBuffer(fullRefresh ? HalDisplay::FULL_REFRESH : HalDisplay::FAST_REFRESH);
  return true;
}

[[noreturn]] void sleepUntilNextMinute() {
  // Seconds come from the chip, read as late as possible so the display time does not shift the wake.
  uint8_t second = 0;
  const bool armed = halClock.readSecond(second) && powerManager.getBatteryPercentage() >= kBatteryFloorPercent;
  if (armed) {
    esp_sleep_enable_timer_wakeup(sleepMicros(second, kWakeLeadMs));
  } else {
    LOG_INF("CLK", "standby: no second / low battery, sleeping without a wake timer");
  }
  putTiltSensorToSleepForDeepSleep();
  display.deepSleep();
  Storage.prepareForDeepSleep();
  powerManager.startDeepSleep(gpio);
  for (;;) delay(1000);  // startDeepSleep() does not return
}
}  // namespace

[[noreturn]] void enter() {
  deepSleepInProgress = true;  // no heap-defrag restart may run while the device goes down
  {
    RenderLock lock;  // the activity's render task must not draw while the standby frame goes out
    const bool ok = paintFrame(true);
    LOG_INF("CLK", "standby: frame %s", ok ? "painted" : "FAILED");
  }
  sleepUntilNextMinute();
}

bool onTimerWake() {
  if (!halClock.isAvailable() || halClock.needsPeriodicNTPSync() || !halClock.hasValidDateTime()) return false;
  setupDisplayAndFonts(true);  // the panel still holds the last frame: no reset, no splash
  // The timer fires ~1.5 s early; wait for the boundary so the minute that is drawn is the new one. A wake far from
  // the boundary (RTC timer drift) just draws what the chip says and the next sleep re-aligns.
  uint8_t second = 0;
  if (halClock.readSecond(second) && second >= 56) {
    delay(static_cast<uint32_t>((60 - second) * 1000 + 150));
  }
  uint8_t h = 0, m = 0;
  halClock.invalidateCache();
  const bool tenth = halClock.getTime(h, m) && (m % 10 == 0);
  deepSleepInProgress = true;
  if (!paintFrame(tenth)) {
    deepSleepInProgress = false;
    return false;
  }
  sleepUntilNextMinute();
}

}  // namespace ClockStandby

// Devices without a battery-backed RTC (X4) lose track of time on every real power
// loss. If we don't already have a valid time (i.e. this is a cold boot, not a
// resume from deep sleep, where the ESP32's internal clock is preserved), briefly
// and silently join the last-used saved WiFi network, sync via NTP, then disconnect
// again. No UI is shown for this - it either succeeds quietly or is skipped/fails
// quietly, and reading/browsing continues normally either way.
//
// Split in two: checkSilentBootTimeSyncCandidate() only touches the SD card, so it
// must run before the display/SPI bus is initialized. attemptSilentBootTimeSync()
// only touches WiFi/NTP (no SD access), so it's safe to run afterwards, behind the
// splash screen, without racing the shared SPI bus.
struct BootTimeSyncCandidate {
  bool shouldAttempt = false;
  std::string ssid;
  std::string password;
};

BootTimeSyncCandidate checkSilentBootTimeSyncCandidate() {
  BootTimeSyncCandidate candidate;

  if (SETTINGS.clockDisabled) {
    return candidate;  // User turned the clock off (X4) - don't join WiFi just to sync it.
  }

  // Decide from the clock value, not merely from the presence of a hardware
  // RTC. X4 Pro/Classic BM8563 can still answer on I2C after a complete battery
  // discharge while containing an invalid/reset date. In that state the old
  // needsPeriodicNTPSync() gate skipped boot-time NTP forever, so Calendar sleep
  // fell back to the standard screen until the user manually connected WiFi.
  if (!halClock.isAvailable() || halClock.hasValidDateTime()) {
    return candidate;
  }

  WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (lastSsid.empty()) {
    LOG_DBG("CLK", "No saved WiFi network - skipping silent boot-time sync");
    return candidate;
  }
  const auto* cred = WIFI_STORE.findCredential(lastSsid);
  if (!cred) {
    LOG_DBG("CLK", "Saved network '%s' has no stored credential - skipping silent boot-time sync",
            lastSsid.c_str());
    return candidate;
  }

  candidate.shouldAttempt = true;
  candidate.ssid = cred->ssid;
  candidate.password = cred->password;
  return candidate;
}

// Best-effort immediate fallback for boots where checkSilentBootTimeSyncCandidate() found no
// valid time yet: seed the software clock from the last value a real NTP sync produced, so
// date/time-dependent UI (e.g. the Calendar sleep screen) has *something* correct-ish to show
// right away instead of nothing while attemptSilentBootTimeSync() below tries (and may fail,
// e.g. no WiFi in range) to get a fresh one. No-op on X3 (real battery-backed RTC) or if we've
// never synced before.
void seedClockFromLastKnownTime() {
  if (SETTINGS.clockDisabled || !halClock.needsPeriodicNTPSync()) return;
  uint8_t hour, minute;
  if (halClock.getTime(hour, minute)) return;  // already have a valid time this boot
  if (SETTINGS.clockLastSyncedEpoch == 0) return;  // never synced before - nothing to fall back to
  halClock.seedFallbackTime(static_cast<time_t>(SETTINGS.clockLastSyncedEpoch));
}

void attemptSilentBootTimeSync(const BootTimeSyncCandidate& candidate) {
  if (!candidate.shouldAttempt) {
    return;
  }

  LOG_INF("CLK", "No valid time on boot - briefly joining saved network '%s' to sync time",
          candidate.ssid.c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(candidate.ssid.c_str(), candidate.password.c_str());

  constexpr unsigned long kConnectTimeoutMs = 6000;
  const unsigned long connectStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - connectStart) < kConnectTimeoutMs) {
    delay(100);
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (halClock.syncFromNTP()) {
      // Display is already initialized at this point (SPI is shared with the SD card),
      // so this save needs the same render lock other post-boot SD writes use.
      RenderLock lock;
      SETTINGS.clockHasBeenSynced = 1;
      SETTINGS.clockDateHasBeenSynced = 1;
      SETTINGS.clockLastSyncedEpoch = static_cast<uint32_t>(time(nullptr));
      SETTINGS.saveToFile();
      LOG_INF("CLK", "Silent boot-time sync succeeded");
    } else {
      LOG_ERR("CLK", "Silent boot-time sync: NTP request failed");
    }
  } else {
    LOG_ERR("CLK", "Silent boot-time sync: could not reach saved network '%s'", candidate.ssid.c_str());
  }

  // This was only ever meant to be a brief dip to fetch the time, not a lasting
  // connection - always leave WiFi off again afterwards, success or failure.
  WiFi.disconnect(true);
  delay(30);
  WiFi.mode(WIFI_OFF);
}

// Called by every plain `new`/`std::vector`/`std::string` growth (anything not using
// `new (std::nothrow)`) when the allocator can't satisfy the request. Exceptions are
// disabled in this build (-fno-exceptions), so without this handler installed, a failed
// allocation anywhere in the app - not just the "big ticket" spots already guarded by
// MemoryBudget checks (image decoders, inline EPUB images) - silently aborts and resets
// the device with no diagnostic at all. This doesn't recover the allocation (there's
// nothing safe to free from here), but it gets the heap state into the serial log before
// the inevitable restart, so a low-memory crash is diagnosable from one log capture
// instead of hours of static code review.
void outOfMemoryHandler() {
  const auto heap = MemoryBudget::snapshot();
  char breadcrumb[48];
  snprintf(breadcrumb, sizeof(breadcrumb), "OOM free=%u max=%u", heap.freeHeap, heap.maxAllocHeap);
  HalSystem::recordBreadcrumb(breadcrumb);
  HalSystem::markDiagnosticReboot();
  LOG_ERR("MEM", "operator new failed - out of memory (free=%u maxAlloc=%u) - restarting", heap.freeHeap,
          heap.maxAllocHeap);
#ifdef ENABLE_SERIAL_LOG
  logSerial.flush();
#endif
  delay(50);
  esp_restart();
}

// Picks and opens the first activity of a boot. A function (not inline in setup()) because the PIN lock runs it later,
// from the unlock screen, with the same inputs.
static void routeStartTarget(const BootResume resume, const uint32_t snapshotTarget, const bool recoveryFirmwareMode) {
  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             (!BoardConfig::isX4Pro() && mappedInputManager.isPressed(MappedInputManager::Button::Back)) ||
             APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }
}

void setup() {
#ifndef SIMULATOR
  // First thing: count crash-resets of this image and roll back after repeated ones.
  boot_health::begin();
#endif
  // X4 Pro: keep the board master power rail asserted before USB/Serial init.
  // This is the same first-step policy used by CrossPoint 1.6.0.
  BoardConfig::holdPowerRails();

  t1 = millis();
  std::set_new_handler(outOfMemoryHandler);

  const esp_reset_reason_t rawResetReason = esp_reset_reason();
  const esp_sleep_wakeup_cause_t rawWakeupCause = esp_sleep_get_wakeup_cause();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  delay(250);
  Serial.begin(115200);
#ifndef SIMULATOR
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  LOG_INF("BOOT", "Reset diagnostic: reset=%d(%s) sleepWake=%d(%s)", static_cast<int>(rawResetReason),
          resetReasonName(rawResetReason), static_cast<int>(rawWakeupCause), wakeupCauseName(rawWakeupCause));

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_MAX) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();
  // Restore the monotonic clock floor (loan-expiry enforcement) before anything reads time().
  trustedtime::init();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");
  LOG_INF("BOOT", "Post-GPIO diagnostic: device=%s usb=%d silentReboot=%d silentTarget=%lu",
          gpio.deviceIsX3() ? "X3" : "X4", gpio.isUsbConnected() ? 1 : 0, isSilentReboot ? 1 : 0,
          static_cast<unsigned long>(snapshotTarget));

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  // A forgotten PIN is reset by /inkoptimum-reset-pin.txt on the card (before anything asks for the PIN).
  PinGate::consumeResetFile();

  // Hyphenation packs for languages that are not built in (SD: /.inkmod/hyphenation/).
  SdHyphenationPack::install();

  // Headless emergency recovery: root-of-SD /inkmod-recovery.bin is validated
  // and flashed before display/UI initialization.
  tryEmergencyFirmwareUpdate();

  BootLog::begin();
  BootLog::stepf("MAIN", "SD mounted; device=%s usb=%d silentReboot=%d", gpio.deviceIsX3() ? "X3" : "X4",
                  gpio.isUsbConnected() ? 1 : 0, isSilentReboot ? 1 : 0);

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  // Clock power saving: a timer wake is always the minute tick of the Clock app's standby (nothing else arms a timer).
  if (rawResetReason == ESP_RST_DEEPSLEEP && rawWakeupCause == ESP_SLEEP_WAKEUP_TIMER && SETTINGS.clockPowerSaving) {
    if (ClockStandby::onTimerWake()) return;  // not reached: it sleeps again
    LOG_ERR("CLK", "standby tick failed, continuing with a normal boot");
  }
  bleturner::begin(pageturner::host, SETTINGS.ble);

  // Wi-Fi transmit power (Settings > System): applied each time the station interface starts, which is the
  // earliest point the radio accepts it. 0 keeps the driver default.
  WiFi.onEvent(
      [](arduino_event_id_t, arduino_event_info_t) {
        static const wifi_power_t kLevels[] = {WIFI_POWER_19_5dBm, WIFI_POWER_15dBm, WIFI_POWER_11dBm,
                                               WIFI_POWER_8_5dBm, WIFI_POWER_5dBm};
        if (SETTINGS.wifiTxPower > 0 && SETTINGS.wifiTxPower < sizeof(kLevels) / sizeof(kLevels[0])) {
          WiFi.setTxPower(kLevels[SETTINGS.wifiTxPower]);
        }
      },
      ARDUINO_EVENT_WIFI_STA_START);

  // clockDisabled is an X4-only preference.  Never let a stale/corrupt value
  // suppress the DS3231 clock or its settings on X3 after an upgrade.
  if (gpio.deviceIsX3() && SETTINGS.clockDisabled) {
    SETTINGS.clockDisabled = 0;
    SETTINGS.saveToFile();
    LOG_INF("CLK", "Cleared X4-only clockDisabled flag on X3");
  }

  // Resolve the wake route before restoring the X4 Pro frontlight. A short
  // power tap while the reader is asleep wakes the S3 long enough for
  // verifyPowerButtonWakeup() to reject it and put the device back to sleep.
  // Restoring the saved light state before that verification made the frontlight
  // visibly flash for a few seconds on an otherwise rejected wake. Keep the light
  // dark until the power-button hold has been accepted; ordinary/cold boots keep
  // the previous restore-on-wake behaviour.
  const auto wakeupReason = gpio.getWakeupReason();
  const bool deferFrontlightRestore =
      BoardConfig::isX4Pro() && wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool restoreFrontlight =
      SETTINGS.frontlightOn != 0 && SETTINGS.frontlightRestoreOnWake != 0;
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth,
                   restoreFrontlight && !deferFrontlightRestore);
  APP_STATE.loadFromFile();
  powerManager.seedLastChargeEpochSeconds(APP_STATE.lastChargeEpochSeconds);

#if FREEINK_DEVICE_X4PRO
  // X4 Pro has no validated dedicated VBUS-detect GPIO. A native USB/JTAG reset
  // is nevertheless definitive evidence that the device was just on USB, so
  // treat it as a charging session. USB-MSC also calls markChargingNow() before
  // its software restart, and that RTC_DATA_ATTR timestamp lands here.
  if (rawResetReason == ESP_RST_USB || rawResetReason == ESP_RST_JTAG) {
    powerManager.markChargingNow();
  }
#endif

  // If the runtime/RTC copy is newer than the SD copy (notably after leaving
  // X4 Pro USB-MSC), persist it now instead of waiting for the 5-minute fallback.
  const uint64_t bootChargeEpoch = powerManager.getLastChargeEpochSeconds();
  if (bootChargeEpoch > APP_STATE.lastChargeEpochSeconds) {
    APP_STATE.lastChargeEpochSeconds = bootChargeEpoch;
    APP_STATE.saveToFile();
  }

  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  if (SETTINGS.languagePack[0] != '\0' && !LanguagePackLoader::load(SETTINGS.languagePack)) {
    SETTINGS.languagePack[0] = '\0';  // pack removed or built for another firmware: back to the built-in language
  }
  // A card that already holds plugins keeps them working after the Plugins gate was introduced (it defaults
  // to off); the check runs once, so switching the setting off afterwards sticks.
  if (!SETTINGS.pluginsMigrated) {
    if (!SETTINGS.pluginsEnabled && PluginLocations::anyPluginInstalledOnDisk()) SETTINGS.pluginsEnabled = 1;
    SETTINGS.pluginsMigrated = 1;
    SETTINGS.saveToFile();
  }
  PluginEventBus::init();
  if (snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_SEND || snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_SEND_TIMEOUT) {
    // Clean boot: no display, fonts or framebuffers yet, so Wi-Fi and TLS have the heap. Send, then on into the sleep.
    PluginEventBus::sendFromCleanBoot();
    SleepSync::run();
    silentRebootTarget = snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_SEND_TIMEOUT ? SILENT_REBOOT_TARGET_SLEEP_TIMEOUT
                                                                                    : SILENT_REBOOT_TARGET_SLEEP;
    silentRebootMagic = SILENT_REBOOT_MAGIC;
    LOG_INF("PEVT", "events sent from a clean boot; on into the sleep");
    delay(50);
    ESP.restart();
    return;
  }
  PluginEventBus::setToastSink([](const char* text) {
    GUI.drawPopup(renderer, text);
    renderer.displayBuffer();
  });
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  WEATHER.loadSettings();
  // SD-only check (see comment on the function): must happen before the display/SPI
  // bus is initialized below. The actual WiFi/NTP step runs later, behind the splash.
  const BootTimeSyncCandidate bootTimeSyncCandidate = checkSilentBootTimeSyncCandidate();
  seedClockFromLastKnownTime();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // Check wake duration before the remaining file loads so the user does not
  // have to hold the power button across all of the SD reads below.
  LOG_INF("BOOT", "Wake route: %s", wakeupRouteName(wakeupReason));
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton: {
      const bool shortPowerWakeAllowed = SETTINGS.shortPowerWakeAllowed(BoardConfig::isX4Pro());
      LOG_INF("BOOT", "Power-button wake: verifying duration required=%u shortAllowed=%d",
              SETTINGS.getPowerButtonWakeDuration(), shortPowerWakeAllowed);
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonWakeDuration(), shortPowerWakeAllowed);
      break;
    }
    case HalGPIO::WakeupReason::AfterUSBPower:
      // A cold boot caused only by external USB power should not leave the
      // reader fully awake. Render the configured sleep screen and return to
      // deep sleep; the power button remains the explicit wake source.
#ifndef SIMULATOR
      // The first start of a freshly flashed (or never confirmed) image often
      // looks like a plain power-on with USB attached. Sleeping then would
      // make a new install appear dead, so boot normally this once.
      if (boot_health::unconfirmedBoot()) {
        LOG_INF("BOOT", "AfterUSBPower route: unconfirmed image, continuing boot");
        break;
      }
#endif
      LOG_INF("BOOT", "AfterUSBPower route: returning to deep sleep");
      enterDeepSleep(false);
      return;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
      LOG_INF("BOOT", "AfterFlash route: continuing boot");
      break;
    case HalGPIO::WakeupReason::Other:
    default:
      LOG_INF("BOOT", "Other wake route: continuing boot");
      break;
  }

  // A PowerButton wake reaches this point only if verifyPowerButtonWakeup()
  // accepted the hold. Restore the saved frontlight state now, after the rejected
  // short-tap path has already gone back to deep sleep. X3/X4 behaviour is
  // intentionally unchanged.
  if (deferFrontlightRestore && restoreFrontlight) {
    Frontlight.setOn(true);
  }

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
#ifndef SIMULATOR
  // Repeated crashes with no verified previous reader image: go to SD recovery.
  bool recoveryFirmwareMode = boot_health::recoveryRequired();
#else
  bool recoveryFirmwareMode = false;
#endif
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting inkMOD version " INKMOD_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  const BootResume resume = isSilentReboot              ? BootResume::Silent
                            : (!APP_STATE.showBootScreen && !PinGate::enabled())  // quick resume would show the last page
                                ? BootResume::QuickResume
                                                        : BootResume::Splash;

  if (snapshotTarget == SILENT_REBOOT_TARGET_SLEEP || snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_TIMEOUT) {
    // The sleep a clean-boot send restarted into: the panel still shows the sleep screen painted before the restarts.
    LOG_INF("MAIN", "Finishing the sleep after a clean-boot event send");
    setupDisplayAndFonts(true);
    sleepEventsSentPass = true;
    enterDeepSleep(snapshotTarget == SILENT_REBOOT_TARGET_SLEEP_TIMEOUT);
    return;  // not reached: enterDeepSleep() never returns
  }

  BootLog::step("MAIN", "entering setupDisplayAndFonts() from setup()");
  setupDisplayAndFonts(resume != BootResume::Splash);
  BootLog::step("MAIN", "back from setupDisplayAndFonts() in setup() - display/fonts OK");

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::QuickResume:
      // One-shot flag: re-arm the splash for the next non-quick-resume boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a quick-resume-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (loadSleepFrameBuffer()) {
        // Restore the pre-sleep reader framebuffer to the panel, but do not
        // draw any Quick Resume marker.  The refresh is required so the sleep
        // wallpaper is replaced immediately; omitting it leaves the sleep
        // image visible until a later unrelated redraw.
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        activityManager.goToBoot();  // frame file missing, fall back to the splash
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
  }

  // Placed after the splash/quick-resume screen is already painted, so a cold boot's
  // brief silent WiFi dip (only happens when there's no valid time yet - see the
  // function) happens behind a visible screen instead of a black/frozen one.
#if FREEINK_DEVICE_X4PRO
  // CrossPoint X4 Pro boots directly from the splash into the routed activity.
  // Keep inkMOD's cold-boot NTP helper off the Pro path until the board is fully
  // up; touching WiFi here can stall ESP32-S3 before Home is painted.
  BootLog::step("WIFI", "X4 Pro: skipping silent boot-time WiFi/NTP sync");
#else
  attemptSilentBootTimeSync(bootTimeSyncCandidate);
#endif

  // Pre-warm WiFi into a stable STA/disconnected state here, in the quiet
  // early-boot window, rather than lazily inside
  // WifiSelectionActivity::attemptConnection() when the user actually
  // triggers a connection. Several boot-log captures now confirm
  // WiFi.mode(WIFI_STA)/WiFi.persistent(false) can hang the whole device -
  // even an independent esp_timer watchdog, armed and confirmed running,
  // never fires when it happens, which points to something disabling
  // interrupts/the scheduler inside the call itself rather than just one
  // task getting stuck. Every one of those hangs happened during
  // interactive use (button held, render task active, hours into a
  // session); the identical calls have never once hung here, run fresh
  // during boot, across dozens of captures. Doing it once now means
  // attemptConnection() later finds WiFi.getMode() already == WIFI_STA and
  // skips this exact call pair entirely (see its own skip-if-already-STA
  // check) - trading a small amount of extra idle-radio power draw for the
  // rest of the session against not touching this call again outside of
  // boot's quiet window.
#if FREEINK_DEVICE_X4PRO
  // CrossPoint 1.6.0 does not run inkMOD's boot-time WiFi pre-warm on X4 Pro.
  // Route to Home/Reader first; WiFi is still available normally when an
  // activity explicitly requests it later.
  BootLog::step("WIFI", "X4 Pro: skipping boot WiFi pre-warm");
#else
  if (WiFi.getMode() != WIFI_STA) {
    BootLog::step("WIFI", "boot pre-warm: WiFi.mode(WIFI_OFF) -> persistent(false) -> mode(WIFI_STA) -> disconnect(true)");
    WiFi.mode(WIFI_OFF);
    delay(100);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    BootLog::step("WIFI", "boot pre-warm: done");
  }
#endif

  // PIN lock: asked for on every cold boot / wake from sleep, not for recovery, panic reports or silent in-session
  // restarts. The unlock screen then opens the same target the routing below would have.
  if (resume != BootResume::Silent && !recoveryFirmwareMode && !HalSystem::isRebootFromPanic() && PinGate::enabled()) {
    activityManager.replaceActivity(std::make_unique<PinEntryActivity>(
        renderer, mappedInputManager, tr(STR_PIN_ENTER), [](const std::string& pin) { return PinGate::attempt(pin); },
        [resume, snapshotTarget, recoveryFirmwareMode](const std::string&) {
          routeStartTarget(resume, snapshotTarget, recoveryFirmwareMode);
        },
        /*cancellable=*/false, /*finishOnDone=*/false));
  } else {
    routeStartTarget(resume, snapshotTarget, recoveryFirmwareMode);
  }

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
  allowSleepAt = millis() + 2000;
}

namespace {
// Only written and consumed by the main Arduino loop task; no mutex needed.
bool deferredHomeSleepPending = false;
bool deferredHomeSleepQuickResume = false;
}  // namespace

void requestDeferredHomeSleep(const bool quickResume) {
  deferredHomeSleepQuickResume = quickResume;
  deferredHomeSleepPending = true;
}

bool consumeDeferredHomeSleep(bool& quickResume) {
  if (!deferredHomeSleepPending) return false;
  quickResume = deferredHomeSleepQuickResume;
  deferredHomeSleepPending = false;
  return true;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  static unsigned long lastMemPrint = 0;
#endif

  mappedInputManager.update();  // gpio.update() plus the one-frame remote click

  // Bluetooth page turner: tell the module what is on screen; a remote press that acted counts as activity.
  bleturner::Scene scene{};
  const bool homeShown = activityManager.isForegroundHomeShown();
  const bool remoteContext = activityManager.isRemoteReaderContext();
  scene.where = remoteContext ? bleturner::Where::Reader
                : homeShown   ? bleturner::Where::Home
                              : bleturner::Where::Elsewhere;
  scene.visit = activityManager.visit();
  scene.pageShown = homeShown || remoteContext;
  scene.storageBusy = false;
  scene.wifiOn = WiFi.getMode() != WIFI_MODE_NULL;
  scene.sleeping = deepSleepInProgress;
  scene.localKey = gpio.wasAnyReleased();
  const bool remoteActed = bleturner::tick(scene);

  // Keep battery telemetry fresh from the main task. X4 Pro's CW2017 shares
  // I2C with touch/RTC, so reader rendering must consume a cached value rather
  // than starting an overlapping Wire transaction from the render task.
  powerManager.pollBatteryPercentage();

  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.tiltPageTurnDirection, SETTINGS.orientation,
                       activityManager.isReaderActivity());

  // X3 only: the BQ27220 loses its RAM-backed DesignCapacity after gauge power
  // loss and returns to TI's 3000 mAh default. Correct it to the real 650 mAh
  // asynchronously, one short I2C step per loop, while holding the render lock
  // so battery/status-bar reads cannot interleave on the shared bus.
  if (gpio.deviceIsX3() && powerManager.isGaugeCapacityLoadPending() && !RenderLock::peek()) {
    RenderLock lock;
    powerManager.loadGaugeCapacity();
  }

  renderer.setFadingFix(SETTINGS.fadingFix);

#if defined(ENABLE_SERIAL_LOG) && LOG_LEVEL >= 2
  // Periodic heap telemetry is useful for development, but release-level
  // logging must not wake USB/UART every ten seconds while the reader is idle.
  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }
#endif

  // Persist "since last charge" so it survives a sleep cycle or a
  // reflash/reset, not just staying valid within one awake session. Two
  // triggers, for two different reasons:
  //
  // - Immediately, the moment charging stops (USB was connected, now
  //   isn't): on battery, this device's deep sleep fully cuts power to
  //   the MCU - RTC memory included (see HalPowerManager::startDeepSleep()'s
  //   own comment) - and there's no USB-connect wake source configured
  //   either, so the very next sleep (which can happen within seconds of
  //   unplugging) would otherwise wipe the in-RAM value before it's ever
  //   written anywhere durable. This is a rare event (once per unplug),
  //   so writing to SD right when it happens isn't a wear concern.
  // - Every 5 minutes as a fallback, for the rarer case of a reflash/reset
  //   happening in the middle of an still-ongoing charge session, before
  //   any "stopped charging" transition has occurred yet.
  {
    static bool wasUsbConnected = false;
    // This updates only a timestamp from HalGPIO's cached charge state and is
    // internally limited to once per BATTERY_POLL_MS. Do not call
    // getBatteryPercentage() here: on X4 that would start an ADC conversion on
    // every main-loop pass, even while the screen is completely idle.
    powerManager.trackChargingState();

    const bool isUsbConnected = gpio.isUsbConnectedCached();
    const bool chargingJustStopped = wasUsbConnected && !isUsbConnected;
    wasUsbConnected = isUsbConnected;

    static unsigned long lastChargePersistCheck = 0;
    constexpr unsigned long kChargePersistIntervalMs = 5 * 60 * 1000;
    const bool periodicCheckDue = millis() - lastChargePersistCheck >= kChargePersistIntervalMs;

    if (chargingJustStopped || periodicCheckDue) {
      lastChargePersistCheck = millis();
      const uint64_t currentLastCharge = powerManager.getLastChargeEpochSeconds();
      if (currentLastCharge != APP_STATE.lastChargeEpochSeconds) {
        APP_STATE.lastChargeEpochSeconds = currentLastCharge;
        APP_STATE.saveToFile();
      }
    }
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  // Only what has arrived is read; a line still on its way is finished on a later pass, so a stray
  // byte (a monitor attaching to the port) no longer stops the loop for the 1 s Stream timeout
  // (SerialLineBuffer says why). One line per pass.
  static SerialLineBuffer serialLine;
  bool serialLineComplete = false;
  while (!serialLineComplete && logSerial.available() > 0) {
    serialLineComplete = serialLine.feed(static_cast<char>(logSerial.read()));
  }
  if (serialLineComplete) {
    const String line(serialLine.take().c_str());
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep() || remoteActed || pageturner::holdsCpu.load()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      screenshotComboHandled = true;
      mappedInputManager.suppressNextPowerConfirmRelease();
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

#ifdef SIMULATOR
  if (gpio.consumeSimulatorSleepRequest()) {
    enterDeepSleep();
    lastActivityTime = millis();
    return;
  }
#endif

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    // In the simulator, deep sleep is a no-op and returns — reset the timer so
    // the main loop does not immediately re-trigger auto-sleep.
    lastActivityTime = millis();
    return;
  }

  if (millis() >= allowSleepAt && handleGlobalShortcutAction(getPowerButtonAction())) {
    lastActivityTime = millis();
    return;
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    // A charger plug/unplug is an asynchronous power-domain transition.
    // Restore the normal CPU clock before rendering the new battery icon.
    // X4 can otherwise start a full E-Ink refresh while downclocked at 10 MHz.
    powerManager.setPowerSaving(false);
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  // Home-key sleep must run after ActivityManager's event dispatch and pending
  // activity changes have completed, not from a recursively entered loop().
  bool quickHomeSleep = false;
  if (consumeDeferredHomeSleep(quickHomeSleep)) {
    enterDeepSleep(quickHomeSleep);
    return;
  }
  const unsigned long activityDuration = millis() - activityStartTime;
  // Time blocked inside a screen (a synchronous download, say) is not idle time (crosspoint #4538); ordinary short
  // passes are not touched.
  if (activityDuration >= 1000) lastActivityTime += activityDuration;

#ifdef SIMULATOR
  runSimulatorSmokeTestTick();
#endif

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
      (void)activityDuration;
    }
  }

  // Safe idle power saving for X3/X4:
  //   < 500 ms idle : normal CPU, 10 ms loop delay
  //   >=500 ms idle : 10 MHz CPU + ordinary 50 ms delay
  //
  // IMPORTANT: no esp_light_sleep_start() is used here. On X4, entering
  // timer light-sleep immediately after USB disconnect can leave the device
  // stuck depending on the USB/power/GPIO state. Downclocking still provides
  // a large part of the idle saving without that risk.
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);
    yield();
  } else {
    const unsigned long idleMs = millis() - lastActivityTime;
    // Keep X4 at its normal clock while USB power is present. Power saving
    // provides no battery benefit on a charger and clock transitions during
    // the USB/charging path are unnecessary (issue #60).
    const bool usbPoweredX4 = gpio.deviceIsX4() && gpio.isUsbConnectedCached();
    if (idleMs >= HalPowerManager::IDLE_POWER_SAVING_MS && !usbPoweredX4) {
      powerManager.setPowerSaving(true);
      delay(50);
    } else {
      powerManager.setPowerSaving(false);
      delay(usbPoweredX4 ? 50 : 10);
    }
  }
}
