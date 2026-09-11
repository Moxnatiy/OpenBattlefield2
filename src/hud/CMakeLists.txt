add_library(obf2_hud
  src/hud.cpp
  src/animation.cpp
  src/render.cpp
  src/spawn.cpp
  src/spawn_interface.cpp
  src/hud_items.cpp
  src/kit_list.cpp
  src/combat_area.cpp
  src/bottom_left.cpp
  src/map_node.cpp
  src/ingame.cpp
  src/states.cpp
)
add_library(obf2::hud ALIAS obf2_hud)

target_include_directories(obf2_hud PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_hud PUBLIC obf2_con obf2_core obf2_meme obf2_engine obf2_font obf2_mesh obf2_game obf2_texture PRIVATE obf2_flags)
