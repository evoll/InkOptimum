#include "TlsTrust.h"

#include <HalStorage.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <TlsPolicy.h>


namespace {

constexpr char PIN_FILE[] = "/.inkmod/tls_pins.json";
constexpr char CA_FILE[] = "/.inkmod/ca.pem";
constexpr size_t MAX_CA_BYTES = 24 * 1024;

tlspolicy::PinStore& pins() {
  static tlspolicy::PinStore store;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    const String json = Storage.readFile(PIN_FILE);
    if (json.length() > 0) store.load(json.c_str());
  }
  return store;
}

void savePins() {
  tlspolicy::PinStore& store = pins();
  if (!store.changed()) return;
  if (!Storage.exists("/.inkmod")) Storage.mkdir("/.inkmod");
  if (Storage.writeFile(PIN_FILE, store.toJson().c_str())) store.markSaved();
}

std::string& rejectedHost() {
  static std::string host;
  return host;
}

// Runs after the handshake, before the first request byte (so before any password is sent).
bool peerCheck(void*, const char* host, const uint16_t port, const uint8_t* fingerprint) {
  const std::string key = tlspolicy::hostKey(host, port);
  const std::string hex = fingerprint ? tlspolicy::toHex(fingerprint, tlspolicy::FINGERPRINT_BYTES) : std::string();
  const auto decision = tlspolicy::decideTofu(pins(), key, hex);
  if (decision == tlspolicy::Decision::Reject) {
    LOG_ERR("TLS", "certificate of %s changed since it was first trusted: refusing", key.c_str());
    rejectedHost() = key;
    return false;
  }
  if (decision == tlspolicy::Decision::AcceptAndSave) {
    LOG_INF("TLS", "trusting the certificate of %s from now on", key.c_str());
    savePins();
  } else if (hex.empty()) {
    LOG_ERR("TLS", "this build cannot read the certificate of %s: connection is not pinned", key.c_str());
  }
  return true;
}

// The user's own CA file, read once per boot. Empty when absent.
const std::string& caPem() {
  static std::string pem;
  static bool loaded = false;
  if (!loaded) {
    loaded = true;
    if (Storage.exists(CA_FILE)) {
      std::string buffer(MAX_CA_BYTES, '\0');
      const size_t n = Storage.readFileToBuffer(CA_FILE, buffer.data(), buffer.size(), MAX_CA_BYTES - 1);
      buffer.resize(n);
      pem = std::move(buffer);
    }
  }
  return pem;
}

}  // namespace

namespace TlsTrust {

namespace {
tlspolicy::Mode (*modeProvider)() = nullptr;
}

void setModeProvider(tlspolicy::Mode (*provider)()) { modeProvider = provider; }

void apply(freeink::SecureHttpClient& http) {
  switch (modeProvider ? modeProvider() : tlspolicy::Mode::TrustOnFirstUse) {
    case tlspolicy::Mode::Off:
      http.setInsecure();
      break;
    case tlspolicy::Mode::Strict: {
      const std::string& pem = caPem();
      if (!pem.empty()) {
        http.setCACert(pem.c_str());  // chain and host name are verified by the transport
      } else {
        // No CA file: fail closed rather than silently trusting everything. wolfSSL with no roots refuses.
        LOG_ERR("TLS", "strict mode but %s is missing: HTTPS will fail", CA_FILE);
        http.setCACert("");
      }
      break;
    }
    case tlspolicy::Mode::TrustOnFirstUse:
    default:
      http.setInsecure();  // the chain is not checked; the pin below is what protects the connection
      http.setPeerCheck(&peerCheck, nullptr);
      break;
  }
}

std::string takeRejectedHost() {
  std::string host;
  host.swap(rejectedHost());
  return host;
}

bool forgetAllPins() {
  pins().clear();
  const bool ok = !Storage.exists(PIN_FILE) || Storage.remove(PIN_FILE);
  pins().markSaved();
  return ok;
}

size_t pinCount() { return pins().size(); }

}  // namespace TlsTrust
