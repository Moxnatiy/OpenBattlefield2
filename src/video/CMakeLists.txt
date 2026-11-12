# Bink video through FFmpeg (docs/functions/startup.md, "The movies").
#
# The original plays its movies with RAD's binkw32.dll. We decode them with
# FFmpeg's libavcodec, linked **dynamically** — FFmpeg is LGPL, which allows an
# MIT program to link it as a shared library and does not allow copying its code
# in. The module is optional in the same way as the Flash one: without FFmpeg
# (CI has none) it is not built, and the movies stay black for their length.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(OBF2_FFMPEG QUIET IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
endif()
if(NOT OBF2_FFMPEG_FOUND)
  message(STATUS "obf2_video: skipped (no FFmpeg through pkg-config)")
  return()
endif()

add_library(obf2_video src/video.cpp)
add_library(obf2::video ALIAS obf2_video)
target_include_directories(obf2_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_video PRIVATE PkgConfig::OBF2_FFMPEG obf2_flags)
target_compile_definitions(obf2_video PUBLIC OBF2_HAVE_VIDEO=1)
