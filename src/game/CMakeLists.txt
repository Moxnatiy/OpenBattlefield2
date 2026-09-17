add_library(obf2_game
  src/controls.cpp
  src/object_template.cpp
  src/template_numbers.cpp
  src/soldier_model.cpp
  src/scene.cpp
  src/object_mesh.cpp
)
add_library(obf2::game ALIAS obf2_game)

target_include_directories(obf2_game PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_game PUBLIC obf2_con obf2_core obf2_mesh obf2_vfs PRIVATE obf2_flags)
