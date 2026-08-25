# stb_image v2.30 (public domain / MIT) — https://github.com/nothings/stb
# Потрібен лише для PNG: ассети меню BF2 лежать саме в ньому, тоді як
# ігрові текстури — DDS, які ми читаємо самі.
add_library(stb_image stb_image.c)
target_include_directories(stb_image PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
  target_compile_options(stb_image PRIVATE /W0)
else()
  target_compile_options(stb_image PRIVATE -w)
endif()
