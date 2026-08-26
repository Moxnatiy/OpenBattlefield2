add_library(obf2_level
  src/level.cpp
  src/gameplay.cpp
)
add_library(obf2::level ALIAS obf2_level)

target_include_directories(obf2_level PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_level PUBLIC obf2_con obf2_core obf2_mesh obf2_vfs PRIVATE obf2_flags)
