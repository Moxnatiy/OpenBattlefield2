# OpenAL Soft 1.24.3 (kcat/openal-soft), the renderer of the level's EAX reverb
# (app/environment_audio.h, docs/functions/environment-audio.md). The original
# plays through its own OpenAL (BF2OpenAL.dll, docs/functions/sound-template.md)
# with an EAX environment; OpenAL Soft's EFX EAX-reverb effect takes the same
# environment parameters.
#
# LICENCE: OpenAL Soft is LGPL-2.1 (COPYING in the archive), and it is linked
# **statically** here. The rest of this repository is MIT. A binary that ships
# with it statically linked must let its users relink against a modified OpenAL
# Soft: ship the object files or the source with it, or switch to a shared
# library (LIBTYPE SHARED below). Fetched at configure time, pinned by hash.
include(FetchContent)
block()
  set(LIBTYPE STATIC)
  set(ALSOFT_EAX OFF)
  set(ALSOFT_EXAMPLES OFF)
  set(ALSOFT_UTILS OFF)
  set(ALSOFT_TESTS OFF)
  set(ALSOFT_INSTALL OFF)
  set(ALSOFT_INSTALL_CONFIG OFF)
  set(ALSOFT_INSTALL_HRTF_DATA OFF)
  set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF)
  set(ALSOFT_UPDATE_BUILD_VERSION OFF)
  FetchContent_Declare(obf2_openal_soft
    URL https://github.com/kcat/openal-soft/archive/refs/tags/1.24.3.tar.gz
    URL_HASH SHA256=7e1fecdeb45e7f78722b776c5cf30bd33934b961d7fd2a11e0494e064cc631ce
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(obf2_openal_soft)
endblock()
