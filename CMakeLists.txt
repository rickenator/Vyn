cmake_minimum_required(VERSION 3.15)
project(VybParser VERSION 0.7.3)

# Build profile (#160): Debug is the DEFAULT when the caller selects no build type,
# but a caller-set -DCMAKE_BUILD_TYPE=Release / RelWithDebInfo is respected (never
# silently overridden). Use `cmake --preset <name>`; see CMakePresets.json and
# doc/BUILD.md for the supported profiles.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type: Debug, Release, RelWithDebInfo, MinSizeRel" FORCE)
endif()

# Optional sanitizer profile (#160). Empty = none. Examples:
#   -DVYB_SANITIZE=address
#   -DVYB_SANITIZE="address,undefined"
# Applies -fsanitize=<s> to C, C++, and link for each listed sanitizer, plus
# -fno-omit-frame-pointer for usable backtraces. Used by the CI ASan preset.
set(VYB_SANITIZE "" CACHE STRING "Sanitizer(s) to enable (empty=none; e.g. address / address,undefined)")
if(VYB_SANITIZE)
    string(REPLACE "," ";" VYB_SAN_LIST ${VYB_SANITIZE})
    foreach(s ${VYB_SAN_LIST})
        string(STRIP ${s} s)
        if(s)
            message(STATUS "Sanitizer: -fsanitize=${s}")
            add_compile_options(-fsanitize=${s} -fno-omit-frame-pointer)
            add_link_options(-fsanitize=${s})
        endif()
    endforeach()
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Catch2 REQUIRED)

# OpenSSL: powers the stdlib `tls` module (TLS over the extern "C" FFI). Linked
# into the `vyb` binary so the ORC JIT's process-symbol lookup can resolve the
# libssl/libcrypto symbols at JIT link time.
option(VYB_USE_OPENSSL "Build with OpenSSL support (stdlib tls module)" ON)
if(VYB_USE_OPENSSL)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL: ${OPENSSL_VERSION} (tls module enabled)")
    else()
        message(WARNING "OpenSSL not found; stdlib tls / https modules disabled")
    endif()
endif()

# Find LLVM
find_package(LLVM 18.1.3 REQUIRED CONFIG)
message(STATUS "LLVM package version: ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM include dirs: ${LLVM_INCLUDE_DIRS}")
message(STATUS "LLVM library dirs: ${LLVM_LIBRARY_DIRS}")
message(STATUS "LLVM available libraries: ${LLVM_AVAILABLE_LIBS}")
message(STATUS "LLVM available components: ${LLVM_AVAILABLE_COMPONENTS}")

# Check components using LLVM_AVAILABLE_LIBS
foreach(comp Core Support IRReader AsmParser BitReader BitWriter ipo TransformUtils ExecutionEngine MCJIT Object X86CodeGen RuntimeDyld X86AsmParser)
  if(NOT "LLVM${comp}" IN_LIST LLVM_AVAILABLE_LIBS)
    message(WARNING "LLVM component ${comp} not found in LLVM_AVAILABLE_LIBS")
  endif()
endforeach()

# libclang (optional): enables `vyb bindgen <header.h> --full` (full
# preprocessor: #include expansion, conditional evaluation, object-like and
# function-like macros, canonical-type resolution for <stdint.h> typedefs).
option(VYB_BINDGEN_LIBCLANG "Build the libclang full-preprocessor bindgen backend" ON)
if(VYB_BINDGEN_LIBCLANG)
    find_path(LIBCLANG_INCLUDE_DIR NAMES clang-c/Index.h
              HINTS ${LLVM_INCLUDE_DIRS} /usr/lib/llvm-*/include)
    find_library(LIBCLANG_LIBRARY NAMES clang libclang-18 libclang
                 HINTS ${LLVM_LIBRARY_DIRS} /usr/lib/llvm-*/lib)
    if(LIBCLANG_INCLUDE_DIR AND LIBCLANG_LIBRARY)
        message(STATUS "libclang: ${LIBCLANG_LIBRARY} (headers ${LIBCLANG_INCLUDE_DIR})")
        set(VYB_HAVE_LIBCLANG TRUE)
    else()
        message(WARNING "libclang headers/library not found; `vyb bindgen --full` disabled")
        set(VYB_HAVE_LIBCLANG FALSE)
    endif()
else()
    set(VYB_HAVE_LIBCLANG FALSE)
endif()

message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

# Make sure AddLLVM.cmake is on the module path
list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")
include(AddLLVM)

# Build a list of actual .a/.so names for the components
llvm_map_components_to_libnames(LLVM_LIBS
  Core
  Support
  IRReader
  AsmParser
  BitReader
  BitWriter
  IPO
  TransformUtils
  ExecutionEngine  # Legacy JIT support
  MCJIT            # Older JIT API
  OrcJIT           # Modern JIT API
  JITLink          # Helper for OrcJIT
  Interpreter      # Add Interpreter support
  Object
  X86CodeGen       # X86 backend (was X86)
  RuntimeDyld
  X86AsmParser
  # Target components for object file emission (all architectures)
  AllTargetsInfos
  AllTargetsDescs
  AllTargetsCodeGens
  AllTargetsAsmParsers
)
message(STATUS "LLVM libraries: ${LLVM_LIBS}")

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
    include/vyb/parser/ast.hpp
    include/vyb/parser/lexer.hpp
    include/vyb/parser/parser.hpp
    include/vyb/parser/source_location.hpp
    include/vyb/parser/token.hpp
)

# VRE sources
set(VRE_SOURCES
    src/vre/value.cpp
    src/vre/semantic.cpp
    src/vre/intrinsics.cpp
    src/vre/llvm/cgen_main.cpp
    src/vre/llvm/cgen_types.cpp
    src/vre/llvm/cgen_util.cpp
    src/vre/llvm/cgen_expr.cpp
    src/vre/llvm/cgen_decl.cpp
    src/vre/llvm/cgen_monomorph.cpp
    src/vre/llvm/cgen_metadata.cpp
    src/vre/llvm/cgen_function_mono.cpp
    src/vre/llvm/cgen_trait_mono.cpp
    src/vre/llvm/cgen_stmt.cpp
    src/vre/llvm/cgen_helpers.cpp
    src/vre/llvm/cgen_string.cpp
    src/vre/llvm/cgen_println.cpp
    src/vre/llvm/cgen_vec.cpp
    src/vre/llvm/cgen_string_type.cpp
    src/vre/llvm/cgen_function.cpp
    src/vre/llvm/cgen_semantics.cpp
    src/vre/llvm/cgen_ownership.cpp
    src/vre/llvm/cgen_async_impl.cpp
    src/vre/llvm/cgen_stack_trace.cpp
)

# VRE headers
set(VRE_HEADERS
    include/vyb/vre/value.hpp
    include/vyb/semantic.hpp
    include/vyb/module_registry.hpp
    include/vyb/vre/memory.hpp
    include/vyb/vre/runtime_types.hpp
    include/vyb/vre/llvm/codegen.hpp
)

# Qt5 Widgets: powers the stdlib `qt` module (native GUI bindings). Qt5 is
# strictly C++, so unlike the C-only stdlib shims the Qt bridge lives in its own
# translation unit (`runtime/vyb_qt_bridge.cpp`) exporting `__vyb_qt_*` extern
# "C" symbols. Optional: if Qt5 is absent the build still succeeds and the module
# resolves to the stub shims in `runtime/vyb_qt_stub.cpp`, which report the GUI
# as unavailable (`qt_init()==false`) instead of failing at JIT link-time.
option(VYB_USE_QT5 "Build with Qt5 Widgets support (stdlib qt module)" ON)
if(VYB_USE_QT5)
    find_package(Qt5 COMPONENTS Widgets QUIET)
    if(Qt5Widgets_FOUND)
        message(STATUS "Qt5: ${Qt5Widgets_VERSION} (qt module enabled)")
        set(VYB_QT_BRIDGE runtime/vyb_qt_bridge.cpp)
        # QtWebEngine (QWebEngineView) is an optional extra on top of Widgets;
        # when absent the `qt_web_*` surface resolves to stub shims instead.
        find_package(Qt5 COMPONENTS WebEngineWidgets QUIET)
        if(Qt5WebEngineWidgets_FOUND)
            message(STATUS "Qt5 WebEngine: ${Qt5WebEngineWidgets_VERSION} (qt web surface enabled)")
            set(VYB_QT_WEB_BRIDGE runtime/vyb_qt_webengine_bridge.cpp)
        else()
            message(STATUS "Qt5 WebEngine not found; stdlib qt web surface disabled (stub fallback)")
        endif()
    else()
        message(WARNING "Qt5 Widgets not found; stdlib qt module disabled")
    endif()
endif()

# Runtime sources
set(RUNTIME_SOURCES
    src/runtime/error_handling.cpp
    runtime/vyb_runtime.c
    runtime/vyb_type_metadata.c
    runtime/vyb_qt_stub.cpp
)
if(VYB_QT_BRIDGE)
    list(APPEND RUNTIME_SOURCES ${VYB_QT_BRIDGE})
endif()
if(VYB_QT_WEB_BRIDGE)
    list(APPEND RUNTIME_SOURCES ${VYB_QT_WEB_BRIDGE})
endif()

# Runtime headers
set(RUNTIME_HEADERS
    include/vyb/runtime/error_handling.hpp
)

# Main and tests
set(MAIN_SOURCES src/main.cpp src/bindgen.cpp src/module_registry.cpp src/manifest.cpp src/tests.cpp)

add_executable(vyb
    ${PARSER_SOURCES}
    ${PARSER_HEADERS}
    ${VRE_SOURCES}
    ${VRE_HEADERS}
    ${RUNTIME_SOURCES}
    ${RUNTIME_HEADERS}
    ${MAIN_SOURCES}
)

target_include_directories(vyb PRIVATE
    include # Project's own include directory
    SYSTEM ${LLVM_INCLUDE_DIRS} # Treat LLVM includes as system headers
)

# Add LLVM definitions
target_compile_definitions(vyb PRIVATE ${LLVM_DEFINITIONS})

# Build identity (#160): every produced binary embeds its build configuration so
# `vyb --version` reports the build type and sanitizer coverage unambiguously.
if(VYB_SANITIZE)
    set(VYB_SANITIZE_STR "${VYB_SANITIZE}")
else()
    set(VYB_SANITIZE_STR "none")
endif()
target_compile_definitions(vyb PRIVATE
    VYB_PROJECT_VERSION="${PROJECT_VERSION}"
    VYB_BUILD_TYPE_STR="${CMAKE_BUILD_TYPE}"
    VYB_SANITIZE_STR="${VYB_SANITIZE_STR}"
)

# Link libraries
target_link_libraries(vyb PRIVATE
    Catch2::Catch2WithMain
    ${LLVM_LIBS} # Link against the mapped LLVM libraries
)

# TLS: link OpenSSL into the binary so the JIT's process-symbol generator can
# resolve libssl/libcrypto symbols for the stdlib `tls` module.
if(VYB_USE_OPENSSL AND OpenSSL_FOUND)
    target_link_libraries(vyb PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(vyb PRIVATE VYB_HAVE_OPENSSL)
endif()

# ncursesw: link the wide-char TUI library into the binary so the JIT's
# process-symbol generator can resolve the stdlib `curses` module's runtime
# shims. Optional: if ncursesw is absent the build still succeeds and modules
# that use `curses` fail at JIT link-time with an unresolved-symbol error.
find_library(NCURSESW_LIBRARY NAMES ncursesw ncurses)
if(NCURSESW_LIBRARY)
    target_link_libraries(vyb PRIVATE ${NCURSESW_LIBRARY})
    target_compile_definitions(vyb PRIVATE VYB_HAVE_NCURSES)
    message(STATUS "ncursesw found: ${NCURSESW_LIBRARY}")
else()
    message(STATUS "ncursesw not found; stdlib `curses` module will be unavailable")
endif()

# Qt5: link the bridge's dependencies into the binary so the JIT's process-symbol
# generator can resolve `__vyb_qt_*` symbols (and the QWidget/QLabel symbols) for
# the stdlib `qt` module. Mirrors the ncursesw linkage above. Note we must NOT
# use `-Wl,--export-dynamic` here: exporting every program symbol lets Mesa's
# bundled LLVM (dlopen'd for GLX) interpose on this binary's LLVM 18 and crash in
# its static initializers.
if(VYB_QT_BRIDGE AND Qt5Widgets_FOUND)
    target_link_libraries(vyb PRIVATE Qt5::Widgets)
    target_compile_definitions(vyb PRIVATE VYB_HAVE_QT5)
endif()

# QtWebEngine: link the web bridge's dependencies so the JIT's process-symbol
# generator can resolve the __vyb_qt_web_* symbols. When WebEngine is absent the
# guarded stubs in vyb_qt_stub.cpp resolve instead and qt_web_* degrades.
if(VYB_QT_WEB_BRIDGE AND Qt5WebEngineWidgets_FOUND)
    target_link_libraries(vyb PRIVATE Qt5::WebEngineWidgets)
    target_compile_definitions(vyb PRIVATE VYB_HAVE_QT_WEBENGINE)
endif()

if(VYB_HAVE_LIBCLANG)
    add_executable(vyb-libclang
        src/bindgen_libclang_main.cpp
        src/bindgen_libclang.cpp
    )
    target_include_directories(vyb-libclang PRIVATE
        include
        SYSTEM ${LIBCLANG_INCLUDE_DIR}
    )
    target_compile_definitions(vyb-libclang PRIVATE VYB_BINDGEN_LIBCLANG)
    target_link_libraries(vyb-libclang PRIVATE ${LIBCLANG_LIBRARY})
endif()

# Platform-specific libraries for error handling runtime
if(UNIX AND NOT APPLE)
    # Linux: backtrace() requires -ldl, pthread for thread safety
    target_link_libraries(vyb PRIVATE dl pthread)
elseif(APPLE)
    # macOS: backtrace() is in system libraries, only need pthread
    target_link_libraries(vyb PRIVATE pthread)
elseif(WIN32)
    # Windows: CaptureStackBackTrace requires dbghelp.lib
    target_link_libraries(vyb PRIVATE dbghelp)
endif()

# Add debug flags for tests.cpp
set_source_files_properties(src/tests.cpp PROPERTIES COMPILE_FLAGS "-DDEBUG_TESTS")

# Parser verbose tracing is off by default. Define VERBOSE (e.g. -DVERBOSE in cmake)
# or use --debug-codegen at runtime to re-enable detailed diagnostics.

# Add a custom target for running tests
find_package(Python3 REQUIRED)
add_custom_target(run-tests
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/run_tests.py --vyb $<TARGET_FILE:vyb> --test-dir ${CMAKE_SOURCE_DIR}/test
    DEPENDS vyb
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Vyb test suite"
)

# CTest registration (#158): the canonical language suite (JIT) and the AOT/native
# build test are registered so `ctest` runs them reproducibly and their results are
# machine-readable (JSON + release evidence under the build dir). Focused category
# runs stay available via `run_tests.py --category <name>` / `ctest -L lang`.
include(CTest)
if(BUILD_TESTING)
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-results)
    add_test(NAME lang-jit
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/run_tests.py
                --vyb $<TARGET_FILE:vyb>
                --test-dir ${CMAKE_SOURCE_DIR}/test
                --execute-jit
                --json ${CMAKE_BINARY_DIR}/test-results/jit.json
                --evidence ${CMAKE_BINARY_DIR}/test-results/jit-evidence.json)
    set_tests_properties(lang-jit PROPERTIES
        TIMEOUT 1800
        LABELS "lang;jit"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

    add_test(NAME aot-native
        COMMAND bash ${CMAKE_SOURCE_DIR}/test_compilation.sh
                --json ${CMAKE_BINARY_DIR}/test-results/aot-evidence.json)
    set_tests_properties(aot-native PROPERTIES
        TIMEOUT 600
        LABELS "lang;aot"
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

add_custom_target(run-examples
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/run_examples.py --vyb $<TARGET_FILE:vyb>
    DEPENDS vyb
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Vyb demos and examples"
)

add_custom_target(run-milestone
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/test/run_milestone_tests.py --vyb $<TARGET_FILE:vyb>
    DEPENDS vyb
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running Vyb milestone gate"
)
