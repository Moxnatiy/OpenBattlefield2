# SDL3 — the window, input and the GPU abstraction (Metal on macOS, Vulkan/D3D12 elsewhere).
#
# First we try an SDL3 already in the system (brew/vcpkg) — that is quick for
# everyday work. If there is none, we fetch and build a fixed version so the build
# is reproducible on a clean machine and in CI.
set(OBF2_SDL3_VERSION "3.4.14" CACHE STRING "The SDL3 version for FetchContent")

find_package(SDL3 CONFIG QUIET)

if(SDL3_FOUND)
  message(STATUS "SDL3: the system's ${SDL3_VERSION}")
else()
  message(STATUS "SDL3: building release-${OBF2_SDL3_VERSION} through FetchContent")
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
