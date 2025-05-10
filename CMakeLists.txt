cmake_minimum_required(VERSION 3.10)
project(VynParser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Catch2 REQUIRED)

add_executable(vyn_parser
    src/token.cpp
    src/lexer.cpp
    src/parser.cpp
    src/main.cpp
    src/tests.cpp
)

target_include_directories(vyn_parser PRIVATE include)

target_link_libraries(vyn_parser PRIVATE Catch2::Catch2WithMain)

target_sources(vyn_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/include/vyn/token.hpp
    ${CMAKE_SOURCE_DIR}/include/vyn/ast.hpp
    ${CMAKE_SOURCE_DIR}/include/vyn/lexer.hpp
    ${CMAKE_SOURCE_DIR}/include/vyn/parser.hpp
    ${CMAKE_SOURCE_DIR}/include/vyn/vyn.hpp
)

# Add debug flags for tests.cpp
set_source_files_properties(src/tests.cpp PROPERTIES COMPILE_FLAGS "-Wall -Wextra -DDEBUG_TESTS")