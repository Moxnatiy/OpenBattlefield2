#include "obf2/vfs/filesystem.h"

#include <cstring>
#include <fstream>
#include <unordered_map>

#include "miniz.h"
#include "obf2/core/path.h"

namespace fs = std::filesystem;

namespace obf2 {

// --- ZipArchive --------------------------------------------------------------

struct ZipArchive::Impl {
  mz_zip_archive zip{};
  bool opened = false;
  std::unordered_map<std::string, mz_uint> index;  // нормалізований шлях -> номер запису

  ~Impl() {
    if (opened) mz_zip_reader_end(&zip);
  }
};

ZipArchive::ZipArchive() : impl_(std::make_unique<Impl>()) {}
ZipArchive::~ZipArchive() = default;
ZipArchive::ZipArchive(ZipArchive&&) noexcept = default;
ZipArchive& ZipArchive::operator=(ZipArchive&&) noexcept = default;

std::unique_ptr<ZipArchive> ZipArchive::open(const fs::path& file, std::string* error) {
  auto archive = std::unique_ptr<ZipArchive>(new ZipArchive());
  archive->file_ = file;

  if (!mz_zip_reader_init_file(&archive->impl_->zip, file.string().c_str(), 0)) {
    if (error) *error = "не вдалося відкрити архів: " + file.string();
    return nullptr;
  }
  archive->impl_->opened = true;

  const mz_uint count = mz_zip_reader_get_num_files(&archive->impl_->zip);
  archive->impl_->index.reserve(count);
  for (mz_uint i = 0; i < count; ++i) {
    if (mz_zip_reader_is_file_a_directory(&archive->impl_->zip, i)) continue;
    mz_zip_archive_file_stat stat;
    if (!mz_zip_reader_file_stat(&archive->impl_->zip, i, &stat)) continue;
    archive->impl_->index.emplace(normalizeAssetPath(stat.m_filename), i);
  }
  return archive;
}

bool ZipArchive::contains(std::string_view normalizedPath) const {
  return impl_->index.find(std::string(normalizedPath)) != impl_->index.end();
}

std::optional<std::vector<std::byte>> ZipArchive::read(std::string_view normalizedPath) const {
  const auto it = impl_->index.find(std::string(normalizedPath));
  if (it == impl_->index.end()) return std::nullopt;

  std::size_t size = 0;
  void* data = mz_zip_reader_extract_to_heap(&impl_->zip, it->second, &size, 0);
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

// Ключ, за яким шукаємо у монтуванні: якщо є точка монтування, шлях має з неї
// починатися, і вона відрізається.
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

  // Індексуємо один раз: шляхи в ассетах регістронезалежні, а том користувача
  // може бути case-sensitive, тож покладатися на ОС не можна.
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

  // Від найпізнішого монтування до найранішого: пізніше перекриває раніше.
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

bool FileSystem::exists(std::string_view path) const { return read(path).has_value(); }

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

// --- завантаження списку архівів з .con --------------------------------------

int mountArchivesFromCon(FileSystem& target, const fs::path& modDir, const fs::path& archivesCon,
                         std::vector<std::string>* errors) {
  auto text = readWholeFile(archivesCon);
  if (!text) {
    if (errors) errors->push_back("не читається " + archivesCon.string());
    return 0;
  }
  const std::string source(reinterpret_cast<const char*>(text->data()), text->size());

  // Сам список архівів — теж .con, тому виконуємо його тим самим інтерпретатором.
  // Файлів він не підключає, тож провайдер тут нічого не віддає.
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
