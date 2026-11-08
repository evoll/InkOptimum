#pragma once

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Apps > LCP loans: the protected books this reader holds a key for, with their due dates. Per book:
// refresh the key (renewals, a new due date) and return the loan through the provider's status document.
// The first row checks the LCP service (reachable / device enrolled / release key in this build).
class LcpLoansActivity final : public Activity {
 public:
  explicit LcpLoansActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LcpLoans", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  struct Loan {
    std::string path;
    std::string title;
    std::string due;  // human line: "Until 2026-03-01", "Expired", ...
  };

  void loadLoans();
  void activate();
  void runAction(int action);
  // Runs job once Wi-Fi is up (offering the Wi-Fi picker when it is not).
  void withNetwork(const std::function<void()>& job);
  void busy(const char* message);
  int rowCount() const { return 1 + static_cast<int>(loans_.size()); }

  ButtonNavigator buttonNavigator_;
  std::vector<Loan> loans_;
  int selected_ = 0;
  bool actionsOpen_ = false;
  int actionSelected_ = 0;
  bool returnArmed_ = false;
  bool busy_ = false;
  std::string statusLine_;
};
