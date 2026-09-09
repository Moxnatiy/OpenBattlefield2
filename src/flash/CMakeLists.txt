# The Flash player for the game's menu.
#
# BF2's menu is `mainMenu.swf`, and it is played by Ruffle
# (docs/research/11-ruffle-menu.md). Ruffle is written in Rust, so the build
# here has two stages: `cargo` makes a library with a C interface, and an
# ordinary CMake target adds our thin C++ wrapper to it.
#
# The module is **optional**: without Rust or without `reference/ruffle` it
# simply is not built, and the client is left without the Flash menu. That is
# deliberate — the rest of the port does not depend on it.
find_program(CARGO_EXECUTABLE cargo)

set(OBF2_RUFFLE_DIR "${CMAKE_SOURCE_DIR}/reference/ruffle")
if(NOT CARGO_EXECUTABLE OR NOT EXISTS "${OBF2_RUFFLE_DIR}/core/Cargo.toml")
  message(STATUS "obf2_flash: skipped (no cargo or no reference/ruffle)")
  return()
endif()

set(OBF2_FLASH_LIB
    "${CMAKE_CURRENT_SOURCE_DIR}/target/release/${CMAKE_SHARED_LIBRARY_PREFIX}obf2_flash${CMAKE_SHARED_LIBRARY_SUFFIX}")

# Ruffle builds its AS3 library with a Java compiler, so java has to be on
# PATH. On macOS Homebrew puts it aside.
#
# The target **always** asks cargo rather than comparing output timestamps: the
# bridge's sources lie outside this tree (`reference/ruffle`, where
# `tools/ruffle_install.sh` puts them), and listing them for CMake is
# impossible. Cargo says "nothing to do" in a second, while a silently stale
# bridge cost us a pass with a wrong conclusion.
add_custom_target(obf2_flash_rust ALL
  COMMAND ${CMAKE_COMMAND} -E env
          "PATH=/opt/homebrew/opt/openjdk@21/bin:$ENV{PATH}"
          ${CARGO_EXECUTABLE} build --release
  BYPRODUCTS "${OBF2_FLASH_LIB}"
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "cargo build --release (obf2_flash, Ruffle)"
  VERBATIM)

add_library(obf2_flash src/movie.cpp src/images.cpp)
add_library(obf2::flash ALIAS obf2_flash)
add_dependencies(obf2_flash obf2_flash_rust)

target_include_directories(obf2_flash PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_flash PUBLIC "${OBF2_FLASH_LIB}" PRIVATE obf2_flags obf2_texture)
target_compile_definitions(obf2_flash PUBLIC OBF2_HAVE_FLASH=1
                           PRIVATE OBF2_VERSION="${PROJECT_VERSION}")

# The library lies in cargo's directory, so the executable has to be told where
# to look for it at runtime.
set_target_properties(obf2_flash PROPERTIES
  INTERFACE_LINK_OPTIONS "-Wl,-rpath,${CMAKE_CURRENT_SOURCE_DIR}/target/release")
