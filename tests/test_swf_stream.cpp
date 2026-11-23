#include <array>

#include "check.h"
#include "base/tu_file.h"

static void testMemoryInput() {
  std::array<char, 4> header{'F', 'W', 'S', 6};
  std::array<char, 4> result{};
  tu_file movie(tu_file::memory_buffer, static_cast<int>(header.size()), header.data());

  CHECK_EQ(movie.get_position(), 0);
  CHECK_EQ(movie.write_bytes("X", 1), 0);
  CHECK_EQ(movie.get_position(), 0);
  CHECK_EQ(movie.read_bytes(result.data(), 3), 3);
  CHECK_EQ(movie.get_position(), 3);
  CHECK_EQ(movie.read_bytes(result.data() + 3, 1), 1);
  CHECK(result == header);
  CHECK(movie.get_eof());
  movie.set_position(1);
  CHECK_EQ(movie.read_bytes(result.data(), 1), 1);
  CHECK_EQ(result[0], 'W');
}

static void testGrowingMemoryStream() {
  tu_file stream(tu_file::memory_buffer);
  CHECK_EQ(stream.get_position(), 0);
  CHECK_EQ(stream.write_bytes("FWS", 3), 3);
  CHECK_EQ(stream.get_position(), 3);
  stream.set_position(0);
  std::array<char, 4> output{};
  CHECK_EQ(stream.read_bytes(output.data(), static_cast<int>(output.size())), 3);
  CHECK_EQ(output[2], 'S');
  CHECK(stream.get_eof());
}

TEST_MAIN({
  testMemoryInput();
  testGrowingMemoryStream();
})
