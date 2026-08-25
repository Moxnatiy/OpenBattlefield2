function(obf2_add_test name)
  add_executable(${name} ${name}.cpp)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_link_libraries(${name} PRIVATE obf2_flags ${ARGN})
  add_test(NAME ${name} COMMAND ${name})
endfunction()

obf2_add_test(test_path obf2_core)
obf2_add_test(test_con obf2_con)
