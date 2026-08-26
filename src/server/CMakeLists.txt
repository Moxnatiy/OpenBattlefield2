add_library(obf2_server
  src/game_server.cpp
  src/game_client.cpp
  src/physics.cpp
)
add_library(obf2::server ALIAS obf2_server)

target_include_directories(obf2_server PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_server PUBLIC obf2_net obf2_game obf2_level obf2_engine PRIVATE obf2_flags)
