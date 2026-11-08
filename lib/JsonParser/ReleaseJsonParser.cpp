#include "ReleaseJsonParser.h"

#include <cstdlib>
#include <cstring>

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

}  // namespace

ReleaseJsonParser::ReleaseJsonParser(AssetMatcher assetMatcher)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd, sOnStringPrefix}),
      assetMatcher(assetMatcher) {
  reset();
}

void ReleaseJsonParser::reset() {
  parser.reset();
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  tagName[0] = '\0';
  releaseName[0] = '\0';
  releaseNotes[0] = '\0';
  firmwareUrl[0] = '\0';
  firmwareDigest[0] = '\0';
  firmwareSigUrl[0] = '\0';
  firmwareSize = 0;
  tagFound = false;
  firmwareFound = false;
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetDigest[0] = '\0';
  currentAssetSize = 0;
}

void ReleaseJsonParser::setAssetMatcher(AssetMatcher matcher) { assetMatcher = matcher; }

void ReleaseJsonParser::feed(const char* data, size_t len) { parser.feed(data, len); }

bool ReleaseJsonParser::foundTag() const { return tagFound; }
bool ReleaseJsonParser::foundFirmware() const { return firmwareFound; }
const char* ReleaseJsonParser::getTagName() const { return tagName; }
const char* ReleaseJsonParser::getFirmwareUrl() const { return firmwareUrl; }
size_t ReleaseJsonParser::getFirmwareSize() const { return firmwareSize; }
const char* ReleaseJsonParser::getFirmwareDigest() const { return firmwareDigest; }
const char* ReleaseJsonParser::getFirmwareSignatureUrl() const { return firmwareSigUrl; }
const char* ReleaseJsonParser::getReleaseName() const { return releaseName; }
const char* ReleaseJsonParser::getReleaseNotes() const { return releaseNotes; }

void ReleaseJsonParser::commitAsset() {
  // "<name>.sig" belongs to the firmware asset when "<name>" is what the matcher picks.
  const size_t nameLen = strlen(currentAssetName);
  if (nameLen > 4 && strcmp(currentAssetName + nameLen - 4, ".sig") == 0 && firmwareSigUrl[0] == '\0') {
    char base[sizeof(currentAssetName)];
    memcpy(base, currentAssetName, nameLen - 4);
    base[nameLen - 4] = '\0';
    const bool baseIsFirmware =
        assetMatcher != nullptr ? assetMatcher(base) : strcmp(base, "firmware.bin") == 0;
    if (baseIsFirmware) memcpy(firmwareSigUrl, currentAssetUrl, sizeof(firmwareSigUrl));
  }
  const bool matchesFirmware =
      assetMatcher != nullptr ? assetMatcher(currentAssetName) : strcmp(currentAssetName, "firmware.bin") == 0;
  if (!firmwareFound && matchesFirmware) {
    memcpy(firmwareUrl, currentAssetUrl, sizeof(firmwareUrl));
    memcpy(firmwareDigest, currentAssetDigest, sizeof(firmwareDigest));
    firmwareSize = currentAssetSize;
    firmwareFound = true;
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetDigest[0] = '\0';
  currentAssetSize = 0;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void ReleaseJsonParser::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == 1) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::RELEASE_NAME;
        else if (len == 4 && memcmp(key, "body", 4) == 0)
          self->lastKey = LastKey::RELEASE_BODY;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else if (len == 6 && memcmp(key, "digest", 6) == 0)
          self->lastKey = LastKey::ASSET_DIGEST;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

// A string cut by the token buffer is only useful for the free-text fields (release notes are routinely
// longer than the buffer; keep their beginning). For tags, asset names, URLs and digests a truncated value
// would be wrong, so it is ignored.
void ReleaseJsonParser::sOnStringPrefix(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);
  if (self->lastKey == LastKey::RELEASE_NAME || self->lastKey == LastKey::RELEASE_BODY) {
    sOnString(ctx, value, len);
  } else {
    self->lastKey = LastKey::NONE;
  }
}

void ReleaseJsonParser::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  // Tags, asset names, URLs and digests of 512 bytes or more were always ignored (the old token buffer);
  // the SDK buffer is larger now, so keep that limit here. Only the free-text fields may be cut.
  const bool freeText = self->lastKey == LastKey::RELEASE_NAME || self->lastKey == LastKey::RELEASE_BODY;
  if (len >= 512 && !freeText) {
    self->lastKey = LastKey::NONE;
    return;
  }

  switch (self->lastKey) {
    case LastKey::RELEASE_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1)
        safeCopy(self->releaseName, sizeof(self->releaseName), value, len);
      break;
    case LastKey::RELEASE_BODY:
      if (self->position == Position::TOP_LEVEL && self->depth == 1)
        safeCopy(self->releaseNotes, sizeof(self->releaseNotes), value, len);
      break;
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == 1) {
        safeCopy(self->tagName, sizeof(self->tagName), value, len);
        self->tagFound = true;
      }
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      break;
    case LastKey::ASSET_DIGEST:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        safeCopy(self->currentAssetDigest, sizeof(self->currentAssetDigest), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNumber(void* ctx, const char* value, size_t /*len*/) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    self->currentAssetSize = static_cast<size_t>(strtoul(value, nullptr, 10));
  }
  self->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnBool(void* ctx, bool /*value*/) {
  static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE;
}

void ReleaseJsonParser::sOnNull(void* ctx) { static_cast<ReleaseJsonParser*>(ctx)->lastKey = LastKey::NONE; }

void ReleaseJsonParser::sOnObjectStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetDigest[0] = '\0';
      self->currentAssetSize = 0;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void ReleaseJsonParser::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayStart(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ASSETS && self->depth == 1) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void ReleaseJsonParser::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<ReleaseJsonParser*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}
