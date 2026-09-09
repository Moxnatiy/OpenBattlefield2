#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "obf2/con/interpreter.h"

namespace obf2 {

// A zip archive of the game (Objects_server.zip, Common_client.zip, ...) — read
// in place, nothing is unpacked to disk. The index is built once on open, and
// the keys are normalised paths.
class ZipArchive {
 public:
  ~ZipArchive();
  ZipArchive(ZipArchive&&) noexcept;
  ZipArchive& operator=(ZipArchive&&) noexcept;
  ZipArchive(const ZipArchive&) = delete;
  ZipArchive& operator=(const ZipArchive&) = delete;

  static std::unique_ptr<ZipArchive> open(const std::filesystem::path& file, std::string* error = nullptr);

  bool contains(std::string_view normalizedPath) const;
  std::optional<std::vector<std::byte>> read(std::string_view normalizedPath) const;
  std::vector<std::string> entries() const;
  const std::filesystem::path& file() const { return file_; }

 private:
  ZipArchive();
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::filesystem::path file_;
};

// The engine's virtual file system.
//
// Reproduces the behaviour of fileManager in Refractor 2: several mount points,
// each a directory on disk or a zip. The search goes from the LATEST mounted to
// the earliest, so loose files in the mod's directory override the archives'
// contents — the whole of BF2 modding rests on that.
class FileSystem : public con::FileProvider {
 public:
  // mountPoint is the prefix inside the VFS ("Objects" for Objects_server.zip).
  bool mountDirectory(const std::filesystem::path& dir, std::string_view mountPoint = {});
  bool mountArchive(const std::filesystem::path& zip, std::string_view mountPoint = {},
                    std::string* error = nullptr);

  bool exists(std::string_view path) const;
  std::optional<std::vector<std::byte>> read(std::string_view path) const;
  std::optional<std::string> loadText(std::string_view normalizedPath) override;
  std::vector<std::string> list(std::string_view prefix = {}) const;

  std::size_t mountCount() const { return mounts_.size(); }

 private:
  struct Mount {
    std::string mountPoint;                 // normalised, may be empty
    std::filesystem::path realDir;          // for a directory
    std::unique_ptr<ZipArchive> archive;    // for an archive
    std::vector<std::pair<std::string, std::filesystem::path>> diskIndex;
  };
  std::vector<Mount> mounts_;
};

// Runs ClientArchives.con / ServerArchives.con and mounts everything listed there.
// The command looks like this: `fileManager.mountArchive Objects_server.zip Objects`.
// Returns the number of archives mounted successfully.
int mountArchivesFromCon(FileSystem& fs, const std::filesystem::path& modDir,
                         const std::filesystem::path& archivesCon,
                         std::vector<std::string>* errors = nullptr);

}  // namespace obf2
