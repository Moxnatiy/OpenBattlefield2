# stb_vorbis v1.24 (Sean Barrett; public domain or MIT, at the user's choice, as
# the end of the file says), fetched at configure time at upstream revision
# 9600522de53973336fa033a289858a4354cbb04c and pinned by its hash, as Speex is
# (cmake/speex.cmake). It decodes the game's `.ogg` music: the mod's and the
# levels' `<music>` (app/menu_music.h). The one file that uses it includes the
# header part; this target compiles the implementation once.
include(FetchContent)
FetchContent_Declare(obf2_stb_vorbis_source
  URL https://raw.githubusercontent.com/nothings/stb/9600522de53973336fa033a289858a4354cbb04c/stb_vorbis.c
  URL_HASH SHA256=b52cf518355f7b2d181f18b97b812f361ffeb26299d935c8cf29e842401d60a6
  DOWNLOAD_NO_EXTRACT TRUE
)
FetchContent_MakeAvailable(obf2_stb_vorbis_source)

add_library(stb_vorbis STATIC "${obf2_stb_vorbis_source_SOURCE_DIR}/stb_vorbis.c")
target_include_directories(stb_vorbis SYSTEM PUBLIC "${obf2_stb_vorbis_source_SOURCE_DIR}")
target_compile_definitions(stb_vorbis PRIVATE STB_VORBIS_NO_STDIO)
# A fetched library is not ours to fix; its warnings are not ours to fail on.
if(MSVC)
  target_compile_options(stb_vorbis PRIVATE /W0)
else()
  target_compile_options(stb_vorbis PRIVATE -w)
endif()
