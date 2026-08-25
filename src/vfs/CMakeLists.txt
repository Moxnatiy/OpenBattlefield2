add_library(obf2_vfs
  src/filesystem.cpp
)
add_library(obf2::vfs ALIAS obf2_vfs)

target_include_directories(obf2_vfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_vfs PUBLIC obf2_core obf2_con PRIVATE obf2_flags miniz)
