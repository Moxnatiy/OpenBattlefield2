add_library(obf2_net
  src/bitstream.cpp
  src/connection.cpp
  src/session.cpp
  src/bf2_protocol.cpp
  src/bf2_events.cpp
  src/end_of_round.cpp
  src/retail_voice.cpp
  src/retail_voice_codec.cpp
  src/soldier_state.cpp
  src/player_state.cpp
  src/score_state.cpp
  src/control_point_state.cpp
  src/projectile_state.cpp
  src/chat_string.cpp
  src/ghost_track.cpp
  src/bf2_world.cpp
  src/bf2_join.cpp
  src/bf2_link.cpp
  src/md5.cpp
  src/udp.cpp
)
add_library(obf2::net ALIAS obf2_net)

target_include_directories(obf2_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_net PUBLIC obf2_core PRIVATE obf2_flags miniz obf2_speex)
if(WIN32)
  target_link_libraries(obf2_net PRIVATE ws2_32)  # Winsock 2, src/udp.cpp
endif()
