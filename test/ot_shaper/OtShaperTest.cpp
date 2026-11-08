// Shapes Indic words with lib/OtShaper (a port of witchhunt's, itself a port of
// HarfBuzz 14.5's Indic engine) and compares glyph IDs and advances with what
// HarfBuzz gave for the same fixture font (test/ot_shaper/compare_harfbuzz.py
// runs the same comparison over word lists). The fixtures are the Noto Sans
// layout subsets (OFL, data/OFL.txt).
#include <OtShaper.h>
#include <Utf8.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace {

struct Glyph {
  uint32_t id;
  int32_t advance, xOffset, yOffset;
};
struct Case {
  const char* font;
  const char* text;
  std::vector<Glyph> glyphs;
};

const std::vector<Case> kCases = {
    {"Devanagari", "क्षि", {{453,552,0,0}, {86,1529,0,0}}},
    {"Devanagari", "हिन्दी", {{446,552,0,0}, {84,1133,0,0}, {150,659,0,0}, {69,1133,0,0}, {29,552,0,0}}},
    {"Devanagari", "कर्म", {{52,1638,0,0}, {76,1276,0,0}, {408,0,0,0}}},
    {"Devanagari", "श्रीमान", {{222,1510,0,0}, {29,552,0,0}, {76,1276,0,0}, {27,552,0,0}, {71,1184,0,0}}},
    {"Bengali", "ক্ষমা", {{107,1832,0,0}, {46,1305,0,0}, {56,567,0,0}}},
    {"Tamil", "கொடு", {{44,1922,0,0}, {16,1760,0,0}, {39,1333,0,0}, {114,2165,0,0}}},
};

std::vector<uint8_t> load(const std::string& name) {
  std::ifstream f(std::string(OT_FIXTURE_DIR) + "/" + name, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

std::vector<uint32_t> decode(const char* s) {
  std::vector<uint32_t> out;
  const auto* p = reinterpret_cast<const unsigned char*>(s);
  while (*p) out.push_back(utf8NextCodepoint(&p));
  return out;
}

const ot::Script kScript[] = {ot::Script::Devanagari, ot::Script::Bengali, ot::Script::Tamil};

}  // namespace

TEST(OtShaper, MatchesHarfBuzz) {
  static uint8_t storage[ot::Buffer::bytesFor(512)];
  for (const auto& c : kCases) {
    const auto font = load(std::string("NotoSans") + c.font + "-Regular.layout");
    ASSERT_FALSE(font.empty()) << c.font;
    ot::FaceTables tables;
    ASSERT_TRUE(ot::tablesFromSfnt(font.data(), static_cast<uint32_t>(font.size()), &tables));
    ot::Face face;
    ASSERT_TRUE(face.init(tables));
    ot::Scale scale;
    scale.set(2133, 33, face.upem());
    ot::Script script = std::string(c.font) == "Devanagari" ? kScript[0]
                        : std::string(c.font) == "Bengali"  ? kScript[1]
                                                            : kScript[2];
    ot::Plan plan;
    plan.build(face, script, ot::languageTagsFor(""));
    ot::Buffer buffer;
    buffer.setStorage(storage, sizeof(storage));
    const auto cps = decode(c.text);
    ASSERT_TRUE(ot::shape(face, scale, plan, cps.data(), static_cast<unsigned>(cps.size()), buffer)) << c.text;
    ASSERT_EQ(buffer.len(), c.glyphs.size()) << c.text;
    for (unsigned i = 0; i < buffer.len(); ++i) {
      EXPECT_EQ(buffer.info[i].codepoint, c.glyphs[i].id) << c.text << " glyph " << i;
      EXPECT_EQ(buffer.pos[i].xAdvance, c.glyphs[i].advance) << c.text << " glyph " << i;
      EXPECT_EQ(buffer.pos[i].xOffset, c.glyphs[i].xOffset) << c.text << " glyph " << i;
      EXPECT_EQ(buffer.pos[i].yOffset, c.glyphs[i].yOffset) << c.text << " glyph " << i;
    }
  }
}

// A streamed font has no cmap or hmtx in memory: the face asks the font.
TEST(OtShaper, HooksStandInForCmapAndHmtx) {
  const auto font = load("NotoSansDevanagari-Regular.layout");
  ot::FaceTables full;
  ASSERT_TRUE(ot::tablesFromSfnt(font.data(), static_cast<uint32_t>(font.size()), &full));
  ot::Face reference;
  ASSERT_TRUE(reference.init(full));

  ot::FaceTables slim = full;
  slim[ot::FaceTables::CMAP] = ot::Table();
  slim[ot::FaceTables::HMTX] = ot::Table();
  ot::Face hooked;
  ASSERT_FALSE(hooked.init(slim));  // no hooks, no cmap

  ot::Face::Hooks hooks;
  hooks.ctx = &reference;
  hooks.nominalGlyph = [](void* ctx, uint32_t cp, uint32_t* g) {
    return static_cast<ot::Face*>(ctx)->nominalGlyph(cp, g);
  };
  hooks.advance = [](void* ctx, uint32_t g) { return static_cast<ot::Face*>(ctx)->advance(g); };
  hooked.setHooks(hooks);
  ASSERT_TRUE(hooked.init(slim));

  static uint8_t storage[ot::Buffer::bytesFor(512)];
  const auto shapeWith = [&](const ot::Face& face) {
    ot::Scale scale;
    scale.set(2133, 33, face.upem());
    ot::Plan plan;
    plan.build(face, ot::Script::Devanagari, ot::languageTagsFor(""));
    ot::Buffer buffer;
    buffer.setStorage(storage, sizeof(storage));
    const auto cps = decode("हिन्दी");
    EXPECT_TRUE(ot::shape(face, scale, plan, cps.data(), static_cast<unsigned>(cps.size()), buffer));
    std::vector<int64_t> out;
    for (unsigned i = 0; i < buffer.len(); ++i) {
      out.push_back(buffer.info[i].codepoint);
      out.push_back(buffer.pos[i].xAdvance);
    }
    return out;
  };
  EXPECT_EQ(shapeWith(hooked), shapeWith(reference));
}

#include <GlyphKey.h>

TEST(GlyphKey, RoundTripsThroughUtf8) {
  std::string s;
  const uint32_t keys[] = {glyphkey::make(453, 0, 0), glyphkey::make(65535, -64, 31, true), glyphkey::make(1, 5, -7)};
  utf8AppendCodepoint('a', s);
  for (const uint32_t k : keys) utf8AppendCodepoint(k, s);
  utf8AppendCodepoint(0x1F600, s);
  const auto* p = reinterpret_cast<const unsigned char*>(s.c_str());
  EXPECT_EQ(utf8NextCodepoint(&p), uint32_t{'a'});
  for (const uint32_t k : keys) {
    const uint32_t got = utf8NextCodepoint(&p);
    EXPECT_EQ(got, k);
    EXPECT_TRUE(glyphkey::isKey(got));
  }
  EXPECT_EQ(utf8NextCodepoint(&p), uint32_t{0x1F600});
  EXPECT_EQ(utf8NextCodepoint(&p), uint32_t{0});
  EXPECT_EQ(glyphkey::gid(keys[1]), 65535u);
  EXPECT_EQ(glyphkey::xOffsetUnits(keys[1]), -64);
  EXPECT_EQ(glyphkey::yOffsetUnits(keys[1]), 31);
  EXPECT_TRUE(glyphkey::noAdvance(keys[1]));
  EXPECT_FALSE(glyphkey::noAdvance(keys[2]));
  EXPECT_EQ(glyphkey::xOffsetUnits(keys[2]), 5);
  EXPECT_EQ(glyphkey::yOffsetUnits(keys[2]), -7);
}

TEST(GlyphKey, StrayLongFormsStayInvalid) {
  // 0xFC 0x80.. is a six-byte form below the key range, and 0xFE/0xFF were never UTF-8.
  const unsigned char bad[] = {0xFC, 0x80, 0x80, 0x80, 0x80, 0xA1, 0x00};
  const unsigned char* p = bad;
  EXPECT_EQ(utf8NextCodepoint(&p), uint32_t{REPLACEMENT_GLYPH});
  EXPECT_EQ(p, bad + 1);
  EXPECT_EQ(utf8SafeTruncateBuffer(reinterpret_cast<const char*>(bad), 3), 3);
}
