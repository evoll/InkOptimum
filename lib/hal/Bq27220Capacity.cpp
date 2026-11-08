#include "Bq27220Capacity.h"

// BQ27220 Technical Reference Manual (SLUUBD4A) 6.1.
namespace {
constexpr uint8_t CONTROL = 0x00;
constexpr uint8_t OPERATION_STATUS = 0x3A;
constexpr uint8_t FULL_CHARGE_CAPACITY = 0x12;
constexpr uint8_t DESIGN_CAPACITY = 0x3C;
constexpr uint8_t MAC_CONTROL = 0x3E;
constexpr uint8_t MAC_DATA = 0x40;
constexpr uint8_t MAC_DATA_SUM = 0x60;
constexpr uint16_t CFGUPDATE = 1 << 10;
constexpr uint8_t SEC_SEALED = 0b11;
constexpr uint8_t SEC_FULL_ACCESS = 0b01;

constexpr uint16_t TI_DEFAULT_MAH = 3000;
constexpr uint16_t KEYS[] = {0x0414, 0x3672, 0xFFFF, 0xFFFF};
constexpr uint16_t SEALED = 0x0030;
constexpr uint16_t ENTER_CFG_UPDATE = 0x0090;
constexpr uint16_t EXIT_CFG_UPDATE_REINIT = 0x0091;
constexpr uint16_t EXIT_CFG_UPDATE = 0x0092;
constexpr uint16_t DM_LEARNED_FULL_CHARGE_CAPACITY = 0x929D;
constexpr uint16_t DM_DESIGN_CAPACITY = 0x929F;

constexpr uint32_t KEY_GAP_MS = 1500;
constexpr uint32_t SELECT_SETTLE_MS = 10;
constexpr uint32_t FLAG_FIRST_READ_MS = 2000;
constexpr uint32_t FLAG_POLL_MS = 500;
constexpr uint32_t FLAG_GIVE_UP_MS = 5000;

uint8_t security(const uint16_t status) { return (status >> 1) & 0b11; }
uint16_t swapBytes(const uint16_t word) { return static_cast<uint16_t>((word << 8) | (word >> 8)); }
// A learning cycle lowers Learned Full Charge Capacity by at most 256 mAh (TRM 1.1.3), so a value learned
// against TI's 3000 mAh default stays far above a small cell and has to be replaced as well.
bool tooHigh(const uint16_t learned, const uint16_t mah) { return learned > mah + mah / 4; }
bool due(const uint32_t nowMs, const uint32_t atMs) { return static_cast<int32_t>(nowMs - atMs) >= 0; }
}  // namespace

void Bq27220Capacity::tick(Bus& bus, const uint32_t nowMs) {
  if (outcome != Result::Pending || !due(nowMs, nextAt)) return;

  uint16_t status = 0;
  switch (step) {
    case Step::Check:
      if (target == 0 || !bus.read(DESIGN_CAPACITY, dc) || !bus.read(OPERATION_STATUS, status)) {
        outcome = target == 0 ? Result::NotNeeded : Result::Failed;
        return;
      }
      inConfig = status & CFGUPDATE;
      if (dc == target) {
        if (inConfig || security(status) != SEC_SEALED) {
          wrote = true;  // an earlier load was cut short
          return finish(bus, nowMs, true);
        }
        // Design Capacity is right; the gauge may still carry a Learned FCC from the 3000 mAh default.
        uint16_t fcc = 0;
        if (!bus.read(FULL_CHARGE_CAPACITY, fcc) || !tooHigh(fcc, target)) {
          outcome = Result::NotNeeded;
          return;
        }
      } else if (dc != TI_DEFAULT_MAH) {
        outcome = Result::NotNeeded;
        return;
      }
      key = security(status) == SEC_SEALED ? 0 : security(status) == SEC_FULL_ACCESS ? 4 : 2;
      step = Step::Keys;
      return;

    case Step::Keys:
      if (key < 4) {
        if (!bus.write(CONTROL, KEYS[key++])) return finish(bus, nowMs, false);
        nextAt = nowMs + KEY_GAP_MS;
        return;
      }
      inConfig = true;
      if (!bus.write(CONTROL, ENTER_CFG_UPDATE)) return finish(bus, nowMs, false);
      return wait(Step::WaitEnter, nowMs);

    case Step::WaitEnter:
    case Step::WaitExit: {
      const bool entering = step == Step::WaitEnter;
      const bool read = bus.read(OPERATION_STATUS, status);
      const bool reached = read && static_cast<bool>(status & CFGUPDATE) == entering;
      if (read && !reached && !due(nowMs, sentAt + FLAG_GIVE_UP_MS)) {
        nextAt = nowMs + FLAG_POLL_MS;
        return;
      }
      if (!entering) {
        failed |= !reached;
        return seal(bus);
      }
      return finish(bus, nowMs,
                    reached && writeParam(bus, DM_LEARNED_FULL_CHARGE_CAPACITY) &&
                        writeParam(bus, DM_DESIGN_CAPACITY));
    }
  }
}

bool Bq27220Capacity::writeParam(Bus& bus, const uint16_t address) {
  uint16_t old = 0;
  uint16_t sumAndLength = 0;
  if (!bus.write(MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  if (!bus.read(MAC_DATA, old) || !bus.read(MAC_DATA_SUM, sumAndLength)) return false;
  const uint16_t value = swapBytes(old);
  if (address == DM_LEARNED_FULL_CHARGE_CAPACITY ? !tooHigh(value, target) : value != TI_DEFAULT_MAH) return true;

  wrote = true;
  const uint8_t sum =
      static_cast<uint8_t>(sumAndLength + (old & 0xFF) + (old >> 8) - (target & 0xFF) - (target >> 8));
  if (!bus.write(MAC_CONTROL, address)) return false;
  bus.pause(SELECT_SETTLE_MS);
  return bus.write(MAC_DATA, swapBytes(target)) &&
         bus.write(MAC_DATA_SUM, (sumAndLength & 0xFF00) | sum);
}

void Bq27220Capacity::wait(const Step next, const uint32_t nowMs) {
  step = next;
  sentAt = nowMs;
  nextAt = nowMs + FLAG_FIRST_READ_MS;
}

void Bq27220Capacity::finish(Bus& bus, const uint32_t nowMs, const bool ok) {
  failed |= !ok;
  if (inConfig) {
    inConfig = false;
    if (bus.write(CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE)) {
      return wait(Step::WaitExit, nowMs);
    }
    failed = true;
  }
  seal(bus);
}

void Bq27220Capacity::seal(Bus& bus) {
  failed |= !bus.write(CONTROL, SEALED);
  if (!bus.read(DESIGN_CAPACITY, dc)) dc = 0;
  outcome = failed || dc != target ? Result::Failed : Result::Loaded;
}

void Bq27220Capacity::abandon(Bus& bus) {
  if (outcome != Result::Pending || step == Step::Check) return;
  failed = true;
  if (inConfig) bus.write(CONTROL, wrote ? EXIT_CFG_UPDATE_REINIT : EXIT_CFG_UPDATE);
  seal(bus);
}
