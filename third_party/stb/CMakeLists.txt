# stb_image v2.30 (public domain / MIT) — https://github.com/nothings/stb
# Needed for PNG only: BF2's menu assets are in it, while the game's textures are
# DDS, which we read ourselves.
add_library(stb_image stb_image.c)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(stb_image PRIVATE /W0)
else()
  target_compile_options(stb_image PRIVATE -w)
endif()
