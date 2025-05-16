cmake_minimum_required(VERSION 3.10)
project(VynParser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Catch2 REQUIRED)

# Parser sources
set(PARSER_SOURCES
    src/parser/ast.cpp
    src/parser/base_parser.cpp
    src/parser/declaration_parser.cpp
    src/parser/expression_parser.cpp
    src/parser/lexer.cpp
    src/parser/module_parser.cpp
    src/parser/parser.cpp
    src/parser/statement_parser.cpp
    src/parser/token.cpp
    src/parser/type_parser.cpp
)

# Parser headers
set(PARSER_HEADERS
    include/vyn/parser/ast.hpp
    include/vyn/parser/lexer.hpp
    include/vyn/parser/parser.hpp
    include/vyn/parser/source_location.hpp
    include/vyn/parser/token.hpp
)

# VRE sources
set(VRE_SOURCES
    src/vre/value.cpp
    src/vre/semantic.cpp
    src/vre/llvm/codegen.cpp
)

# VRE headers
set(VRE_HEADERS
    include/vyn/vre/value.hpp
    include/vyn/vre/semantic.hpp
    include/vyn/vre/memory.hpp
    include/vyn/vre/runtime_types.hpp
    include/vyn/vre/llvm/codegen.hpp
)

# Main and tests
set(MAIN_SOURCES src/main.cpp src/tests.cpp)

add_executable(vyn_parser
    ${PARSER_SOURCES}
    ${PARSER_HEADERS}
    ${VRE_SOURCES}
    ${VRE_HEADERS}
    ${MAIN_SOURCES}
)

target_include_directories(vyn_parser PRIVATE include)

target_link_libraries(vyn_parser PRIVATE Catch2::Catch2WithMain)

# Add debug flags for tests.cpp
set_source_files_properties(src/tests.cpp PROPERTIES COMPILE_FLAGS "-Wall -Wextra -DDEBUG_TESTS -DVERBOSE")

# Enable verbose debugging for all parser files when needed
# Uncomment these lines to enable verbose debugging for the entire project
add_compile_definitions(VERBOSE)