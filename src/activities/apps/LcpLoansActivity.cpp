#include "LcpLoansActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LcpInfo.h>
#include <WiFi.h>

#include <memory>

#include "LcpDeviceStore.h"
#include "LcpLoans.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/LcpClient.h"
#include "util/StringUtils.h"
#include "util/TouchListNavigation.h"

namespace {
enum Action { ACTION_RENEW = 0, ACTION_RETURN = 1, ACTION_COUNT = 2 };

std::string titleOf(const std::string& path) {
  const size_t slash = path.rfind('/');
  std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  const size_t dot = name.rfind('.');
  if (dot != std::string::npos && dot > 0) name.resize(dot);
  return name;
}

std::string dueLine(const lcpclient::LoanInfo& info) {
  if (!info.hasKey) return tr(STR_LCP_NEED_PASSPHRASE);
  switch (static_cast<lcpinfo::LoanState>(info.state)) {
    case lcpinfo::LoanState::NoExpiry:
      return tr(STR_LCP_NO_EXPIRY);
    case lcpinfo::LoanState::Active:
      return std::string(tr(STR_LCP_UNTIL)) + " " + lcpinfo::formatDate(info.expiresAt);
    case lcpinfo::LoanState::Expired:
      return std::string(tr(STR_LCP_EXPIRED)) + " (" + lcpinfo::formatDate(info.expiresAt) + ")";
    case lcpinfo::LoanState::ClockUnknown:
      return std::string(tr(STR_LCP_CLOCK_UNKNOWN)) + " (" + lcpinfo::formatDate(info.expiresAt) + ")";
  }
  return {};
}

const char* resultText(const lcpclient::Result r) {
  switch (r) {
    case lcpclient::Result::Ok:
      return "";
    case lcpclient::Result::WrongPassphrase:
      return tr(STR_LCP_WRONG_PASSPHRASE);
    case lcpclient::Result::Unsupported:
      return tr(STR_LCP_CANNOT_RETURN);
    case lcpclient::Result::OfficialBuildRequired:
      return tr(STR_LCP_OFFICIAL_BUILD);
    case lcpclient::Result::Offline:
      return tr(STR_LCP_OFFLINE);
    case lcpclient::Result::DeviceRejected:
      return tr(STR_LCP_DEVICE_REJECTED);
    case lcpclient::Result::ServiceError:
      return tr(STR_LCP_SERVICE_ERROR);
    case lcpclient::Result::NeedPassphrase:
      return tr(STR_LCP_NEED_PASSPHRASE);
    case lcpclient::Result::Failed:
      break;
  }
  return tr(STR_DOWNLOAD_FAILED);
}
}  // namespace

void LcpLoansActivity::loadLoans() {
  loans_.clear();
  for (const auto& path : lcploans::list()) {
    Loan loan;
    loan.path = path;
    loan.title = titleOf(path);
    loan.due = dueLine(lcpclient::loanInfo(path));
    loans_.push_back(std::move(loan));
  }
}

void LcpLoansActivity::onEnter() {
  Activity::onEnter();
  loadLoans();
  selected_ = 0;
  actionsOpen_ = false;
  returnArmed_ = false;
  statusLine_.clear();
  requestUpdate();
}

void LcpLoansActivity::onExit() {
  Activity::onExit();
  loans_.clear();
  loans_.shrink_to_fit();
}

void LcpLoansActivity::busy(const char* message) {
  busy_ = true;
  statusLine_ = message;
  requestUpdateAndWait();
}

void LcpLoansActivity::withNetwork(const std::function<void()>& job) {
  if (WiFi.status() == WL_CONNECTED) {
    job();
    return;
  }
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this, job](const ActivityResult& r) {
                           if (r.isCancelled) {
                             statusLine_ = tr(STR_WIFI_CONN_FAILED);
                             requestUpdate();
                             return;
                           }
                           job();
                         });
}

void LcpLoansActivity::runAction(const int action) {
  const int index = selected_ - 1;
  if (index < 0 || index >= static_cast<int>(loans_.size())) return;
  const std::string path = loans_[index].path;

  if (action == ACTION_RETURN && !returnArmed_) {
    returnArmed_ = true;  // returning cannot be undone: ask for a second press
    statusLine_ = tr(STR_LCP_RETURN_CONFIRM);
    requestUpdate();
    return;
  }
  returnArmed_ = false;

  withNetwork([this, action, path] {
    busy(tr(STR_LOADING));
    const lcpclient::Result r = action == ACTION_RETURN ? lcpclient::returnBook(path) : lcpclient::refreshKey(path);
    busy_ = false;
    statusLine_ = r == lcpclient::Result::Ok ? (action == ACTION_RETURN ? tr(STR_LCP_RETURNED) : tr(STR_LCP_RENEWED))
                                              : resultText(r);
    actionsOpen_ = false;
    const int keep = selected_;
    loadLoans();
    selected_ = std::min(keep, rowCount() - 1);
    requestUpdate();
  });
}

void LcpLoansActivity::activate() {
  if (selected_ == 0) {
    withNetwork([this] {
      busy(tr(STR_LOADING));
      const lcpclient::ServiceStatus s = lcpclient::checkService();
      busy_ = false;
      if (!s.reachable) {
        statusLine_ = tr(STR_LCP_OFFLINE);
      } else if (s.enrolled) {
        statusLine_ = std::string(tr(STR_LCP_SERVICE_OK)) + ", " + tr(STR_LCP_DEVICE_ENROLLED);
      } else if (s.canEnroll) {
        statusLine_ = std::string(tr(STR_LCP_SERVICE_OK)) + ", " + tr(STR_LCP_DEVICE_NOT_ENROLLED);
      } else {
        statusLine_ = std::string(tr(STR_LCP_SERVICE_OK)) + ". " + tr(STR_LCP_NO_RELEASE_KEY);
      }
      requestUpdate();
    });
    return;
  }
  actionsOpen_ = true;
  actionSelected_ = 0;
  returnArmed_ = false;
  statusLine_.clear();
  requestUpdate();
}

void LcpLoansActivity::loop() {
  if (busy_) return;

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (actionsOpen_) {
      actionsOpen_ = false;
      returnArmed_ = false;
      statusLine_.clear();
      requestUpdate();
      return;
    }
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (actionsOpen_) {
      runAction(actionSelected_);
    } else {
      activate();
    }
    return;
  }

  const int listSize = actionsOpen_ ? static_cast<int>(ACTION_COUNT) : rowCount();
  int& cursor = actionsOpen_ ? actionSelected_ : selected_;
  buttonNavigator_.onNextRelease([this, listSize, &cursor] {
    cursor = ButtonNavigator::nextIndex(cursor, listSize);
    returnArmed_ = false;
    statusLine_.clear();
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, listSize, &cursor] {
    cursor = ButtonNavigator::previousIndex(cursor, listSize);
    returnArmed_ = false;
    statusLine_.clear();
    requestUpdate();
  });
}

void LcpLoansActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_LCP_LOANS));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int statusH = renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing - statusH;

  if (actionsOpen_) {
    GUI.drawList(
        renderer, Rect{0, contentTop, width, contentHeight}, static_cast<int>(ACTION_COUNT), actionSelected_,
        [](int i) { return std::string(i == ACTION_RENEW ? tr(STR_LCP_RENEW) : tr(STR_LCP_RETURN)); },
        [this](int) { return loans_[selected_ - 1].title; }, nullptr, nullptr, true);
  } else if (loans_.empty()) {
    GUI.drawList(
        renderer, Rect{0, contentTop, width, contentHeight}, 1, 0, [](int) { return std::string(tr(STR_LCP_CHECK_SERVICE)); },
        [](int) { return std::string(tr(STR_LCP_NO_LOANS)); }, nullptr, nullptr, true);
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, width, contentHeight}, rowCount(), selected_,
        [this](int i) { return i == 0 ? std::string(tr(STR_LCP_CHECK_SERVICE)) : loans_[i - 1].title; },
        [this](int i) { return i == 0 ? std::string() : loans_[i - 1].due; }, nullptr, nullptr, true);
  }

  if (!statusLine_.empty()) {
    const int tw = renderer.getTextWidth(UI_10_FONT_ID, statusLine_.c_str());
    renderer.drawText(UI_10_FONT_ID, std::max(0, (width - tw) / 2),
                      renderer.getScreenHeight() - metrics.buttonHintsHeight - statusH, statusLine_.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
