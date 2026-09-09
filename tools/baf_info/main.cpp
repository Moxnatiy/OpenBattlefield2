// baf_info — what is inside a `.baf` animation.
//
//   baf_info <modDir> <path/to/file.baf> [frame]
#include <cstdio>
#include <cstdlib>
#include <string>

#include "obf2/mesh/animation.h"
#include "obf2/vfs/filesystem.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fputs("usage: baf_info <modDir> <file.baf> [frame]\n", stderr);
    return 2;
  }

  obf2::FileSystem files;
  for (const char* list : {"ServerArchives.con", "ClientArchives.con"}) {
    obf2::mountArchivesFromCon(files, argv[1], std::filesystem::path(argv[1]) / list);
  }
  files.mountDirectory(argv[1]);

  const auto bytes = files.read(argv[2]);
  if (!bytes) {
    std::fprintf(stderr, "not found: %s\n", argv[2]);
    return 1;
  }

  std::string error;
  const auto animation = obf2::mesh::loadBoneAnimation(*bytes, &error);
  if (!animation) {
    std::fprintf(stderr, "not parsed: %s\n", error.c_str());
    return 1;
  }

  std::printf("version %u, %zu bones, %u frames, precision %u, %.2f s long\n",
              animation->version, animation->boneIds.size(), animation->frameCount,
              animation->precision, animation->duration());

  const std::uint32_t frame =
      argc > 3 ? static_cast<std::uint32_t>(std::atoi(argv[3])) : 0;
  std::printf("frame %u:\n", frame);
  for (std::size_t i = 0; i < animation->tracks.size() && i < 12; ++i) {
    float rotation[4];
    obf2::mesh::Vec3 position;
    animation->sample(i, frame, rotation, &position);
    std::printf("  bone %3u  rotation %6.3f %6.3f %6.3f %6.3f  offset %7.3f %7.3f %7.3f\n",
                animation->boneIds[i], rotation[0], rotation[1], rotation[2], rotation[3],
                position.x, position.y, position.z);
  }
  return 0;
}
