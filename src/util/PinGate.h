#pragma once
// Device PIN lock glue (lib/PinLock holds the maths). The record lives in /.inkmod/settings_pin.json (web access to
// it is refused like the other credential files). Wrong tries are counted on the card, so power-cycling does not
// reset the waiting time.

#include <cstdint>
#include <string>

namespace PinGate {

struct Result {
  bool accepted = false;
  bool locked = false;       // too many wrong tries: wait `waitSeconds` before the next one
  uint32_t waitSeconds = 0;
  uint32_t triesLeft = 0;    // before waiting starts (0 once locked)
};

bool enabled();
uint32_t lockedSeconds();  // time still to wait (0 = a try is allowed now)
Result attempt(const std::string& pin);
bool setPin(const std::string& pin);  // false: not 4-8 digits or the card write failed
void clear();

}  // namespace PinGate
