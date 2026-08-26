add_library(obf2_hud
  src/hud.cpp
)
add_library(obf2::hud ALIAS obf2_hud)

target_include_directories(obf2_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_hud PUBLIC obf2_con obf2_core PRIVATE obf2_flags)
