#include <gtest/gtest.h>
extern "C" {
#include "minibidi.h"
}
#include <vector>

static std::vector<ucschar> run(const std::vector<ucschar>& in, int level, int* outLevel = nullptr) {
  std::vector<bidi_char> line(in.size());
  for (size_t i = 0; i < in.size(); i++) line[i] = {in[i], in[i], static_cast<uint16_t>(i)};
  int l = do_bidi(false, level, line.data(), static_cast<int>(line.size()));
  if (outLevel) *outLevel = l;
  std::vector<ucschar> out;
  for (auto& c : line) out.push_back(c.wc);
  return out;
}

// Hebrew letters alef, bet, gimel
static const ucschar HA = 0x05D0, HB = 0x05D1, HG = 0x05D2;

TEST(MiniBidi, BracketsAroundHebrewAreMirroredInTheRightPlaces) {
  // logical: ( alef bet )  in an RTL paragraph -> visual left-to-right: ( bet alef ) with both brackets swapped
  auto out = run({'(', HA, HB, ')'}, 1);
  EXPECT_EQ(out, (std::vector<ucschar>{'(', HB, HA, ')'}));
}

TEST(MiniBidi, LatinInsideRtlParagraphKeepsItsBrackets) {
  // alef ( a ) gimel : the latin run "(a)" ... brackets pair inside an LTR-resolved run and stay unmirrored
  auto out = run({HA, ' ', 'a', ' ', HG}, 1);
  EXPECT_EQ(out.size(), 5u);
  EXPECT_EQ(out[0], HG);
  EXPECT_EQ(out[4], HA);
}

TEST(MiniBidi, MirroringFollowsTheCharacterNotTheVisualSlot) {
  // RTL paragraph: alef ( bet  -> opener is in an RTL run, so it is shown as ')' at its visual place
  auto out = run({HA, '(', HB}, 1);
  EXPECT_EQ(out, (std::vector<ucschar>{HB, ')', HA}));
}

TEST(MiniBidi, BracketsInsideALatinRunStayUnmirroredAfterTheRunsAreFlipped) {
  // RTL paragraph, logical "a(b) alef": the latin run keeps "(b)"; mirroring by the stale pre-flip levels
  // used to turn the closing bracket into '(' because it landed on an index that was RTL before the flip.
  auto out = run({'a', '(', 'b', ')', ' ', HA}, 1);
  EXPECT_EQ(out, (std::vector<ucschar>{HA, ' ', 'a', '(', 'b', ')'}));
}
