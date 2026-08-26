add_library(obf2_anim src/system.cpp)
target_include_directories(obf2_anim PUBLIC include)
target_link_libraries(obf2_anim PUBLIC obf2_con obf2_vfs obf2_core PRIVATE obf2_flags)
