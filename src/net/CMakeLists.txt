add_library(obf2_net
  src/bitstream.cpp
  src/connection.cpp
  src/session.cpp
  src/bf2_protocol.cpp
  src/bf2_events.cpp
  src/md5.cpp
  src/udp.cpp
)
add_library(obf2::net ALIAS obf2_net)

target_include_directories(obf2_net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_net PUBLIC obf2_core PRIVATE obf2_flags)
