#include "PinGate.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PinLock.h>
#include <esp_random.h>

#include <cstdlib>

namespace PinGate {
namespace {
constexpr const char* PATH = "/.inkmod/settings_pin.json";
constexpr const char* DIR = "/.inkmod";

struct State {
  std::string record;
  uint32_t fails = 0;
};

bool armed = false;
uint32_t lockUntilMs = 0;

State load() {
  State s;
  if (!Storage.exists(PATH)) return s;
  const String text = Storage.readFile(PATH);
  const std::string t(text.c_str(), text.length());
  const size_t nl = t.find('\n');
  s.record = t.substr(0, nl);
  if (!pinlock::wellFormed(s.record)) {
    s.record.clear();
    return s;
  }
  if (nl != std::string::npos) s.fails = static_cast<uint32_t>(std::strtoul(t.c_str() + nl + 1, nullptr, 10));
  return s;
}

bool save(const State& s) {
  Storage.mkdir(DIR);
  const std::string text = s.record + "\n" + std::to_string(s.fails) + "\n";
  const bool ok = Storage.writeFile(PATH, text.c_str());
  if (!ok) LOG_ERR("PIN", "Cannot write %s", PATH);
  return ok;
}
}  // namespace

bool enabled() { return !load().record.empty(); }

uint32_t lockedSeconds() {
  const State s = load();
  if (s.record.empty() || s.fails < pinlock::kFreeAttempts) return 0;
  const uint32_t now = millis();
  if (!armed) {  // a fresh boot: the waiting starts again from now
    armed = true;
    lockUntilMs = now + pinlock::delaySeconds(s.fails) * 1000u;
  }
  const int32_t left = static_cast<int32_t>(lockUntilMs - now);
  return left > 0 ? static_cast<uint32_t>((left + 999) / 1000) : 0;
}

Result attempt(const std::string& pin) {
  Result r;
  State s = load();
  if (s.record.empty()) {
    r.accepted = true;
    return r;
  }
  const uint32_t wait = lockedSeconds();
  if (wait > 0) {
    r.locked = true;
    r.waitSeconds = wait;
    return r;
  }
  if (pinlock::verify(s.record, pin)) {
    if (s.fails != 0) {
      s.fails = 0;
      save(s);
    }
    armed = false;
    r.accepted = true;
    return r;
  }
  s.fails++;
  save(s);
  if (s.fails >= pinlock::kFreeAttempts) {
    armed = true;
    lockUntilMs = millis() + pinlock::delaySeconds(s.fails) * 1000u;
    r.locked = true;
    r.waitSeconds = pinlock::delaySeconds(s.fails);
  } else {
    r.triesLeft = pinlock::kFreeAttempts - s.fails;
  }
  return r;
}

bool setPin(const std::string& pin) {
  if (!pinlock::validPin(pin)) return false;
  std::string salt(16, '\0');
  for (size_t i = 0; i < salt.size(); i += 4) {
    const uint32_t v = esp_random();
    for (size_t k = 0; k < 4; ++k) salt[i + k] = static_cast<char>((v >> (8 * k)) & 0xFF);
  }
  State s;
  s.record = pinlock::makeRecord(pin, salt);
  if (s.record.empty()) return false;
  armed = false;
  return save(s);
}

void clear() {
  armed = false;
  if (Storage.exists(PATH)) Storage.remove(PATH);
}

}  // namespace PinGate
