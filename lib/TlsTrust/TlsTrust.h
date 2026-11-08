#pragma once

#include <TlsPolicy.h>

#include <string>

namespace freeink {
class SecureHttpClient;
}

// Applies the user's TLS trust mode (Settings > System > Server certificates) to a wolfSSL HTTPS client.
// See lib/TlsPolicy for what the modes mean. Call before begin().
namespace TlsTrust {

// The application tells the library which mode the user chose (kept out of the library so it does not
// depend on the settings store). Until set, trust-on-first-use applies.
void setModeProvider(tlspolicy::Mode (*provider)());

void apply(freeink::SecureHttpClient& http);

// Host:port of the most recent connection refused because its certificate differs from the pinned one
// ("" when none since the last call). Lets an error screen say why the server was refused.
std::string takeRejectedHost();

// Forgets every remembered server certificate.
bool forgetAllPins();
size_t pinCount();

}  // namespace TlsTrust
