#include "obf2/vfs/filesystem.h"

#include <cstring>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "miniz.h"
#include "obf2/core/path.h"

namespace fs = std::filesystem;

namespace obf2 {

// --- ZipArchive --------------------------------------------------------------

// One miniz reader over the archive. Extracting seeks the file it was opened
// with and keeps its state inside `mz_zip_archive`, so two threads cannot share
// one — each takes a reader of its own out of the pool below and puts it back.
struct ZipReader {
  mz_zip_archive zip{};
  bool opened = false;

  ~ZipReader() {
    if (opened) mz_zip_reader_end(&zip);
  }
};

struct ZipArchive::Impl {
  std::filesystem::path file;
  std::unordered_map<std::string, mz_uint> index;  // normalised path -> entry number

  // The idle readers. One is enough while the loading is serial; a level loaded
  // over eight threads ends up with eight, each an open file handle and a few
  // kilobytes of miniz state.
  mutable std::mutex mutex;
  mutable std::vector<std::unique_ptr<ZipReader>> idle;

  std::unique_ptr<ZipReader> take() const {
    {
      std::lock_guard<std::mutex> lock(mutex);
      if (!idle.empty()) {
        auto reader = std::move(idle.back());
        idle.pop_back();
        return reader;
      }
    }
    auto reader = std::make_unique<ZipReader>();
    if (!mz_zip_reader_init_file(&reader->zip, file.string().c_str(), 0)) return nullptr;
    reader->opened = true;
    return reader;
  }

  void give(std::unique_ptr<ZipReader> reader) const {
    if (!reader) return;
    std::lock_guard<std::mutex> lock(mutex);
    idle.push_back(std::move(reader));
  }
};

ZipArchive::ZipArchive() : impl_(std::make_unique<Impl>()) {}
ZipArchive::~ZipArchive() = default;
ZipArchive::ZipArchive(ZipArchive&&) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&&) noexcept = default;

std::unique_ptr<ZipArchive> ZipArchive::open(const fs::path& file, std::string* error) {
  auto archive = std::unique_ptr<ZipArchive>(new ZipArchive());
  archive->file_ = file;
  archive->impl_->file = file;

  auto reader = archive->impl_->take();
  if (!reader) {
    if (error) *error = "could not open the archive: " + file.string();
    return nullptr;
  }

  const mz_uint count = mz_zip_reader_get_num_files(&reader->zip);
  archive->impl_->index.reserve(count);
  for (mz_uint i = 0; i < count; ++i) {
    if (mz_zip_reader_is_file_a_directory(&reader->zip, i)) continue;
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&reader->zip, i, &stat)) continue;
    archive->impl_->index.emplace(normalizeAssetPath(stat.m_filename), i);
  }
  archive->impl_->give(std::move(reader));
  return archive;
}

bool ZipArchive::contains(std::string_view normalizedPath) const {
  return impl_->index.find(std::string(normalizedPath)) != impl_->index.end();
}

std::optional<std::vector<std::byte>> ZipArchive::read(std::string_view normalizedPath) const {
  const auto it = impl_->index.find(std::string(normalizedPath));
  if (it == impl_->index.end()) return std::nullopt;

  // The index is written once, on open, and only read after that; the reader is
  // this thread's alone until it is given back. So several threads may unpack
  // from one archive at once, which is the whole point — inflating a level's
  // meshes and textures is most of what loading is.
  auto reader = impl_->take();
  if (!reader) return std::nullopt;

  std::size_t size = 0;
  void* data = mz_zip_reader_extract_to_heap(&reader->zip, it->second, &size, 0);
  impl_->give(std::move(reader));
  if (data == nullptr) return std::nullopt;

  std::vector<std::byte> out(size);
  std::memcpy(out.data(), data, size);
  mz_free(data);
  return out;
}

std::vector<std::string> ZipArchive::entries() const {
  std::vector<std::string> out;
  out.reserve(impl_->index.size());
  for (const auto& [path, _] : impl_->index) out.push_back(path);
  return out;
}

// --- FileSystem --------------------------------------------------------------

namespace {

std::optional<std::vector<std::byte>> readWholeFile(const fs::path& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) return std::nullopt;
  const std::streamsize size = in.tellg();
  if (size < 0) return std::nullopt;
  in.seekg(0, std::ios::beg);
  std::vector<std::byte> data(static_cast<std::size_t>(size));
  if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size)) return std::nullopt;
  return data;
}

// The key we look up in a mount: if there is a mount point, the path has to start
// with it, and it is stripped.
std::optional<std::string> stripMountPoint(std::string_view mountPoint, std::string_view path) {
  if (mountPoint.empty()) return std::string(path);
  if (path.size() <= mountPoint.size()) return std::nullopt;
  if (path.compare(0, mountPoint.size(), mountPoint) != 0) return std::nullopt;
  if (path[mountPoint.size()] != '/') return std::nullopt;
  return std::string(path.substr(mountPoint.size() + 1));
}

}  // namespace

bool FileSystem::mountDirectory(const fs::path& dir, std::string_view mountPoint) {
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) return false;

  Mount mount;
  mount.mountPoint = normalizeAssetPath(mountPoint);
  mount.realDir = dir;

  // Indexed once: asset paths are case-insensitive, while the user's volume may
  // be case-sensitive, so the OS cannot be relied on.
  for (auto it = fs::recursive_directory_iterator(
           dir, fs::directory_options::skip_permission_denied, ec);
       it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (ec) { ec.clear(); continue; }
    if (!it->is_regular_file(ec)) continue;
    const fs::path rel = fs::relative(it->path(), dir, ec);
    if (ec) { ec.clear(); continue; }
    mount.diskIndex.emplace_back(normalizeAssetPath(rel.generic_string()), it->path());
  }

  mounts_.push_back(std::move(mount));
  return true;
}

bool FileSystem::mountArchive(const fs::path& zip, std::string_view mountPoint, std::string* error) {
  auto archive = ZipArchive::open(zip, error);
  if (!archive) return false;

  Mount mount;
  mount.mountPoint = normalizeAssetPath(mountPoint);
  mount.archive = std::move(archive);
  mounts_.push_back(std::move(mount));
  return true;
}

std::optional<std::vector<std::byte>> FileSystem::read(std::string_view path) const {
  const std::string normalized = normalizeAssetPath(path);

  // From the latest mount to the earliest: later overrides earlier.
  for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
    const auto key = stripMountPoint(it->mountPoint, normalized);
    if (!key) continue;

    if (it->archive) {
      if (auto data = it->archive->read(*key)) return data;
      continue;
    }
    for (const auto& [indexed, real] : it->diskIndex) {
      if (indexed == *key) {
        if (auto data = readWholeFile(real)) return data;
      }
    }
  }
  return std::nullopt;
}

bool FileSystem::exists(std::string_view path) const {
  // Asking whether a file is there must not unpack it. It used to call `read`,
  // and a level asks this of every patch's colour map, light map and two chart
  // maps before it reads any of them — 64 megabytes of inflate to answer 64
  // yes-or-no questions.
  const std::string normalized = normalizeAssetPath(path);
  for (auto it = mounts_.rbegin(); it != mounts_.rend(); ++it) {
    const auto key = stripMountPoint(it->mountPoint, normalized);
    if (!key) continue;
    if (it->archive) {
      if (it->archive->contains(*key)) return true;
      continue;
    }
    for (const auto& [indexed, real] : it->diskIndex) {
      std::error_code ec;
      if (indexed == *key && fs::is_regular_file(real, ec)) return true;
    }
  }
  return false;
}

std::optional<std::string> FileSystem::loadText(std::string_view normalizedPath) {
  auto data = read(normalizedPath);
  if (!data) return std::nullopt;
  return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

std::vector<std::string> FileSystem::list(std::string_view prefix) const {
  const std::string normalizedPrefix = normalizeAssetPath(prefix);
  std::vector<std::string> out;

  auto consider = [&](const std::string& key, const std::string& mountPoint) {
    std::string full = mountPoint.empty() ? key : mountPoint + "/" + key;
    if (normalizedPrefix.empty() || full.compare(0, normalizedPrefix.size(), normalizedPrefix) == 0) {
      out.push_back(std::move(full));
    }
  };

  for (const auto& mount : mounts_) {
    if (mount.archive) {
      for (auto& entry : mount.archive->entries()) consider(entry, mount.mountPoint);
    } else {
      for (const auto& [key, _] : mount.diskIndex) consider(key, mount.mountPoint);
    }
  }
  return out;
}

// --- loading the archive list from a .con -----------------------------------

int mountArchivesFromCon(FileSystem& target, const fs::path& modDir, const fs::path& archivesCon,
                         std::vector<std::string>* errors) {
  auto text = readWholeFile(archivesCon);
  if (!text) {
    if (errors) errors->push_back("cannot read " + archivesCon.string());
    return 0;
  }
  const std::string source(reinterpret_cast<const char*>(text->data()), text->size());

  // The archive list is itself a .con, so we run it with the same interpreter.
  // It includes no files, so the provider here returns nothing.
  struct NullProvider : con::FileProvider {
    std::optional<std::string> loadText(std::string_view) override { return std::nullopt; }
  } nullProvider;

  int mounted = 0;
  con::Interpreter interp(
      nullProvider,
      [&](const con::Command& cmd) {
        if (cmd.lowerPath != "filemanager.mountarchive" || cmd.args.empty()) return;
        const fs::path zip = modDir / cmd.args[0];
        const std::string_view mountPoint = cmd.argStr(1);
        std::string error;
        if (target.mountArchive(zip, mountPoint, &error)) {
          ++mounted;
        } else if (errors) {
          errors->push_back(error);
        }
      },
      [&](const con::Diagnostic& d) {
        if (errors) errors->push_back(d.file + ":" + std::to_string(d.line) + ": " + d.message);
      });

  interp.runText(source, archivesCon.filename().string());
  return mounted;
}

}  // namespace obf2
