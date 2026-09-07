add_library(obf2_meme
  src/file.cpp
  src/graph.cpp
)
add_library(obf2::meme ALIAS obf2_meme)

target_include_directories(obf2_meme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_meme PUBLIC obf2_core PRIVATE obf2_flags)
