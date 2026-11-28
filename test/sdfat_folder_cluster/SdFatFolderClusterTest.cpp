// SdFat's free cluster search, built as the firmware builds it (SdFat 2.3.1 with
// scripts/sdfat_patches/ applied, see CMakeLists.txt), over an in-memory card that counts
// the FAT sectors each operation reads. On the device every one of those is a separate
// card command, ~1 ms or more over SPI: the 2,000-sector walks below are seconds.
//
// The layout tests mirror what the reader does: files rewritten through
// SDCardManager::openFileForWrite() (O_TRUNC, so their clusters are freed and allocated
// again) low on the card, gigabytes of books above them, new cache files at the top. The
// consistency tests check the FAT from the raw sectors, the way fsck does, after random
// rounds of create, grow, truncate, delete and remount on a nearly full card.
#include <FsLib/FsLib.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

constexpr uint64_t MB = 1ull << 20;
constexpr uint64_t GB = 1ull << 30;
constexpr uint32_t NO_HINT = 0xFFFFFFFF;

// Keeps only the sectors that are not all zero, so a 16 GB card of zero-filled books costs
// almost no memory.
class SparseCard final : public FsBlockDeviceInterface {
 public:
  explicit SparseCard(const Sector_t sectors) : sectors_(sectors) {}

  size_t fatReads = 0;
  Sector_t fatBegin = 0;  // both FAT copies, set once the card is formatted
  Sector_t fatEnd = 0;
  Sector_t failReadsOf = ~Sector_t(0);
  Sector_t failWritesOf = ~Sector_t(0);
  Sector_t countWritesOf = ~Sector_t(0);  // its writes are counted in countedWrites, failed ones too
  size_t countedWrites = 0;
  size_t writeCommands = 0;  // writeSector() and writeSectors() calls: one card command each on the device

  bool isBusy() override { return false; }
  bool readSector(const Sector_t sector, uint8_t* dst) override {
    if (sector >= fatBegin && sector < fatEnd) ++fatReads;
    if (sector == failReadsOf || sector >= sectors_) return false;
    peek(sector, dst);
    return true;
  }
  bool readSectors(const Sector_t sector, uint8_t* dst, const size_t count) override {
    for (size_t i = 0; i < count; ++i) {
      if (!readSector(sector + i, dst + 512 * i)) return false;
    }
    return true;
  }
  Sector_t sectorCount() override { return sectors_; }
  bool syncDevice() override { return true; }
  bool writeSector(const Sector_t sector, const uint8_t* src) override {
    ++writeCommands;
    return store(sector, src);
  }
  bool writeSectors(const Sector_t sector, const uint8_t* src, const size_t count) override {
    ++writeCommands;
    for (size_t i = 0; i < count; ++i) {
      if (!store(sector + i, src + 512 * i)) return false;
    }
    return true;
  }

  // Raw access for the checks, not counted.
  void peek(const Sector_t sector, uint8_t* dst) const {
    const auto it = data_.find(sector);
    if (it == data_.end()) {
      memset(dst, 0, 512);
    } else {
      memcpy(dst, it->second.data(), 512);
    }
  }
  void poke(const Sector_t sector, const uint8_t* src) { store(sector, src); }

 private:
  Sector_t sectors_;
  std::unordered_map<Sector_t, std::array<uint8_t, 512>> data_;

  bool store(const Sector_t sector, const uint8_t* src) {
    if (sector == countWritesOf) ++countedWrites;
    if (sector == failWritesOf || sector >= sectors_) return false;
    if (std::all_of(src, src + 512, [](const uint8_t b) { return b == 0; })) {
      data_.erase(sector);
    } else {
      memcpy(data_[sector].data(), src, 512);
    }
    return true;
  }
};

uint16_t le16(const uint8_t* p) { return p[0] | p[1] << 8; }
uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | static_cast<uint32_t>(p[3]) << 24; }
void putLe32(uint8_t* p, const uint32_t v) {
  for (int i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (8 * i));
}

// File contents: the first 8 bytes of every cluster-sized block of a file name the file and
// the block, the rest is zero, so a cluster handed to two files shows up as wrong data.
void stampData(uint8_t* buf, const uint64_t fileOffset, const size_t len, const uint32_t stamp,
               const uint32_t blockBytes) {
  memset(buf, 0, len);
  if (len == 0) return;
  const uint64_t firstBlock = fileOffset / blockBytes;
  const uint64_t lastBlock = (fileOffset + len - 1) / blockBytes;
  for (uint64_t block = firstBlock; block <= lastBlock; ++block) {
    uint8_t header[8];
    putLe32(header, stamp);
    putLe32(header + 4, static_cast<uint32_t>(block));
    for (uint32_t i = 0; i < 8; ++i) {
      const uint64_t at = block * blockBytes + i;
      if (at >= fileOffset && at < fileOffset + len) buf[at - fileOffset] = header[i];
    }
  }
}

class Card {
 public:
  explicit Card(const uint64_t bytes) : dev(static_cast<Sector_t>(bytes / 512)) {
    uint8_t buffer[512];
    FatFormatter formatter;
    EXPECT_TRUE(formatter.format(&dev, buffer, nullptr));
    readLayout();
    remount();
  }

  SparseCard dev;
  FsVolume vol;

  uint8_t fatType = 0;
  uint32_t bytesPerCluster = 0;
  uint32_t lastCluster = 0;

  void remount() {
    vol.end();
    ASSERT_TRUE(vol.begin(&dev));
  }

  // What a PC leaves in FAT32's FSInfo; SdFat itself never writes it after formatting.
  void setFsInfoNextFree(const uint32_t nextFree, const bool validSignature = true) {
    if (fatType != 32) return;
    uint8_t sector[512];
    dev.peek(partStart_ + fsInfoSector_, sector);
    putLe32(sector + 492, nextFree);
    putLe32(sector, validSignature ? FSINFO_LEAD_SIGNATURE : 0);
    dev.poke(partStart_ + fsInfoSector_, sector);
  }
  Sector_t fsInfoSector() const { return partStart_ + fsInfoSector_; }
  uint32_t fsInfoNextFree() const {
    uint8_t sector[512];
    dev.peek(fsInfoSector(), sector);
    return le32(sector + 492);
  }

  // As SDCardManager::openFileForWrite(): O_TRUNC frees the old clusters first.
  bool write(const char* path, const uint64_t size, const uint32_t stamp = 0) {
    FsFile file = vol.open(path, O_RDWR | O_CREAT | O_TRUNC);
    if (!file) return false;
    const bool ok = append(file, 0, size, stamp);
    return file.close() && ok;
  }
  bool append(FsFile& file, uint64_t offset, uint64_t size, const uint32_t stamp) {
    static uint8_t buffer[64 * 1024];
    while (size) {
      const size_t n = static_cast<size_t>(std::min<uint64_t>(size, sizeof(buffer)));
      stampData(buffer, offset, n, stamp, bytesPerCluster);
      if (file.write(buffer, n) != n) return false;
      offset += n;
      size -= n;
    }
    return true;
  }
  // A large file without writing its data, as books copied by a PC sit on the card.
  bool fill(const char* path, const uint64_t bytes) {
    FsFile file = vol.open(path, O_RDWR | O_CREAT | O_TRUNC);
    return file && file.preAllocate(bytes) && file.close();
  }
  size_t fatReadsToWrite(const char* path, const uint64_t size) {
    const size_t before = dev.fatReads;
    EXPECT_TRUE(write(path, size)) << path;
    return dev.fatReads - before;
  }
  uint32_t firstCluster(const char* path) {
    FsFile file = vol.open(path, O_RDONLY);
    return file ? file.firstCluster() : 0;
  }

  // The FAT entry of `cluster` in FAT copy 0 or 1, read from the raw sectors.
  uint32_t fatEntry(const uint32_t cluster, const int copy = 0) const {
    const uint32_t bytes = fatType == 32 ? 4 : 2;
    uint8_t sector[512];
    dev.peek(fatStart_ + copy * sectorsPerFat_ + cluster * bytes / 512, sector);
    const uint8_t* p = sector + cluster * bytes % 512;
    return fatType == 32 ? le32(p) & 0x0FFFFFFF : le16(p);
  }
  uint32_t rawFreeClusters() const {
    uint32_t free = 0;
    for (uint32_t c = 2; c <= lastCluster; ++c) free += fatEntry(c) == 0;
    return free;
  }

  // fsck from the raw sectors: every chain well formed and owned once, file sizes matching
  // their chains, no allocated cluster left unreferenced, both FAT copies equal, and SdFat's
  // free count right. Empty when the volume is consistent.
  std::string check() {
    std::string errors;
    owner_.assign(lastCluster + 1, false);
    if (fatType == 32) {
      followChain(rootCluster_, 0, true, "/", errors);
      walkDirCluster(rootCluster_, "/", errors);
    } else {
      walkDirSectors(rootDirSector_, rootDirSectors_, "/", errors);
    }
    for (uint32_t c = 2; c <= lastCluster; ++c) {
      if (fatEntry(c) != 0 && !owner_[c]) errors += "lost cluster " + std::to_string(c) + "\n";
    }
    for (uint32_t s = 0; s < sectorsPerFat_; ++s) {
      uint8_t a[512], b[512];
      dev.peek(fatStart_ + s, a);
      dev.peek(fatStart_ + sectorsPerFat_ + s, b);
      if (memcmp(a, b, 512) != 0) errors += "FAT copies differ in sector " + std::to_string(s) + "\n";
    }
    const int32_t sdfatFree = vol.freeClusterCount();
    if (sdfatFree != static_cast<int32_t>(rawFreeClusters())) {
      errors += "free count " + std::to_string(sdfatFree) + ", FAT has " + std::to_string(rawFreeClusters()) + "\n";
    }
    return errors;
  }

  // Every cluster-sized block of the file starts with the stamp it was written with.
  bool dataIntact(const char* path, const uint64_t size, const uint32_t stamp) {
    FsFile file = vol.open(path, O_RDONLY);
    if (!file || file.fileSize() != size) return false;
    for (uint64_t block = 0; block * bytesPerCluster < size; ++block) {
      const uint64_t at = block * bytesPerCluster;
      const size_t n = static_cast<size_t>(std::min<uint64_t>(8, size - at));
      uint8_t got[8], want[8];
      stampData(want, at, n, stamp, bytesPerCluster);
      if (!file.seekSet(at) || file.read(got, n) != static_cast<int>(n) || memcmp(got, want, n) != 0) return false;
    }
    return true;
  }

 private:
  Sector_t partStart_ = 0;
  uint16_t fsInfoSector_ = 0;
  Sector_t fatStart_ = 0;
  uint32_t sectorsPerFat_ = 0;
  Sector_t dataStart_ = 0;
  uint32_t sectorsPerCluster_ = 0;
  uint32_t rootCluster_ = 0;
  Sector_t rootDirSector_ = 0;
  uint32_t rootDirSectors_ = 0;
  std::vector<bool> owner_;

  void readLayout() {
    uint8_t mbr[512], pbs[512];
    dev.peek(0, mbr);
    partStart_ = le32(mbr + 446 + 8);
    dev.peek(partStart_, pbs);
    sectorsPerCluster_ = pbs[13];
    bytesPerCluster = sectorsPerCluster_ * 512;
    fatStart_ = partStart_ + le16(pbs + 14);
    const uint32_t rootEntries = le16(pbs + 17);
    sectorsPerFat_ = le16(pbs + 22) ? le16(pbs + 22) : le32(pbs + 36);
    const uint32_t totalSectors = le16(pbs + 19) ? le16(pbs + 19) : le32(pbs + 32);
    rootDirSector_ = fatStart_ + 2 * sectorsPerFat_;
    rootDirSectors_ = (rootEntries * 32 + 511) / 512;
    dataStart_ = rootDirSector_ + rootDirSectors_;
    const uint32_t clusters = (totalSectors - (dataStart_ - partStart_)) / sectorsPerCluster_;
    lastCluster = clusters + 1;
    fatType = clusters < 65525 ? 16 : 32;
    if (fatType == 32) {
      rootCluster_ = le32(pbs + 44);
      fsInfoSector_ = le16(pbs + 48);
    }
    dev.fatBegin = fatStart_;
    dev.fatEnd = fatStart_ + 2 * sectorsPerFat_;
  }
  bool isEoc(const uint32_t entry) const { return entry >= (fatType == 32 ? 0x0FFFFFF8u : 0xFFF8u); }
  Sector_t clusterSector(const uint32_t cluster) const { return dataStart_ + (cluster - 2) * sectorsPerCluster_; }

  // Marks the chain from `first`; returns its length in clusters.
  uint32_t followChain(const uint32_t first, const uint64_t size, const bool isDir, const std::string& path,
                       std::string& errors) {
    uint32_t length = 0;
    for (uint32_t c = first; c != 0;) {
      if (c < 2 || c > lastCluster) {
        errors += path + ": chain leaves the FAT at " + std::to_string(c) + "\n";
        break;
      }
      if (owner_[c]) {
        errors += path + ": cluster " + std::to_string(c) + " is cross-linked\n";
        break;
      }
      owner_[c] = true;
      ++length;
      const uint32_t next = fatEntry(c);
      if (next == 0) {
        errors += path + ": chain runs into free cluster " + std::to_string(c) + "\n";
        break;
      }
      if (isEoc(next)) break;
      c = next;
    }
    if (!isDir && length != (size + bytesPerCluster - 1) / bytesPerCluster) {
      errors += path + ": " + std::to_string(length) + " clusters for " + std::to_string(size) + " bytes\n";
    }
    return length;
  }
  // Returns false at the end-of-directory marker.
  bool walkDirSector(const Sector_t sector, const std::string& path, std::string& errors) {
    uint8_t buf[512];
    dev.peek(sector, buf);
    for (int i = 0; i < 16; ++i) {
      const uint8_t* e = buf + 32 * i;
      if (e[0] == 0) return false;
      const uint8_t attr = e[11];
      if (e[0] == 0xE5 || attr == 0x0F || (attr & 0x08) || e[0] == '.') continue;
      const uint32_t first = static_cast<uint32_t>(le16(e + 20)) << 16 | le16(e + 26);
      const uint32_t size = le32(e + 28);
      const std::string name = path + std::string(reinterpret_cast<const char*>(e), 11);
      if (attr & 0x10) {
        followChain(first, 0, true, name, errors);
        walkDirCluster(first, name + "/", errors);
      } else {
        followChain(first, size, false, name, errors);
      }
    }
    return true;
  }
  void walkDirSectors(const Sector_t first, const uint32_t count, const std::string& path, std::string& errors) {
    for (uint32_t s = 0; s < count; ++s) {
      if (!walkDirSector(first + s, path, errors)) return;
    }
  }
  void walkDirCluster(uint32_t cluster, const std::string& path, std::string& errors) {
    while (cluster >= 2 && cluster <= lastCluster) {
      for (uint32_t s = 0; s < sectorsPerCluster_; ++s) {
        if (!walkDirSector(clusterSector(cluster) + s, path, errors)) return;
      }
      const uint32_t next = fatEntry(cluster);
      if (next == 0 || isEoc(next)) return;
      cluster = next;
    }
  }
};

// A 16 GB FAT32 card (32 KB clusters, 4,094 FAT sectors per copy) half full: the reader's
// settings and a book's progress.bin low on the card, 8 GB of books copied above them.
void layOutHalfFullCard(Card& card) {
  ASSERT_EQ(card.fatType, 32);
  ASSERT_TRUE(card.vol.mkdir("/.crosspoint/epub_1/spines/0", true));
  ASSERT_TRUE(card.write("/.crosspoint/settings.json", 300));
  ASSERT_TRUE(card.write("/.crosspoint/epub_1/progress.bin", 16));
  ASSERT_TRUE(card.write("/old.json", 2));
  // FAT32 files stop short of 4 GB: two of them.
  ASSERT_TRUE(card.fill("/books1.epub", 4 * GB - MB));
  ASSERT_TRUE(card.fill("/books2.epub", 4 * GB - MB));
}

// A page turn rewrites progress.bin; the next new file was a section of the book. Before
// sdfat_patches/0004 every progress save pulled the free cluster search back down to
// progress.bin's cluster, and the section file after it read every FAT sector of the 8 GB
// above: 2,047 of them, each time.

// Clearing a cache or deleting a book below the free space did the same.

// Clusters freed below the search start are still found once everything above it is in use,
// both by a new file and by a file growing from its last cluster.

// SdFat 2.3.1 lowered the search start only for a cluster below it, not for the one at it:
// the cluster allocated last, freed again, stayed out of reach until the next mount. On a
// full card a page turn then truncated progress.bin and could not write it back.

// Every wake is a reboot and a fresh mount, and SdFat 2.3.1 started each mount's search at
// cluster 2, so the first new file after a wake read the FAT up to the free space. SdFat never
// writes FSInfo; a card a PC wrote last carries the PC's hint, behind which lies only what the
// reader has written since.

// FSInfo is advisory and other systems leave anything in it. Whatever the hint, a mount must
// still find every free cluster, below the hint as well, and never hand out a used one.

// An unreadable FSInfo sector leaves the search at cluster 2; the mount works as it did.

// A card only the reader writes (books over Wi-Fi or USB serial) carries no hint a PC left, or a
// stale one: until the reader kept FSInfo itself, the first new file after every wake read the FAT
// up to the free space.

// One FSInfo write for each FAT sector of clusters the search passes (128 on FAT32), not one per
// allocation, and only the hint changes: the signatures and the free count stay as they were.

// An FSInfo without its signatures is not FSInfo: never written.

// A sector that will not take the hint fails no file, and is tried once a mount.

// Random rounds on a nearly full card, remounting with good and garbage FSInfo hints in
// between; the FAT and every file's data are checked as they go.
void runRandomRounds(Card& card, const int rounds, const uint32_t seed, const uint32_t maxFileClusters) {
  std::mt19937 rng(seed);
  struct Model {
    uint64_t size;
    uint32_t stamp;
  };
  std::map<std::string, Model> files;
  const std::vector<std::string> dirs = {"/", "/.crosspoint/", "/.crosspoint/epub_1/", "/books/"};
  ASSERT_TRUE(card.vol.mkdir("/.crosspoint/epub_1", true));
  ASSERT_TRUE(card.vol.mkdir("/books", true));
  uint32_t nextStamp = 1;
  int fullEvents = 0;
  auto randomFile = [&]() { return dirs[rng() % dirs.size()] + "f" + std::to_string(rng() % 12) + ".bin"; };
  auto randomSize = [&]() -> uint64_t {
    switch (rng() % 3) {
      case 0:
        return rng() % 600;  // progress, settings
      case 1:
        return rng() % (4 * card.bytesPerCluster);
      default:
        return rng() % (static_cast<uint64_t>(maxFileClusters) * card.bytesPerCluster);
    }
  };
  auto expectFull = [&](const char* what) {
    ++fullEvents;
    EXPECT_EQ(card.rawFreeClusters(), 0u) << what << " failed with clusters free";
  };

  for (int round = 0; round < rounds; ++round) {
    const uint32_t op = rng() % 100;
    if (op < 35) {  // create or rewrite
      const std::string path = randomFile();
      const uint64_t size = randomSize();
      const uint32_t stamp = nextStamp++;
      if (card.write(path.c_str(), size, stamp)) {
        files[path] = {size, stamp};
      } else {
        expectFull("write");
        card.vol.remove(path.c_str());
        files.erase(path);
      }
    } else if (op < 55 && !files.empty()) {  // grow
      auto it = std::next(files.begin(), rng() % files.size());
      FsFile file = card.vol.open(it->first.c_str(), O_RDWR | O_APPEND);
      ASSERT_TRUE(file);
      const bool ok = card.append(file, it->second.size, randomSize(), it->second.stamp);
      it->second.size = file.fileSize();
      ASSERT_TRUE(file.close());
      if (!ok) {
        // SdFat keeps the clusters of a failed write in the chain past the file size.
        expectFull("append");
        ASSERT_TRUE(card.vol.remove(it->first.c_str()));
        files.erase(it);
      }
    } else if (op < 63 && !files.empty()) {  // truncate
      auto it = std::next(files.begin(), rng() % files.size());
      const uint64_t size = it->second.size ? rng() % it->second.size : 0;
      FsFile file = card.vol.open(it->first.c_str(), O_RDWR);
      ASSERT_TRUE(file && file.truncate(size) && file.close());
      it->second.size = size;
    } else if (op < 73 && !files.empty()) {  // delete
      auto it = std::next(files.begin(), rng() % files.size());
      ASSERT_TRUE(card.vol.remove(it->first.c_str()));
      files.erase(it);
    } else if (op < 85) {  // wake: remount, FSInfo as the reader or some other system left it
      switch (rng() % 7) {
        case 0:
          card.setFsInfoNextFree(NO_HINT);
          break;
        case 1:
          card.setFsInfoNextFree(2 + rng() % (card.lastCluster - 1));
          break;
        case 2:
          card.setFsInfoNextFree(card.lastCluster);
          break;
        case 3:
          card.setFsInfoNextFree(card.lastCluster + 1 + rng() % 4);
          break;
        case 4:
          card.setFsInfoNextFree(rng());
          break;
        case 5:
          break;  // as the reader kept it
        default:
          card.setFsInfoNextFree(2 + rng() % (card.lastCluster - 1), false);
          break;
      }
      card.remount();
    } else {  // the reader's page turn: progress.bin rewritten through O_TRUNC
      const uint32_t stamp = nextStamp++;
      if (card.write("/.crosspoint/epub_1/progress.bin", 16, stamp)) {
        files["/.crosspoint/epub_1/progress.bin"] = {16, stamp};
      } else {
        expectFull("progress");
        card.vol.remove("/.crosspoint/epub_1/progress.bin");
        files.erase("/.crosspoint/epub_1/progress.bin");
      }
    }
    if (round % 20 == 19 || round == rounds - 1) {
      ASSERT_EQ(card.check(), "") << "round " << round;
      for (const auto& [path, model] : files) {
        ASSERT_TRUE(card.dataIntact(path.c_str(), model.size, model.stamp)) << path << " round " << round;
      }
    }
  }
  // The rounds must have run the card full, or the wrap was never exercised.
  EXPECT_GT(fullEvents, 5);
}



// A new folder's cluster is zeroed whatever its sectors held, eight sectors a write command rather than one
// (sdfat_patches/0005 and 0006). On the device each command is a card write of ~1 ms: on the X4 Pro's exFAT card a
// first open's two folders took 298 and 636 ms of single-sector writes (2026-11-15).
//
// The clusters the folder may take, past the last one allocated (the free cluster search moves forward), are first
// filled with junk, as a deleted book leaves them. Returns the write commands the mkdir took; checks the cluster came
// out zero past its first `keepBytes` (FAT's "." and ".." entries) and lists empty.
size_t mkdirOverJunk(SparseCard& dev, FsVolume& vol, const uint32_t keepBytes) {
  FsFile probe = vol.open("/probe.bin", O_RDWR | O_CREAT | O_TRUNC);
  EXPECT_TRUE(probe && probe.write("x", 1) == 1);
  const uint32_t probeCluster = probe.firstCluster();
  EXPECT_TRUE(probe.close());
  const uint32_t spc = static_cast<uint32_t>(vol.sectorsPerCluster());
  const auto clusterSector = [&](const uint32_t cluster) { return vol.dataStartSector() + (cluster - 2) * spc; };
  uint8_t junk[512];
  memset(junk, 0xA5, sizeof(junk));
  for (uint32_t c = probeCluster + 1; c <= probeCluster + 8; ++c) {
    for (uint32_t s = 0; s < spc; ++s) dev.poke(clusterSector(c) + s, junk);
  }

  const size_t before = dev.writeCommands;
  EXPECT_TRUE(vol.mkdir("/new"));
  const size_t commands = dev.writeCommands - before;

  FsFile dir = vol.open("/new", O_RDONLY);
  EXPECT_TRUE(dir && dir.isDir());
  const uint32_t cluster = dir.firstCluster();
  EXPECT_GT(cluster, probeCluster);
  EXPECT_LE(cluster, probeCluster + 8) << "the folder must take a cluster that held junk";
  uint8_t sector[512];
  for (uint32_t s = 0; s < spc; ++s) {
    dev.peek(clusterSector(cluster) + s, sector);
    const uint32_t from = s == 0 ? keepBytes : 0;
    EXPECT_TRUE(std::all_of(sector + from, sector + 512, [](const uint8_t b) { return b == 0; }))
        << "sector " << s << " of the new folder's cluster";
  }
  FsFile entry;
  EXPECT_FALSE(entry.openNext(&dir, O_RDONLY)) << "the new folder lists empty";
  return commands;
}

TEST(SdFatDirCluster, ANewFolderIsZeroedEightSectorsAWriteOnFat32) {
  Card card(16 * GB);  // 32 KB clusters: 64 sectors
  ASSERT_EQ(card.fatType, 32);
  const size_t commands = mkdirOverJunk(card.dev, card.vol, /*keepBytes=*/64);
  // Sector by sector the cluster alone was 64 commands.
  EXPECT_LE(commands, 64u / 8 + 8) << commands << " write commands";
  EXPECT_EQ(card.check(), "");
}

TEST(SdFatDirCluster, ANewFolderIsZeroedEightSectorsAWriteOnExFat) {
  SparseCard dev(static_cast<Sector_t>(GB / 512));
  uint8_t buffer[512];
  ExFatFormatter formatter;
  ASSERT_TRUE(formatter.format(&dev, buffer, nullptr));
  FsVolume vol;
  ASSERT_TRUE(vol.begin(&dev));
  ASSERT_EQ(vol.fatType(), FAT_TYPE_EXFAT);
  ASSERT_EQ(vol.sectorsPerCluster(), 256u);  // 128 KB clusters, as on the X4 Pro's card
  const size_t commands = mkdirOverJunk(dev, vol, /*keepBytes=*/0);
  // Sector by sector the cluster alone was 256 commands.
  EXPECT_LE(commands, 256u / 8 + 8) << commands << " write commands";
  // The folder survives a remount: it is found and still lists empty.
  vol.end();
  ASSERT_TRUE(vol.begin(&dev));
  FsFile dir = vol.open("/new", O_RDONLY);
  ASSERT_TRUE(dir && dir.isDir());
  FsFile entry;
  EXPECT_FALSE(entry.openNext(&dir, O_RDONLY));
}

}  // namespace
