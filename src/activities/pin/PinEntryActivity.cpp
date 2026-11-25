#include "PinEntryActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <PinLock.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

void PinEntryActivity::onEnter() {
  Activity::onEnter();
  entered_.clear();
  message_.clear();
  selected_ = 0;
  requestUpdate();
}

void PinEntryActivity::submit() {
  if (entered_.size() < pinlock::kMinDigits) {
    message_ = tr(STR_PIN_TOO_SHORT);
    requestUpdate();
    return;
  }
  const PinGate::Result r = check_ ? check_(entered_) : PinGate::Result{true, false, 0, 0};
  if (r.accepted) {
    const std::string pin = entered_;
    entered_.assign(entered_.size(), '0');  // do not leave the PIN lying in the freed buffer
    entered_.clear();
    const Done done = onDone_;
    const bool finishNow = finishOnDone_;
    if (finishNow) {
      ActivityResult res;
      res.data = KeyboardResult{pin};
      setResult(std::move(res));
    }
    if (done) done(pin);  // may replace (destroy) this activity when !finishOnDone_: touch nothing after
    if (finishNow) finish();
    return;
  }
  entered_.clear();
  if (r.locked) {
    message_ = std::string(tr(STR_PIN_LOCKED)) + " " + std::to_string(r.waitSeconds) + " s";
  } else {
    message_ = tr(STR_PIN_WRONG);
    if (r.triesLeft > 0) message_ += " (" + std::to_string(r.triesLeft) + ")";
  }
  requestUpdate();
}

void PinEntryActivity::activate() {
  if (selected_ <= 9) {
    if (entered_.size() < pinlock::kMaxDigits) entered_ += static_cast<char>('0' + selected_);
    message_.clear();
    requestUpdate();
  } else if (selected_ == 10) {
    if (!entered_.empty()) entered_.pop_back();
    message_.clear();
    requestUpdate();
  } else {
    submit();
  }
}

void PinEntryActivity::loop() {
  if (cancellable_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult cancelled;
    cancelled.isCancelled = true;
    setResult(std::move(cancelled));
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activate();
    return;
  }
  if (!cancellable_ && mappedInput.wasReleased(MappedInputManager::Button::Back)) {  // Back = delete a digit
    if (!entered_.empty()) {
      entered_.pop_back();
      requestUpdate();
    }
    return;
  }
  nav_.onNextRelease([this] {
    selected_ = (selected_ + 1) % kCells;
    requestUpdate();
  });
  nav_.onPreviousRelease([this] {
    selected_ = (selected_ + kCells - 1) % kCells;
    requestUpdate();
  });
  nav_.onNextContinuous([this] {
    selected_ = (selected_ + 1) % kCells;
    requestUpdate();
  });
  nav_.onPreviousContinuous([this] {
    selected_ = (selected_ + kCells - 1) % kCells;
    requestUpdate();
  });

  // While locked out, count the wait down on screen.
  const uint32_t wait = PinGate::lockedSeconds();
  if (wait != lastShownWait_) {
    lastShownWait_ = wait;
    if (wait > 0) {
      message_ = std::string(tr(STR_PIN_LOCKED)) + " " + std::to_string(wait) + " s";
    } else if (!message_.empty()) {
      message_.clear();
    }
    requestUpdate();
  }
}

void PinEntryActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int w = renderer.getScreenWidth();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, w, metrics.headerHeight}, title_.c_str());

  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  int y = metrics.topPadding + metrics.headerHeight + lh;
  std::string dots;
  for (size_t i = 0; i < entered_.size(); ++i) dots += "* ";
  if (dots.empty()) dots = "_";
  renderer.drawCenteredText(UI_12_FONT_ID, y, dots.c_str());
  y += lh * 2;
  if (!message_.empty()) renderer.drawCenteredText(UI_10_FONT_ID, y - lh, message_.c_str());

  // Two rows of six cells.
  const int cols = 6;
  const int cellW = (w - 2 * metrics.contentSidePadding) / cols;
  const int cellH = lh * 2;
  for (int i = 0; i < kCells; ++i) {
    const int cx = metrics.contentSidePadding + (i % cols) * cellW;
    const int cy = y + (i / cols) * (cellH + 8);
    const char* label;
    char digit[2] = {static_cast<char>('0' + i), '\0'};
    if (i <= 9) {
      label = digit;
    } else if (i == 10) {
      label = "<";
    } else {
      label = "OK";
    }
    const bool sel = i == selected_;
    if (sel) {
      renderer.fillRect(cx + 2, cy, cellW - 4, cellH, true);
    } else {
      renderer.drawRect(cx + 2, cy, cellW - 4, cellH, true);
    }
    const int tw = renderer.getTextWidth(UI_12_FONT_ID, label);
    renderer.drawText(UI_12_FONT_ID, cx + (cellW - tw) / 2, cy + (cellH - lh) / 2, label, !sel);
  }

  const auto labels = mappedInput.mapLabels(cancellable_ ? tr(STR_BACK) : "<", tr(STR_SELECT), tr(STR_DIR_UP),
                                            tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
