# SDL3 — вікно, ввід і GPU-абстракція (Metal на macOS, Vulkan/D3D12 деінде).
#
# Спочатку пробуємо готовий SDL3 у системі (brew/vcpkg) — це швидко для
# щоденної роботи. Якщо його нема, тягнемо й збираємо фіксовану версію, щоб
# збірка була відтворюваною на чистій машині та в CI.
set(OBF2_SDL3_VERSION "3.4.14" CACHE STRING "Версія SDL3 для FetchContent")

find_package(SDL3 CONFIG QUIET)

if(SDL3_FOUND)
  message(STATUS "SDL3: системний ${SDL3_VERSION}")
else()
  message(STATUS "SDL3: збираємо release-${OBF2_SDL3_VERSION} через FetchContent")
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
  set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-${OBF2_SDL3_VERSION}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()
