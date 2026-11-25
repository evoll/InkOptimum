#pragma once
// Companion-app API v1 (docs/COMPANION_APP.md, stage 0): pairing by an on-screen 6-digit code and bearer tokens.
// /api/v1/info is open; every other /api/v1/* route needs "Authorization: Bearer <token>".

#include <string>

class WebServer;

namespace companionapi {

void registerRoutes(WebServer& server);  // call before server.begin(); the caller must collect the Authorization header
bool authorized(WebServer& server);      // true when the request carries a valid bearer token
// The code to show on the reader while a pairing session is open (empty otherwise). `changed` is set when what the
// screen shows changed since the last call, so the activity knows to repaint.
std::string codeToShow(bool& changed);
// Forget the cached token list (call after the token file was edited elsewhere, e.g. the Linked devices screen).
void invalidateTokens();
void reset();  // server stopped: drop an open pairing session

}  // namespace companionapi
