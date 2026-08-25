# miniz 3.0.2 (MIT) — https://github.com/richgel999/miniz
# Single-file zlib/zip реалізація. Взято амальгамацію, щоб збірка не залежала
# від системного zlib: на macOS він є, на Windows — ні.
add_library(miniz STATIC miniz.c)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(miniz PRIVATE /W0)
else()
  target_compile_options(miniz PRIVATE -w)
endif()
