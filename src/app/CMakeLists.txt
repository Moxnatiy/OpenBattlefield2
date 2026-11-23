# The application's own modules. They are a library rather than part of the
# executable so that a test can link them: whatever a test can check does not
# belong in `main.cpp` (CLAUDE.md, rule 11).
add_library(obf2_app
  src/command_line.cpp
  src/hosted_game.cpp
  src/scene_build.cpp
  src/render_context.cpp
  src/world_view.cpp
  src/main_menu.cpp
  src/camera.cpp
  src/model_view.cpp
  src/calibrate.cpp
  src/collision_probe.cpp
  src/scripted_input.cpp
  src/scoreboard_players.cpp
  src/spawn_circles.cpp
  src/crosshair.cpp
  src/ammo_hud.cpp
  src/player_hud.cpp
  src/first_person.cpp
  src/menu_movie.cpp
  src/loading_screen.cpp
  src/hit_direction.cpp
  src/death_camera.cpp
  src/third_person_look.cpp
)
add_library(obf2::app ALIAS obf2_app)
if(TARGET obf2_video)
  target_link_libraries(obf2_app PUBLIC obf2_video)
endif()
target_link_libraries(obf2_app PUBLIC obf2_swf)
target_compile_definitions(obf2_app PRIVATE OBF2_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(obf2_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_app PUBLIC obf2_core obf2_con obf2_game obf2_hud obf2_level obf2_mesh obf2_net obf2_server obf2_texture obf2_gfx obf2_anim obf2_engine obf2_font obf2_session obf2_vfs PRIVATE obf2_flags)

add_executable(openbf2 main.cpp)
target_link_libraries(openbf2 PRIVATE obf2_app obf2_gfx obf2_vfs obf2_game obf2_level obf2_engine obf2_font obf2_loc obf2_server obf2_session obf2_hud obf2_anim obf2_flags)
