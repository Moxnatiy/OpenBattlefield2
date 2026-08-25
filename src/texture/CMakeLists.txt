add_library(obf2_texture
  src/dds.cpp
  src/png.cpp
)
add_library(obf2::texture ALIAS obf2_texture)

target_include_directories(obf2_texture PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_texture PUBLIC obf2_core PRIVATE obf2_flags stb_image)
