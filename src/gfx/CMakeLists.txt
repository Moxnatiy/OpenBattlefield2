add_library(obf2_gfx
  src/device.cpp
  src/mesh_renderer.cpp
)
add_library(obf2::gfx ALIAS obf2_gfx)

target_include_directories(obf2_gfx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_gfx PUBLIC obf2_core obf2_mesh obf2_texture SDL3::SDL3 PRIVATE obf2_flags)
