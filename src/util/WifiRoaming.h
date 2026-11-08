#pragma once

#include <WiFi.h>

namespace wifi_roaming {

// By default the ESP32 station joins the *first* access point it finds for an SSID. On a mesh or
// repeater network that is often a distant one, and uploads then stay slow or fail. Scanning every
// channel and sorting candidates by signal makes WiFi.begin() join the strongest AP of the network.
// Call right before WiFi.begin(). Costs about a second of extra scanning at connect time.
inline void preferStrongestAccessPoint() {
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
}

}  // namespace wifi_roaming
