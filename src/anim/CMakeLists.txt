add_library(obf2_anim src/system.cpp src/player.cpp src/ragdoll.cpp)
target_include_directories(obf2_anim PUBLIC include)
target_link_libraries(obf2_anim PUBLIC obf2_con obf2_vfs obf2_mesh obf2_core PRIVATE obf2_flags)
