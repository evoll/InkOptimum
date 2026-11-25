#include "PinFlow.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <functional>
#include <memory>
#include <vector>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "activities/home/BookActions.h"
#include "activities/pin/PinEntryActivity.h"
#include "activities/util/OptionSelectionActivity.h"
#include "util/PinGate.h"

namespace pin_flow {
namespace {

void toast(Activity& host, GfxRenderer& r, const char* text) {
  BookActions::drawToast(r, text);
  delay(900);
  host.requestUpdate(true);
}

// Ask for a new PIN twice, then store it.
void chooseNew(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  host.startActivityForResult(
      std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_PIN_NEW), nullptr, nullptr, true, true),
      [&host, &renderer, &mappedInput](const ActivityResult& r) {
        if (r.isCancelled) return;
        const std::string first = std::get<KeyboardResult>(r.data).text;
        auto same = [first](const std::string& again) {
          PinGate::Result res;
          res.accepted = again == first;
          return res;
        };
        host.startActivityForResult(
            std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_PIN_REPEAT), same, nullptr, true, true),
            [&host, &renderer, first](const ActivityResult& r2) {
              if (r2.isCancelled) return;
              toast(host, renderer, PinGate::setPin(first) ? tr(STR_PIN_SET) : tr(STR_PIN_SAVE_FAILED));
            });
      });
}

// Check the current PIN, then run `then`.
void verifyThen(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput,
                std::function<void()> then) {
  host.startActivityForResult(
      std::make_unique<PinEntryActivity>(renderer, mappedInput, tr(STR_PIN_CURRENT),
                                         [](const std::string& pin) { return PinGate::attempt(pin); }, nullptr, true,
                                         true),
      [then](const ActivityResult& r) {
        if (!r.isCancelled) then();
      });
}

}  // namespace

void open(Activity& host, GfxRenderer& renderer, MappedInputManager& mappedInput) {
  if (!PinGate::enabled()) {
    chooseNew(host, renderer, mappedInput);
    return;
  }
  std::vector<std::string> options = {tr(STR_PIN_CHANGE), tr(STR_PIN_TURN_OFF)};
  host.startActivityForResult(
      std::make_unique<OptionSelectionActivity>(renderer, mappedInput, "PinMenu", StrId::STR_PIN_LOCK, options, 0,
                                                false, false),
      [&host, &renderer, &mappedInput](const ActivityResult& r) {
        if (r.isCancelled) return;
        if (std::get<OptionSelectionResult>(r.data).index == 0) {
          verifyThen(host, renderer, mappedInput, [&host, &renderer, &mappedInput] { chooseNew(host, renderer, mappedInput); });
        } else {
          verifyThen(host, renderer, mappedInput, [&host, &renderer] {
            PinGate::clear();
            toast(host, renderer, tr(STR_PIN_OFF));
          });
        }
      });
}

}  // namespace pin_flow
