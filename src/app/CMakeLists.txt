add_executable(openbf2 main.cpp)
target_link_libraries(openbf2 PRIVATE obf2_gfx obf2_vfs obf2_game obf2_level obf2_engine obf2_font obf2_loc obf2_server obf2_hud obf2_flags)
