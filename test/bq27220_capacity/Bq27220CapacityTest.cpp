#include <gtest/gtest.h>

#include "Bq27220Capacity.h"

namespace {
uint16_t bswap(const uint16_t w) { return static_cast<uint16_t>((w << 8) | (w >> 8)); }

// Minimal model of the BQ27220 registers the loader touches.
struct FakeGauge final : Bq27220Capacity::Bus {
  uint16_t designCapacity = 3000;
  uint16_t learnedFcc = 3000;
  uint8_t security = 3;  // 3 sealed, 2 unsealed, 1 full access
  bool cfgUpdate = false;
  uint8_t keysSeen = 0;
  uint16_t selected = 0;
  uint32_t clock = 0;
  int writes = 0;

  bool write(const uint8_t reg, const uint16_t word) override {
    if (reg == 0x00) {
      if (word == 0x0414 && keysSeen == 0) { keysSeen = 1; }
      else if (word == 0x3672 && keysSeen == 1) { keysSeen = 2; security = 2; }
      else if (word == 0xFFFF && keysSeen >= 2 && keysSeen < 4) { if (++keysSeen == 4) security = 1; }
      else if (word == 0x0090) cfgUpdate = security == 1;
      else if (word == 0x0091 || word == 0x0092) cfgUpdate = false;
      else if (word == 0x0030) { security = 3; keysSeen = 0; }
    } else if (reg == 0x3E) {
      selected = word;
    } else if (reg == 0x40) {
      ++writes;
      if (!cfgUpdate) return true;
      if (selected == 0x929D) learnedFcc = bswap(word);
      if (selected == 0x929F) designCapacity = bswap(word);
    }
    return true;
  }
  bool read(const uint8_t reg, uint16_t& word) override {
    switch (reg) {
      case 0x12: word = learnedFcc; return true;
      case 0x3A: word = static_cast<uint16_t>((cfgUpdate ? 0x0400 : 0) | (security << 1)); return true;
      case 0x3C: word = designCapacity; return true;
      case 0x40: word = bswap(selected == 0x929D ? learnedFcc : designCapacity); return true;
      case 0x60: word = 0x0A00; return true;  // checksum is not validated by the model
      default: return false;
    }
  }
  void pause(const uint32_t ms) override { clock += ms; }
};

Bq27220Capacity::Result run(FakeGauge& g, const uint16_t target) {
  Bq27220Capacity load(target);
  for (int i = 0; i < 400 && load.result() == Bq27220Capacity::Result::Pending; ++i) {
    g.clock += 100;
    load.tick(g, g.clock);
  }
  return load.result();
}
}  // namespace

TEST(Bq27220Capacity, FreshGaugeGetsBothValues) {
  FakeGauge g;
  EXPECT_EQ(run(g, 650), Bq27220Capacity::Result::Loaded);
  EXPECT_EQ(g.designCapacity, 650);
  EXPECT_EQ(g.learnedFcc, 650);
  EXPECT_EQ(g.security, 3);
  EXPECT_FALSE(g.cfgUpdate);
}

TEST(Bq27220Capacity, CorrectDesignButStaleLearnedFccIsFixed) {
  FakeGauge g;
  g.designCapacity = 650;
  g.learnedFcc = 2900;
  EXPECT_EQ(run(g, 650), Bq27220Capacity::Result::Loaded);
  EXPECT_EQ(g.designCapacity, 650);
  EXPECT_EQ(g.learnedFcc, 650);
}

TEST(Bq27220Capacity, HealthyGaugeIsLeftAlone) {
  FakeGauge g;
  g.designCapacity = 650;
  g.learnedFcc = 700;  // within a quarter of the cell
  EXPECT_EQ(run(g, 650), Bq27220Capacity::Result::NotNeeded);
  EXPECT_EQ(g.writes, 0);
}

TEST(Bq27220Capacity, ForeignDesignCapacityIsNotTouched) {
  FakeGauge g;
  g.designCapacity = 1200;
  EXPECT_EQ(run(g, 650), Bq27220Capacity::Result::NotNeeded);
  EXPECT_EQ(g.designCapacity, 1200);
}

TEST(Bq27220Capacity, NoTargetMeansNotNeeded) {
  FakeGauge g;
  EXPECT_EQ(run(g, 0), Bq27220Capacity::Result::NotNeeded);
}
