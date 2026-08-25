add_library(obf2_font
  src/dif_font.cpp
  src/text.cpp
)
add_library(obf2::font ALIAS obf2_font)

target_include_directories(obf2_font PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_font PUBLIC obf2_core obf2_mesh PRIVATE obf2_flags)
