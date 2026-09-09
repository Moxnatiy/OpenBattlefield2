#pragma once
// Platform/architecture detection. Targets: arm64 macOS and x86_64 Windows.
// No platform-specific code outside this header and src/platform/.

#include <cstdint>

#if defined(_WIN32)
  #define OBF2_PLATFORM_WINDOWS 1
  #define OBF2_PLATFORM_NAME "windows"
#elif defined(__APPLE__)
  #define OBF2_PLATFORM_MACOS 1
  #define OBF2_PLATFORM_NAME "macos"
#elif defined(__linux__)
  #define OBF2_PLATFORM_LINUX 1
  #define OBF2_PLATFORM_NAME "linux"
#else
  #error "OpenBattlefield2: unsupported platform"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
  #define OBF2_ARCH_ARM64 1
  #define OBF2_ARCH_NAME "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
  #define OBF2_ARCH_X64 1
  #define OBF2_ARCH_NAME "x86_64"
#else
  #error "OpenBattlefield2: unsupported architecture"
#endif

// BF2's files are little-endian. Both our targets are LE too, so no swaps are
// needed; should a BE platform ever appear, this breaks loudly and at once.
static_assert(sizeof(void*) == 8, "a 64-bit build is required");

namespace obf2 {

// Paths inside BF2's assets are case-insensitive and use backslashes (a 2005
// Windows habit). On APFS/NTFS that almost never matters, but on
// case-sensitive volumes it is critical, so normalisation is mandatory.
inline constexpr bool kAssetPathsAreCaseInsensitive = true;

}  // namespace obf2
