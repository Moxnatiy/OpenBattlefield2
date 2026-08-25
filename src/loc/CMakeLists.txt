add_library(obf2_loc
  src/lexicon.cpp
)
add_library(obf2::loc ALIAS obf2_loc)

target_include_directories(obf2_loc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_loc PUBLIC obf2_core PRIVATE obf2_flags)
