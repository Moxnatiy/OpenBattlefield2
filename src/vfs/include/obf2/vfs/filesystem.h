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

// Zip-архів гри (Objects_server.zip, Common_client.zip, ...) — читаємо як є,
// нічого не розпаковуючи на диск. Індекс будується один раз при відкритті,
// ключі — нормалізовані шляхи.
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

// Віртуальна файлова система рушія.
//
// Повторює поведінку fileManager у Refractor 2: кілька точок монтування, кожна
// з яких — тека на диску або zip. Пошук іде від НАЙПІЗНІШЕ змонтованого до
// найранішого, тому вільні файли в теці моду перекривають вміст архівів — саме
// на цьому тримається весь модінг BF2.
class FileSystem : public con::FileProvider {
 public:
  // mountPoint — префікс усередині VFS ("Objects" для Objects_server.zip).
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
    std::string mountPoint;                 // нормалізований, може бути порожнім
    std::filesystem::path realDir;          // для теки
    std::unique_ptr<ZipArchive> archive;    // для архіву
    std::vector<std::pair<std::string, std::filesystem::path>> diskIndex;
  };
  std::vector<Mount> mounts_;
};

// Виконує ClientArchives.con / ServerArchives.con і монтує все, що там указано.
// Команда виглядає так: `fileManager.mountArchive Objects_server.zip Objects`.
// Повертає кількість успішно змонтованих архівів.
int mountArchivesFromCon(FileSystem& fs, const std::filesystem::path& modDir,
                         const std::filesystem::path& archivesCon,
                         std::vector<std::string>* errors = nullptr);

}  // namespace obf2
