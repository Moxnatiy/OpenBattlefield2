add_library(obf2_engine
  src/console.cpp
  src/settings.cpp
  src/control_map.cpp
  src/engine.cpp
)
add_library(obf2::engine ALIAS obf2_engine)

target_include_directories(obf2_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_engine PUBLIC obf2_con obf2_core obf2_vfs obf2_loc PRIVATE obf2_flags)
