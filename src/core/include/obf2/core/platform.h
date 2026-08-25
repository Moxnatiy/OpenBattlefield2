#pragma once
// Визначення платформи/архітектури. Ціль: arm64 macOS та x86_64 Windows.
// Ніякого платформозалежного коду поза цим заголовком і src/platform/.

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
  #error "OpenBattlefield2: непідтримувана платформа"
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
  #define OBF2_ARCH_ARM64 1
  #define OBF2_ARCH_NAME "arm64"
#elif defined(__x86_64__) || defined(_M_X64)
  #define OBF2_ARCH_X64 1
  #define OBF2_ARCH_NAME "x86_64"
#else
  #error "OpenBattlefield2: непідтримувана архітектура"
#endif

// Файли BF2 — little-endian. Обидві наші цілі теж LE, тому свопи не потрібні;
// якщо колись з'явиться BE-платформа, тут воно зламається голосно й одразу.
static_assert(sizeof(void*) == 8, "потрібна 64-бітна збірка");

namespace obf2 {

// Шляхи всередині ассетів BF2 регістронезалежні й з зворотними слешами
// (Windows-звичка 2005 року). На APFS/NTFS це майже завжди неважливо, але
// на case-sensitive томах — критично, тому нормалізація обов'язкова всюди.
inline constexpr bool kAssetPathsAreCaseInsensitive = true;

}  // namespace obf2
