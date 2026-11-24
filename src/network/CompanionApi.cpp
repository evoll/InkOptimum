#include "CompanionApi.h"

#include <ArduinoJson.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WebServer.h>
#include <esp_random.h>

#include <CompanionAuth.h>

#include "AppVersion.h"
#include "InkMODState.h"

namespace companionapi {
namespace {

constexpr const char* DIR = "/.crosspoint";
constexpr const char* TOKENS_PATH = "/.crosspoint/companion_tokens.txt";

companion::Pairing pairing;
companion::TokenStore tokens;
bool tokensLoaded = false;
bool shownActive = false;
std::string shownCode;

void ensureTokens() {
  if (tokensLoaded) return;
  tokensLoaded = true;
  const String text = Storage.readFile(TOKENS_PATH);
  if (text.length()) tokens.load(std::string(text.c_str(), text.length()));
}

void saveTokens() {
  Storage.mkdir(DIR);
  if (!Storage.writeFile(TOKENS_PATH, tokens.serialize().c_str())) LOG_ERR("COMP", "token file not saved");
}

void sendJson(WebServer& s, int code, JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  s.sendHeader("Cache-Control", "no-store");
  s.send(code, "application/json", out);
}

void sendError(WebServer& s, int code, const char* err) {
  JsonDocument doc;
  doc["error"] = err;
  sendJson(s, code, doc);
}

void handleInfo(WebServer& s) {
  ensureTokens();
  JsonDocument doc;
  doc["api"] = 1;
  doc["name"] = "InkOptimum";
  doc["version"] = INKMOD_VERSION;
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";
  doc["paired"] = !tokens.tokens().empty();
  doc["pairingOpen"] = pairing.active(millis());
  doc["authorized"] = authorized(s);
  sendJson(s, 200, doc);
}

void handlePairStart(WebServer& s) {
  if (!pairing.begin(esp_random(), millis())) {
    sendError(s, 429, "locked");
    return;
  }
  JsonDocument doc;
  doc["expiresInSec"] = companion::Pairing::kCodeLifetimeMs / 1000;
  sendJson(s, 200, doc);  // the code itself is shown only on the reader's screen
}

void handlePairConfirm(WebServer& s) {
  ensureTokens();
  JsonDocument in;
  if (deserializeJson(in, s.arg("plain")) != DeserializationError::Ok) {
    sendError(s, 400, "bad_json");
    return;
  }
  const std::string code = in["code"] | "";
  const std::string name = in["deviceName"] | "";
  switch (pairing.confirm(code, millis())) {
    case companion::Pairing::Result::Ok: {
      std::string raw(32, '\0');
      for (size_t i = 0; i < 32; i += 4) {
        const uint32_t r = esp_random();
        for (int j = 0; j < 4; j++) raw[i + j] = static_cast<char>(r >> (8 * j));
      }
      const std::string token = tokens.issue(raw, name);
      saveTokens();
      JsonDocument doc;
      doc["token"] = token;
      sendJson(s, 200, doc);
      break;
    }
    case companion::Pairing::Result::Wrong: sendError(s, 403, "wrong_code"); break;
    case companion::Pairing::Result::Locked: sendError(s, 429, "locked"); break;
    case companion::Pairing::Result::NoSession: sendError(s, 409, "no_pairing"); break;
  }
}

void handleDevices(WebServer& s) {
  if (!authorized(s)) return sendError(s, 401, "unauthorized");
  JsonDocument doc;
  JsonArray arr = doc["devices"].to<JsonArray>();
  for (const auto& t : tokens.tokens()) {
    JsonObject o = arr.add<JsonObject>();
    o["id"] = t.id;
    o["name"] = t.name;
  }
  sendJson(s, 200, doc);
}

void handleRevoke(WebServer& s) {
  if (!authorized(s)) return sendError(s, 401, "unauthorized");
  JsonDocument in;
  if (deserializeJson(in, s.arg("plain")) != DeserializationError::Ok) return sendError(s, 400, "bad_json");
  const std::string id = in["id"] | "";
  if (!tokens.revoke(id)) return sendError(s, 404, "not_found");
  saveTokens();
  JsonDocument doc;
  doc["ok"] = true;
  sendJson(s, 200, doc);
}

}  // namespace

bool authorized(WebServer& s) {
  ensureTokens();
  const String h = s.header("Authorization");
  const std::string token = companion::bearerFrom(std::string(h.c_str(), h.length()));
  return !token.empty() && tokens.verify(token);
}

void registerRoutes(WebServer& s) {
  s.on("/api/v1/info", HTTP_GET, [&s] { handleInfo(s); });
  s.on("/api/v1/pair/start", HTTP_POST, [&s] { handlePairStart(s); });
  s.on("/api/v1/pair/confirm", HTTP_POST, [&s] { handlePairConfirm(s); });
  s.on("/api/v1/devices", HTTP_GET, [&s] { handleDevices(s); });
  s.on("/api/v1/devices/revoke", HTTP_POST, [&s] { handleRevoke(s); });
}

std::string codeToShow(bool& changed) {
  const bool active = pairing.active(millis());
  const std::string code = active ? pairing.code() : std::string();
  changed = active != shownActive || code != shownCode;
  shownActive = active;
  shownCode = code;
  return code;
}

void reset() {
  pairing.cancel();
  shownActive = false;
  shownCode.clear();
}

}  // namespace companionapi
