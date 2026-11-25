#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

#include "check.h"
#include "miniz.h"

namespace {

void writeLE(std::span<unsigned char> field, std::uint64_t value) {
  for (auto& byte : field) {
    byte = static_cast<unsigned char>(value);
    value >>= 8;
  }
}

void checkOffsetOnlyZip64() {
  constexpr std::string_view name = "asset";
  constexpr std::string_view contents = "test";
  mz_zip_archive writer{};
  if (!mz_zip_writer_init_heap(&writer, 0, 0)) {
    CHECK(false);
    return;
  }
  void* data = nullptr;
  size_t size = 0;
  const bool written = mz_zip_writer_add_mem(&writer, name.data(), contents.data(),
                                            contents.size(), MZ_NO_COMPRESSION) &&
                       mz_zip_writer_finalize_heap_archive(&writer, &data, &size);
  mz_zip_writer_end(&writer);
  CHECK(written);
  if (!written) return;
  const auto* start = static_cast<const unsigned char*>(data);
  std::vector<unsigned char> bytes(start, start + size);
  mz_free(data);

  // APPNOTE 4.3.12, 4.3.16 and 4.5.3. Add a ZIP64 extra field for just
  // the local header offset while retaining the entry's 32-bit sizes.
  constexpr size_t endSize = 22;
  constexpr size_t directoryHeaderSize = 46;
  constexpr size_t extraSize = 12;
  const size_t directorySize = directoryHeaderSize + name.size();
  const size_t directoryStart = bytes.size() - endSize - directorySize;
  const size_t extraStart = bytes.size() - endSize;
  bytes.insert(bytes.begin() + extraStart, extraSize, 0);
  std::span<unsigned char> zip(bytes);
  writeLE(zip.subspan(directoryStart + 30, 2), extraSize);
  writeLE(zip.subspan(directoryStart + 42, 4), UINT32_MAX);
  writeLE(zip.subspan(extraStart, 2), 1);
  writeLE(zip.subspan(extraStart + 2, 2), 8);
  writeLE(zip.subspan(extraStart + extraSize + 12, 4), directorySize + extraSize);

  mz_zip_archive reader{};
  const bool opened = mz_zip_reader_init_mem(&reader, bytes.data(), bytes.size(), 0);
  CHECK(opened);
  if (opened) {
    char extracted[contents.size()]{};
    CHECK(mz_zip_reader_extract_to_mem(&reader, 0, extracted, sizeof(extracted), 0));
    CHECK_EQ(std::string_view(extracted, sizeof(extracted)), contents);
    mz_zip_reader_end(&reader);
  }

  writeLE(zip.subspan(extraStart + 4, 8), bytes.size());
  mz_zip_zero_struct(&reader);
  CHECK(!mz_zip_reader_init_mem(&reader, bytes.data(), bytes.size(), 0));
  if (reader.m_pState) mz_zip_reader_end(&reader);
}

}  // namespace

TEST_MAIN(checkOffsetOnlyZip64())
