#pragma once
// PIN lock logic (platform-free). A PIN is 4-8 digits, stored as "v1:<salt hex>:<hash hex>" where the hash is
// 1000 rounds of SHA-256 over (previous || salt || pin). It is a screen lock: it does not encrypt the SD card.

#include <cstdint>
#include <string>

namespace pinlock {

constexpr size_t kMinDigits = 4;
constexpr size_t kMaxDigits = 8;
constexpr uint32_t kFreeAttempts = 5;  // wrong tries in a row before waiting starts

bool validPin(const std::string& pin);
// `salt` is 16 random bytes from the caller (esp_random on the device).
std::string makeRecord(const std::string& pin, const std::string& salt);
bool verify(const std::string& record, const std::string& pin);
bool wellFormed(const std::string& record);
// Seconds to wait before the next try after `failCount` wrong tries in a row: 0 for the first five, then 30 s,
// doubling each time, at most one hour.
uint32_t delaySeconds(uint32_t failCount);

}  // namespace pinlock
