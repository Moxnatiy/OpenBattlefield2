add_library(obf2_core
  src/path.cpp
)
add_library(obf2::core ALIAS obf2_core)

target_include_directories(obf2_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_core PRIVATE obf2_flags)
