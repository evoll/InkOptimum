#pragma once
// Named KOReader-sync server profiles persisted in /.inkmod/koreader_profiles.json.
// Applying a profile copies it into KOREADER_STORE, so the sync code stays unchanged.
#include <SyncProfile.h>

#include <string>
#include <vector>

class KOReaderProfileStore {
 public:
  static KOReaderProfileStore& getInstance() { return instance; }

  bool load();
  bool save() const;

  const std::vector<sync_profile::Profile>& profiles() const { return list; }
  const std::string& activeName() const { return active; }

  // Validates and stores the profile (header via opds_profile, URL via sync_profile). Empty password on an
  // existing profile keeps the old one. Returns false and fills `error` on invalid input.
  bool upsert(sync_profile::Profile p, bool keepPasswordIfEmpty, std::string& error);
  bool remove(const std::string& name);
  // Copies the profile into KOREADER_STORE and saves both stores.
  bool activate(const std::string& name);
  // Switch to the next profile (device UI). Returns the activated name or "".
  std::string activateNext();

 private:
  static KOReaderProfileStore instance;
  std::vector<sync_profile::Profile> list;
  std::string active;
  bool loaded = false;
  void ensureLoaded();
};

#define KOREADER_PROFILES KOReaderProfileStore::getInstance()
