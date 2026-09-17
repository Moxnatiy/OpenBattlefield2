# The application's own modules. They are a library rather than part of the
# executable so that a test can link them: whatever a test can check does not
# belong in `main.cpp` (CLAUDE.md, rule 11).
add_library(obf2_app
  src/command_line.cpp
  src/hosted_game.cpp
  src/scene_build.cpp
)
add_library(obf2::app ALIAS obf2_app)
target_include_directories(obf2_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_app PUBLIC obf2_core obf2_con obf2_game obf2_level obf2_mesh obf2_net obf2_server obf2_vfs PRIVATE obf2_flags)

add_executable(openbf2 main.cpp)
target_link_libraries(openbf2 PRIVATE obf2_app obf2_gfx obf2_vfs obf2_game obf2_level obf2_engine obf2_font obf2_loc obf2_server obf2_session obf2_hud obf2_anim obf2_flags)
if(TARGET obf2_flash)
  target_link_libraries(openbf2 PRIVATE obf2_flash)
endif()
