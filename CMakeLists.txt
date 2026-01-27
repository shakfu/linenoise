cmake_minimum_required(VERSION 3.10)
project(linenoise VERSION 2.0.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Tree-sitter support option
option(WITH_TREESITTER "Build with tree-sitter syntax highlighting support" ON)

# Platform-specific terminal implementation
if(WIN32)
    set(TERMINAL_SRC src/terminal_windows.c)
else()
    set(TERMINAL_SRC src/terminal_posix.c)
endif()

# Module source files
set(MODULE_SOURCES
    src/utf8.c
    src/history.c
    src/completion.c
    src/keyparser.c
    src/render.c
)

# Core library source files
set(LINENOISE_SOURCES
    linenoise.c
    ${MODULE_SOURCES}
    ${TERMINAL_SRC}
)

# Linenoise library (static by default)
add_library(linenoise STATIC ${LINENOISE_SOURCES})

target_include_directories(linenoise
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/internal
)

# Shared library option
option(BUILD_SHARED_LIBS "Build shared library" OFF)
if(BUILD_SHARED_LIBS)
    add_library(linenoise_shared SHARED ${LINENOISE_SOURCES})
    target_include_directories(linenoise_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/internal
    )
    set_target_properties(linenoise_shared PROPERTIES OUTPUT_NAME linenoise)
endif()

# Example application
option(BUILD_EXAMPLES "Build example application" ON)
if(BUILD_EXAMPLES)
    add_executable(linenoise-example example.c)
    target_link_libraries(linenoise-example PRIVATE linenoise)
endif()

# Tree-sitter based Lua example with syntax highlighting
if(WITH_TREESITTER AND BUILD_EXAMPLES)
    # Tree-sitter library sources
    set(TREESITTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter)
    set(TREESITTER_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-lua)

    # Build tree-sitter as a static library
    # lib.c includes all other source files
    add_library(tree-sitter STATIC
        ${TREESITTER_DIR}/lib/src/lib.c
    )
    target_include_directories(tree-sitter
        PUBLIC
            ${TREESITTER_DIR}/lib/include
        PRIVATE
            ${TREESITTER_DIR}/lib/src
    )
    # Silence warnings in third-party code
    target_compile_options(tree-sitter PRIVATE -w)

    # Build tree-sitter-lua grammar
    add_library(tree-sitter-lua STATIC
        ${TREESITTER_LUA_DIR}/src/parser.c
        ${TREESITTER_LUA_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-lua
        PRIVATE
            ${TREESITTER_LUA_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    # Silence warnings in third-party code
    target_compile_options(tree-sitter-lua PRIVATE -w)

    # Lua highlighter module
    add_library(linenoise-highlight-lua STATIC
        src/highlight_lua.c
    )
    target_include_directories(linenoise-highlight-lua
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-lua
        PRIVATE tree-sitter tree-sitter-lua
    )

    # Lua REPL example with syntax highlighting
    add_executable(linenoise-lua example-lua.c)
    target_include_directories(linenoise-lua
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    target_link_libraries(linenoise-lua
        PRIVATE linenoise linenoise-highlight-lua tree-sitter tree-sitter-lua
    )

    message(STATUS "  Tree-sitter Lua example: ON")
endif()

# Tests
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS)
    enable_testing()

    add_executable(linenoise-test linenoise-test.c src/utf8.c)
    target_include_directories(linenoise-test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/internal)

    # Add test that runs the test suite
    add_test(NAME linenoise-test COMMAND linenoise-test)

    # Set environment for tests (the test binary needs the example binary)
    set_tests_properties(linenoise-test PROPERTIES
        ENVIRONMENT "PATH=$<TARGET_FILE_DIR:linenoise-example>;$ENV{PATH}"
        DEPENDS linenoise-example
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS linenoise
    EXPORT linenoiseTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES linenoise.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export for find_package support
install(EXPORT linenoiseTargets
    FILE linenoiseTargets.cmake
    NAMESPACE linenoise::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linenoise
)

# Package config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/linenoiseConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linenoiseConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/linenoiseConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linenoise
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/linenoiseConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/linenoiseConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/linenoise
)

# Summary
message(STATUS "")
message(STATUS "linenoise ${PROJECT_VERSION} configuration:")
message(STATUS "  Platform: ${CMAKE_SYSTEM_NAME}")
message(STATUS "  Terminal: ${TERMINAL_SRC}")
message(STATUS "  Build examples: ${BUILD_EXAMPLES}")
message(STATUS "  Build tests: ${BUILD_TESTS}")
message(STATUS "  Build shared lib: ${BUILD_SHARED_LIBS}")
message(STATUS "")
