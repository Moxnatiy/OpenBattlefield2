cmake_minimum_required(VERSION 3.24)
project(OpenBattlefield2 VERSION 0.1.0 LANGUAGES C CXX)

# --- Мультиплатформа: arm64 macOS + x86_64 Windows ---------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)              # без GNU-розширень: MSVC їх не має
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

option(OBF2_WERROR "Тлумачити попередження як помилки" OFF)
option(OBF2_BUILD_TESTS "Збирати тести" ON)
option(OBF2_BUILD_TOOLS "Збирати CLI-утиліти" ON)

add_library(obf2_flags INTERFACE)
if(MSVC)
  target_compile_options(obf2_flags INTERFACE
    /W4 /permissive- /utf-8 /Zc:__cplusplus /Zc:preprocessor /EHsc)
  target_compile_definitions(obf2_flags INTERFACE
    NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  if(OBF2_WERROR)
    target_compile_options(obf2_flags INTERFACE /WX)
  endif()
else()
  target_compile_options(obf2_flags INTERFACE -Wall -Wextra -Wpedantic)
  if(OBF2_WERROR)
    target_compile_options(obf2_flags INTERFACE -Werror)
  endif()
endif()

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "")
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(sdl3)

add_subdirectory(third_party/miniz)
add_subdirectory(third_party/stb)

add_subdirectory(src/core)
add_subdirectory(src/con)
add_subdirectory(src/vfs)
add_subdirectory(src/mesh)
add_subdirectory(src/texture)
add_subdirectory(src/game)
add_subdirectory(src/level)
add_subdirectory(src/engine)
add_subdirectory(src/net)
add_subdirectory(src/font)
add_subdirectory(src/loc)
add_subdirectory(src/gfx)
add_subdirectory(src/app)

if(OBF2_BUILD_TOOLS)
  add_subdirectory(tools/con_dump)
  add_subdirectory(tools/mesh_info)
  add_subdirectory(tools/texture_info)
  add_subdirectory(tools/object_info)
endif()

if(OBF2_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

message(STATUS "OpenBattlefield2 ${PROJECT_VERSION} | ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR} | ${CMAKE_CXX_COMPILER_ID}")
