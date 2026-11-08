#include "BleSettingsActivity.h"

#include <BlePageTurner.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <cstring>
#include <iterator>

#include "I18nKeys.h"
#include "InkMODSettings.h"
#include "MappedInputManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "util/TouchListNavigation.h"

namespace {
enum MenuAction : int16_t { ActionEnable, ActionScan, ActionPaired, ActionMapping };
constexpr bleturner::Action MAP_ACTIONS[] = {bleturner::Action::NextPage,    bleturner::Action::PrevPage,
                                             bleturner::Action::NextChapter, bleturner::Action::PrevChapter,
                                             bleturner::Action::Confirm,     bleturner::Action::Back,
                                             bleturner::Action::ReaderMenu,  bleturner::Action::SaveQuote};
constexpr StrId MAP_LABELS[] = {StrId::STR_NEXT_PAGE,       StrId::STR_PREV_PAGE,    StrId::STR_BT_NEXT_CHAPTER,
                                StrId::STR_BT_PREV_CHAPTER, StrId::STR_CONFIRM,      StrId::STR_BACK,
                                StrId::STR_MENU,            StrId::STR_CREATE_CLIPPING};
static_assert(std::size(MAP_ACTIONS) == std::size(MAP_LABELS));
constexpr uint32_t SCAN_MS = 15000;
constexpr unsigned long CLEAR_HOLD_MS = 700;
}  // namespace

void BleSettingsActivity::onEnter() {
  Activity::onEnter();
  // A saved opt-in brings the radio up here, so scanning and pairing work at once.
  if (SETTINGS.ble.enabled) bleturner::switchOn();
  selected = 0;
  refresh();
  requestUpdate();
}

void BleSettingsActivity::onExit() {
  if (view == View::Scan) bleturner::scan(0);
  Activity::onExit();
}

const char* BleSettingsActivity::headerTitle() const {
  switch (view) {
    case View::Menu:
      return tr(STR_BT_PAGE_TURNER);
    case View::Scan:
      return tr(STR_BT_SCAN);
    case View::Paired:
      return tr(STR_BT_PAIRED_REMOTES);
    case View::Mapping:
      return tr(STR_BT_MAP_BUTTONS);
  }
  return tr(STR_BT_PAGE_TURNER);
}

void BleSettingsActivity::showView(const View target) {
  {
    RenderLock lock(*this);
    view = target;
    learnAction = bleturner::Action::None;
    learnNote = nullptr;
    selected = 0;
  }
  refresh();
  requestUpdate();
}

uint32_t BleSettingsActivity::signature() const {
  const auto st = bleturner::status();
  return (st.running ? 1u : 0u) | (st.scanning ? 2u : 0u) | (st.connected ? 4u : 0u) | (st.connecting ? 8u : 0u) |
         (st.stopping ? 16u : 0u) | (SETTINGS.ble.enabled ? 32u : 0u) |
         static_cast<uint32_t>(bleturner::bondCount()) << 8 | static_cast<uint32_t>(bleturner::foundCount()) << 16;
}

void BleSettingsActivity::refresh() {
  RenderLock lock(*this);
  shownSignature = signature();
  const auto st = bleturner::status();
  char failure[48];
  if (!SETTINGS.ble.enabled || st.stopping) {
    status = tr(STR_STATE_OFF);
  } else if (bleturner::takeConnectFailure(failure, sizeof(failure))) {
    status = failure;
  } else if (!st.running) {
    status = tr(STR_BT_START_FAILED);
  } else if (st.connected) {
    status = bleturner::linked().name;
  } else if (st.connecting) {
    status = tr(STR_CONNECTING);
  } else if (st.scanning) {
    status = tr(STR_SCANNING);
  } else {
    status = tr(STR_STATE_ON);
  }

  const uint8_t found = st.running ? bleturner::foundCount() : 0;
  const uint8_t bonds = st.running ? bleturner::bondCount() : 0;
  rows.clear();
  const auto add = [this](const std::string& label, const int16_t code, const bool enabled = true) -> Row& {
    Row row;
    row.label = label;
    row.code = code;
    row.enabled = enabled;
    rows.push_back(row);
    return rows.back();
  };
  switch (view) {
    case View::Menu: {
      auto& enable = add(tr(STR_BT_PAGE_TURNER), ActionEnable);
      enable.value = status;
      add(tr(STR_BT_SCAN), ActionScan, st.running);
      add(tr(STR_BT_PAIRED_REMOTES), ActionPaired, st.running);
      auto& mapping = add(tr(STR_BT_MAP_BUTTONS), ActionMapping, st.connected);
      if (!st.connected) mapping.subtitle = tr(STR_BT_CONNECT_REMOTE);
      break;
    }
    case View::Scan:
      add(st.scanning ? tr(STR_BT_STOP_SCAN) : tr(STR_BT_SCAN), -1, st.running);
      for (uint8_t i = 0; i < found; ++i) add(bleturner::found(i).name, i);
      if (found == 0) add(st.scanning ? tr(STR_SCANNING) : tr(STR_BT_NO_DEVICES), -1, false);
      break;
    case View::Paired:
      for (uint8_t i = 0; i < bonds; ++i) {
        const auto peer = bleturner::bond(i);
        auto& row = add(peer.name[0] != '\0' ? peer.name : peer.addr, i);
        row.value = tr(STR_FORGET_BUTTON);
      }
      if (bonds == 0) add(tr(STR_BT_NO_REMOTES), -1, false);
      break;
    case View::Mapping: {
      const auto peer = bleturner::linked();
      const auto* table = bleturner::tableFor(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
      for (uint8_t i = 0; i < std::size(MAP_ACTIONS); ++i) {
        auto& row = add(I18N.get(MAP_LABELS[i]), i, st.connected && learnAction == bleturner::Action::None);
        bleturner::Binding binding;
        if (table && bleturner::find(*table, MAP_ACTIONS[i], binding)) {
          char code[24];
          bleturner::formatCode(code, sizeof(code), binding);
          row.value = code;
          if (binding & bleturner::kHoldBit) row.value += std::string(" ") + tr(STR_BT_HOLD);
        } else {
          row.value = tr(STR_NOT_SET);
        }
        if (!st.connected)
          row.subtitle = tr(STR_BT_CONNECT_REMOTE);
        else if (MAP_ACTIONS[i] == learnAction)
          row.subtitle = tr(STR_BT_PRESS_REMOTE);
        else if (i == 0 && learnNote)
          row.subtitle = learnNote;
      }
      break;
    }
  }
  if (selected >= static_cast<int>(rows.size())) selected = static_cast<int>(rows.size()) - 1;
  if (selected < 0) selected = 0;
}

void BleSettingsActivity::loop() {
  const int count = static_cast<int>(rows.size());
  bool touchActivate = false;
  if (mappedInput.hasTouch() && count > 0) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    const auto safeArea = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing * 2;
    int touchIndex = selected;
    auto touch = TouchListNavigation::handle(mappedInput, touchIndex, count,
                                             Rect{safeArea.x, contentTop, safeArea.width, contentHeight},
                                             metrics.listRowHeight);
    selected = touchIndex;
    if (touch.handled && !touch.activate) {
      requestUpdate();
      return;
    }
    touchActivate = touch.activate;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (learnAction != bleturner::Action::None) {
      learnAction = bleturner::Action::None;
      learnNote = nullptr;
      refresh();
      requestUpdate();
      return;
    }
    if (view == View::Menu) {
      finish();
      return;
    }
    if (view == View::Scan) bleturner::scan(0);
    showView(View::Menu);
    return;
  }

  if (touchActivate || mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Holding Confirm on a mapped action clears it.
    if (!touchActivate && view == View::Mapping && mappedInput.getHeldTime() >= CLEAR_HOLD_MS) {
      clearMapping(selected);
    } else {
      activateIndex(selected);
    }
    return;
  }

  if (count > 0) {
    buttonNavigator.onNext([this, count] {
      selected = (selected + 1) % count;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, count] {
      selected = (selected + count - 1) % count;
      requestUpdate();
    });
  }

  if (millis() - lastPollMs < 250) return;
  lastPollMs = millis();
  // A remote being paired that links becomes the chosen one: save it.
  if (SETTINGS.ble.enabled && bleturner::service()) SETTINGS.saveToFile();
  if (learnAction != bleturner::Action::None) stepLearning();
  if (signature() == shownSignature) return;  // an e-ink repaint only when something changed
  refresh();
  requestUpdate();
}

void BleSettingsActivity::activateIndex(const int index) {
  if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[index].enabled) return;
  selected = index;
  const int16_t code = rows[index].code;
  switch (view) {
    case View::Menu:
      switch (code) {
        case ActionEnable:
          SETTINGS.ble.enabled = SETTINGS.ble.enabled ? 0 : 1;
          if (SETTINGS.ble.enabled)
            bleturner::switchOn();
          else
            bleturner::switchOff();
          SETTINGS.saveToFile();
          break;
        case ActionScan:
          bleturner::scan(SCAN_MS);
          showView(View::Scan);
          return;
        case ActionPaired:
          showView(View::Paired);
          return;
        case ActionMapping:
          showView(View::Mapping);
          return;
      }
      break;
    case View::Scan:
      if (code < 0) {
        bleturner::scan(bleturner::status().scanning ? 0 : SCAN_MS);
      } else {
        const auto peer = bleturner::found(static_cast<uint8_t>(code));
        const std::string addr = peer.addr;
        const std::string name = peer.name;
        bleturner::scan(0);
        bleturner::pair(addr.c_str(), name.c_str());
        showView(View::Menu);
        return;
      }
      break;
    case View::Paired: {
      const std::string addr = bleturner::bond(static_cast<uint8_t>(code)).addr;
      if (bleturner::forget(addr.c_str())) SETTINGS.saveToFile();
      break;
    }
    case View::Mapping:
      startLearning(code);
      break;
  }
  refresh();
  requestUpdate();
}

void BleSettingsActivity::startLearning(const int index) {
  if (index < 0 || index >= static_cast<int>(std::size(MAP_ACTIONS)) || !bleturner::status().connected) return;
  bleturner::Event ev;
  while (bleturner::pollEvent(ev)) {
  }  // presses from before the row was chosen are not the answer
  learnAction = MAP_ACTIONS[index];
  const auto peer = bleturner::linked();
  strncpy(learnAddr, peer.addr, sizeof(learnAddr) - 1);
  learnCode = 0;
  learnSinceMs = millis();
  learnNote = nullptr;
}

void BleSettingsActivity::stepLearning() {
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    refresh();
    requestUpdate();
    return;
  }
  const auto action = learnAction;
  bleturner::Event ev;
  while (learnAction == action && bleturner::pollEvent(ev)) {
    if (ev.pressed) {
      learnCode = ev.code;
      learnPressMs = ev.atMs;
      learnSinceMs = millis();
    } else if (learnCode != 0 && ev.code == learnCode) {
      if (ev.wasRest) {
        learnCode = 0;  // the remote idling on a non-zero byte, not a button
      } else {
        // A button held past the reader's hold threshold is mapped as a hold, apart from its tap.
        keepLearned(ev.atMs - learnPressMs >= bleturner::kHoldMs);
      }
    }
  }
  if (learnAction == action) {
    // Allow the full learning window for a hold before falling back to a no-release tap.
    if (learnCode != 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      keepLearned(false);
    } else if (learnCode == 0 && millis() - learnSinceMs >= bleturner::kWaitMs) {
      learnAction = bleturner::Action::None;
      learnNote = tr(STR_BT_NO_KEY);
    }
  }
  if (learnAction != action) {
    refresh();
    requestUpdate();
  }
}

void BleSettingsActivity::keepLearned(const bool hold) {
  // Preserve the remote's other bindings when changing the selected action.
  const auto peer = bleturner::linked();
  if (!bleturner::status().connected || strcmp(peer.addr, learnAddr) != 0) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_CONNECT_REMOTE);
    return;
  }
  bleturner::RemoteTable* table =
      bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
  if (table == nullptr || !bleturner::learn(*table, learnAction, learnCode, hold)) {
    learnAction = bleturner::Action::None;
    learnNote = tr(STR_BT_KEYS_NOT_SAVED);
    return;
  }
  SETTINGS.saveToFile();
  learnCode = 0;
  learnSinceMs = millis();
  learnAction = bleturner::Action::None;
  learnNote = tr(STR_BT_MAPPING_SAVED);
}

void BleSettingsActivity::clearMapping(const int index) {
  if (index < 0 || index >= static_cast<int>(std::size(MAP_ACTIONS)) || learnAction != bleturner::Action::None) return;
  const auto action = MAP_ACTIONS[index];
  // The linked remote's slot for this action goes; a built-in default is copied out first, so it
  // does not come back. The page rows also drop the usage code learned before per-remote tables.
  if (bleturner::status().connected) {
    const auto peer = bleturner::linked();
    auto* table = bleturner::editableTable(SETTINGS.ble.remotes, SETTINGS.ble.remoteCount, peer.addr, peer.name);
    if (table != nullptr) bleturner::clearAction(*table, action);
  }
  if (action == bleturner::Action::NextPage) SETTINGS.ble.nextKeyUsage = bleturner::kUsageNone;
  if (action == bleturner::Action::PrevPage) SETTINGS.ble.prevKeyUsage = bleturner::kUsageNone;
  SETTINGS.saveToFile();
  learnNote = tr(STR_BT_MAPPING_CLEARED);
  refresh();
  requestUpdate();
}

void BleSettingsActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto safeArea =
      UITheme::getInstance().getScreenSafeArea(renderer, /*hasFrontButtonHints=*/true, /*hasSideButtonHints=*/false);

  GUI.drawHeader(renderer, Rect{safeArea.x, metrics.topPadding, safeArea.width, metrics.headerHeight}, headerTitle());

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = safeArea.y + safeArea.height - contentTop - metrics.verticalSpacing * 2;
  GUI.drawList(
      renderer, Rect{safeArea.x, contentTop, safeArea.width, contentHeight}, static_cast<int>(rows.size()), selected,
      [this](int index) { return rows[index].label; },
      [this](int index) { return rows[index].subtitle; }, nullptr,
      [this](int index) { return rows[index].value; }, true, [this](int index) { return !rows[index].enabled; });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
