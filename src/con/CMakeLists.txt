add_library(obf2_con
  src/lexer.cpp
  src/interpreter.cpp
)
add_library(obf2::con ALIAS obf2_con)

target_include_directories(obf2_con PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(obf2_con PUBLIC obf2_core PRIVATE obf2_flags)
