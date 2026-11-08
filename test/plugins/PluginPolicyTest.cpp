#include <gtest/gtest.h>

#include "ProtectedPaths.h"
#include "util/PluginLocations.h"

using namespace PluginLocations;

TEST(PluginVersion, ParsesExactlyThreeNumericParts) {
  uint32_t v[3];
  EXPECT_TRUE(parseVersion("1.22.333", v));
  EXPECT_EQ(v[0], 1u);
  EXPECT_EQ(v[1], 22u);
  EXPECT_EQ(v[2], 333u);
  EXPECT_FALSE(parseVersion("1.2", v));
  EXPECT_FALSE(parseVersion("1.2.3.4", v));
  EXPECT_FALSE(parseVersion("1.2.3-beta", v));
  EXPECT_FALSE(parseVersion("+1.2.3", v));
  EXPECT_FALSE(parseVersion("1..3", v));
  EXPECT_FALSE(parseVersion("", v));
  EXPECT_FALSE(parseVersion("99999999999.0.0", v));
}

TEST(PluginVersion, NewerComparesNumerically) {
  EXPECT_TRUE(isNewerVersion("1.10.0", "1.9.9"));
  EXPECT_FALSE(isNewerVersion("1.9.9", "1.10.0"));
  EXPECT_FALSE(isNewerVersion("1.0.0", "1.0.0"));
  EXPECT_TRUE(isNewerVersion("1.0.0", "garbage"));   // unreadable installed version: offer the update
  EXPECT_FALSE(isNewerVersion("garbage", "1.0.0"));  // unreadable catalog version: never
}

TEST(PluginDiscovery, QuarantinedAndMarkerlessFoldersAreNotListed) {
  EXPECT_TRUE(isDiscoverableEntry(false, true, false, false));
  EXPECT_TRUE(isDiscoverableEntry(false, false, true, false));
  EXPECT_TRUE(isDiscoverableEntry(false, false, false, true));
  EXPECT_FALSE(isDiscoverableEntry(true, true, true, true));
  EXPECT_FALSE(isDiscoverableEntry(false, false, false, false));
}

TEST(PluginDiscovery, PickerOpensCatalogsAndReadmesOnly) {
  const auto catalog = classifyDeviceManifest(true, false);
  EXPECT_EQ(catalog, DeviceKind::Catalog);
  EXPECT_EQ(classifyDeviceManifest(false, true), DeviceKind::Background);
  EXPECT_EQ(classifyDeviceManifest(false, false), DeviceKind::None);
  EXPECT_EQ(pickerAction(catalog, false), PickerAction::Catalog);
  EXPECT_EQ(pickerAction(DeviceKind::Background, true), PickerAction::Readme);
  EXPECT_EQ(pickerAction(DeviceKind::None, true), PickerAction::Readme);
  EXPECT_EQ(pickerAction(DeviceKind::Background, false), PickerAction::None);
}

TEST(PluginDiscovery, BundlesReuseAnInstalledFolderOnlyForDirectChildren) {
  EXPECT_TRUE(shouldReuseInstalledBundleDir("/plugins", "hub"));
  EXPECT_TRUE(shouldReuseInstalledBundleDir("/.inkmod/plugins", "hub"));
  EXPECT_FALSE(shouldReuseInstalledBundleDir("/plugins", "a/b"));
  EXPECT_FALSE(shouldReuseInstalledBundleDir("/plugins", ""));
  EXPECT_FALSE(shouldReuseInstalledBundleDir("/books", "hub"));
}

TEST(PluginPaths, CredentialStoresAreOffLimits) {
  using protectedpaths::isPluginPath;
  using protectedpaths::isSensitivePath;
  EXPECT_TRUE(isSensitivePath("/.inkmod/wifi.json"));
  EXPECT_TRUE(isSensitivePath("/.INKMOD/WIFI.JSON"));          // FAT is case-insensitive
  EXPECT_TRUE(isSensitivePath("/books/../.inkmod/opds.json"));  // ".." resolves to the store
  EXPECT_TRUE(isSensitivePath("//.inkmod//koreader.json.bak"));
  EXPECT_TRUE(isSensitivePath("/.inkmod/koreader_profiles.json"));
  EXPECT_TRUE(isSensitivePath("/INKMOD~1/wifi.json"));  // 8.3 alias of the data folder
  EXPECT_FALSE(isSensitivePath("/.inkmod/plugins/hub/device.json"));
  EXPECT_FALSE(isSensitivePath("/books/wifi.json"));
  EXPECT_TRUE(isPluginPath("/plugins/hub/token.json"));
  EXPECT_FALSE(isPluginPath("plugins/hub"));        // not absolute
  EXPECT_FALSE(isPluginPath("/plugins/../x"));      // parent reference
  EXPECT_FALSE(isPluginPath("/.inkmod/wifi.json"));
}
