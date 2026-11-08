#include "PluginReadmeActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t MAX_README_BYTES = 16 * 1024;
constexpr int SIDE_MARGIN = 12;
}  // namespace

void PluginReadmeActivity::onEnter() {
  Activity::onEnter();
  text_.clear();
  std::string buffer(MAX_README_BYTES, '\0');
  const size_t n = Storage.readFileToBuffer(path_.c_str(), buffer.data(), buffer.size(), MAX_README_BYTES - 1);
  buffer.resize(n);
  text_ = std::move(buffer);
  // Normalise line ends; tabs would draw as missing glyphs.
  text_.erase(std::remove(text_.begin(), text_.end(), '\r'), text_.end());
  std::replace(text_.begin(), text_.end(), '\t', ' ');
  lines_.clear();
  wrappedWidth_ = 0;
  page_ = 0;
  requestUpdate();
}

void PluginReadmeActivity::onExit() {
  Activity::onExit();
  text_.clear();
  text_.shrink_to_fit();
  lines_.clear();
  lines_.shrink_to_fit();
}

int PluginReadmeActivity::linesPerPage() const {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
  return std::max(1, height / std::max(1, renderer.getLineHeight(UI_10_FONT_ID)));
}

void PluginReadmeActivity::wrapText() {
  const int width = renderer.getScreenWidth() - 2 * SIDE_MARGIN;
  if (!lines_.empty() && wrappedWidth_ == width) return;
  wrappedWidth_ = width;
  lines_.clear();
  size_t pos = 0;
  while (pos <= text_.size()) {
    size_t end = text_.find('\n', pos);
    if (end == std::string::npos) end = text_.size();
    const std::string paragraph = text_.substr(pos, end - pos);
    if (paragraph.empty()) {
      lines_.emplace_back();
    } else {
      auto wrapped = renderer.wrappedText(UI_10_FONT_ID, paragraph.c_str(), width, 64);
      for (auto& line : wrapped) lines_.push_back(std::move(line));
    }
    if (end >= text_.size()) break;
    pos = end + 1;
  }
}

void PluginReadmeActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }

  const int perPage = linesPerPage();
  const int pageCount = std::max(1, (static_cast<int>(lines_.size()) + perPage - 1) / perPage);
  buttonNavigator_.onNextRelease([this, pageCount] {
    page_ = std::min(page_ + 1, pageCount - 1);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this] {
    page_ = std::max(page_ - 1, 0);
    requestUpdate();
  });
}

void PluginReadmeActivity::render(RenderLock&&) {
  renderer.clearScreen();
  wrapText();

  const auto& metrics = UITheme::getInstance().getMetrics();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 title_.c_str());

  const int perPage = linesPerPage();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int first = page_ * perPage;
  for (int i = 0; i < perPage && first + i < static_cast<int>(lines_.size()); i++) {
    const std::string& line = lines_[first + i];
    if (!line.empty()) renderer.drawText(UI_10_FONT_ID, SIDE_MARGIN, top + i * lineHeight, line.c_str());
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
