# miniz 3.0.2 (MIT) — https://github.com/richgel999/miniz
# A single-file zlib/zip implementation. The amalgamation was taken so the build
# does not depend on a system zlib: macOS has one, Windows does not.
add_library(miniz STATIC miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(miniz PRIVATE /W0)
else()
  target_compile_options(miniz PRIVATE -w)
endif()
