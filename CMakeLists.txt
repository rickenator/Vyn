cmake_minimum_required(VERSION 3.15)
project(VynParser)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Catch2 REQUIRED)

# Find LLVM
find_package(LLVM 18.1 REQUIRED CONFIG) # Removed COMPONENTS

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Make sure AddLLVM.cmake is on the module path
list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")
include(AddLLVM)

# Build a list of actual .a/.so names for the components you need
llvm_map_components_to_libnames(LLVM_LIBS
  Core
  Support
  IRReader
  AsmParser
  BitReader
  BitWriter
  IPO
  TransformUtils
  ExecutionEngine
  Object
  X86
)

# Parser sources
set(PARSER_SOURCES
    src/parser/ast.cpp
    src/parser/ast_extra.cpp
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
    src/vre/llvm/cgen_main.cpp
    src/vre/llvm/cgen_types.cpp
    src/vre/llvm/cgen_util.cpp
    src/vre/llvm/cgen_expr.cpp
    src/vre/llvm/cgen_decl.cpp
    src/vre/llvm/cgen_stmt.cpp
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

target_include_directories(vyn_parser PRIVATE
    include # Project's own include directory
    ${LLVM_INCLUDE_DIRS} # Added LLVM include directories
)

add_definitions(${LLVM_DEFINITIONS}) # Added LLVM definitions

# Link libraries
target_link_libraries(vyn_parser PRIVATE
    Catch2::Catch2WithMain
    ${LLVM_LIBS} # Link against the mapped LLVM libraries
)

# Add debug flags for tests.cpp
set_source_files_properties(src/tests.cpp PROPERTIES COMPILE_FLAGS "-Wall -Wextra -DDEBUG_TESTS -DVERBOSE")

# Enable verbose debugging for all parser files when needed
# Uncomment these lines to enable verbose debugging for the entire project
add_compile_definitions(VERBOSE)