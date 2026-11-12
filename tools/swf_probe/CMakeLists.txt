add_executable(swf_probe main.cpp)
target_link_libraries(swf_probe PRIVATE gameswf obf2_vfs obf2_con)
set_target_properties(swf_probe PROPERTIES CXX_STANDARD 20)
target_compile_options(swf_probe PRIVATE -w)
