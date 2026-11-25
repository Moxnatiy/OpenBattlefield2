# The menu's Flash player, native (docs/research/12-native-flash.md): our
# gameswf (third_party/gameswf), the bridge to the engine, a CPU renderer.
add_library(obf2_swf src/player.cpp src/bridge.cpp src/raster.cpp)
add_library(obf2::swf ALIAS obf2_swf)
target_include_directories(obf2_swf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_swf PRIVATE gameswf stb_image obf2_texture)
# The menu's background is a Bink video in a loadMovie fill (`?menu.bik`).
if(TARGET obf2_video)
  target_link_libraries(obf2_swf PRIVATE obf2_video)
endif()
# gameswf's headers are C++14-era code with its own warnings; ours is compiled
# with the project's flags apart from those headers' noise.
set_target_properties(obf2_swf PROPERTIES CXX_STANDARD 20)
if(NOT MSVC)  # a GCC/Clang flag; cl.exe rejects it (D8021)
  target_compile_options(obf2_swf PRIVATE -Wno-deprecated-declarations)
endif()
# The menu's rasteriser touches every pixel of a 1024 × 768 frame each frame;
# unoptimised (std::clamp and friends as calls) it held the menu at a frame or
# two a second in the debug build. Debug information stays.
if(NOT MSVC)
  target_compile_options(obf2_swf PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
