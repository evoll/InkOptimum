#pragma once

#include <string>

#include "activities/Activity.h"

// Shows the text of a footnote without leaving the current reading position.
// Confirm = open the note in the book (result not cancelled), Back = return.
class FootnotePreviewActivity final : public Activity {
 public:
  explicit FootnotePreviewActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string marker,
                                   std::string text)
      : Activity("FootnotePreview", renderer, mappedInput), marker(std::move(marker)), text(std::move(text)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool allowPowerAsConfirmInReaderMode() const override { return true; }

 private:
  std::string marker;
  std::string text;
};
