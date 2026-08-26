add_library(obf2_mesh
  src/bf2_mesh.cpp
  src/collision.cpp
  src/skeleton.cpp
)
add_library(obf2::mesh ALIAS obf2_mesh)

target_include_directories(obf2_mesh PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_mesh PUBLIC obf2_core PRIVATE obf2_flags)
