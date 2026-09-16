add_library(obf2_session
  src/remote_world.cpp
)
add_library(obf2::session ALIAS obf2_session)

target_include_directories(obf2_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# It needs all of them: the protocol from net, the soldier's physics from server,
# the templates from game, the level from level and the profile from engine. That
# is why it is not under net/ — obf2_server already links obf2_net.
target_link_libraries(obf2_session
  PUBLIC obf2_core obf2_net obf2_server obf2_game obf2_level obf2_engine obf2_vfs
  PRIVATE obf2_flags)
