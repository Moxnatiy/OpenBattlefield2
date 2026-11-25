# gameswf, tu-testbed revision 777 (27 February 2006) — public domain.
#
# The base our own Flash player grows from (src/swf). It is the snapshot
# SwiffPlayer.dll was built from: of the library's log messages, 40 are in r777
# and in the DLL but not in the later r1714, against 2 the other way
# (docs/research/12-native-flash.md). Exported with
#   svn export https://svn.code.sf.net/p/tu-testbed/code/trunk/tu-testbed/{gameswf,base}@777
# without the OpenGL/D3D/SDL backends and the stand-alone programs. Every local
# change is marked "OpenBattlefield2:" at its place.
file(GLOB GAMESWF_SOURCES CONFIGURE_DEPENDS
     ${CMAKE_CURRENT_SOURCE_DIR}/gameswf/*.cpp
     ${CMAKE_CURRENT_SOURCE_DIR}/base/*.cpp)

add_library(gameswf STATIC ${GAMESWF_SOURCES})
# SYSTEM: the library's own warnings stay out of our modules' builds.
target_include_directories(gameswf SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                                 ${CMAKE_CURRENT_SOURCE_DIR}/base)
target_compile_definitions(gameswf PUBLIC
  TU_CONFIG_LINK_TO_THREAD=0
  TU_CONFIG_LINK_TO_JPEGLIB=0
  TU_CONFIG_LINK_TO_LIBPNG=0
  TU_CONFIG_LINK_TO_ZLIB=1)
set_target_properties(gameswf PROPERTIES CXX_STANDARD 14 CXX_EXTENSIONS ON)
# The system zlib by name: ZLIB::ZLIB adds the SDK's include directory as
# -isystem, which puts the C headers ahead of libc++'s and breaks <cstdlib>.
# Windows has no system zlib; there miniz's zlib-compatible API stands in
# (third_party/miniz, already in the build for the archives).
if(WIN32)
  target_link_libraries(gameswf PRIVATE miniz stb_image)
else()
  target_link_libraries(gameswf PUBLIC z PRIVATE stb_image)
endif()
if(MSVC)
  target_compile_options(gameswf PRIVATE /W0)
else()
  target_compile_options(gameswf PRIVATE -w)
  # The menu runs through this VM every frame; unoptimised it cost the debug
  # build most of its frame time. Debug information stays.
  target_compile_options(gameswf PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
