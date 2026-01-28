cmake_minimum_required(VERSION 3.10)
project(linenoise VERSION 2.0.0 LANGUAGES C)

# C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler warnings
if(MSVC)
    add_compile_options(/W4)
    # Suppress deprecation warnings for POSIX function names like strdup, stricmp, etc.
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Tree-sitter support option
option(WITH_TREESITTER "Build with tree-sitter syntax highlighting support" ON)

# Platform-specific terminal implementation
if(WIN32)
    set(TERMINAL_SRC src/internal/terminal_windows.c)
else()
    set(TERMINAL_SRC src/internal/terminal_posix.c)
endif()

# Module source files
set(MODULE_SOURCES
    src/internal/utf8.c
    src/internal/history.c
    src/internal/completion.c
    src/internal/keyparser.c
    src/internal/render.c
)

# Core library source files
set(LINENOISE_SOURCES
    src/linenoise.c
    ${MODULE_SOURCES}
    ${TERMINAL_SRC}
)

# Linenoise library (static by default)
add_library(linenoise STATIC ${LINENOISE_SOURCES})

target_include_directories(linenoise
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
)

# Shared library option
option(BUILD_SHARED_LIBS "Build shared library" OFF)
if(BUILD_SHARED_LIBS)
    add_library(linenoise_shared SHARED ${LINENOISE_SOURCES})
    target_include_directories(linenoise_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:include>
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    )
    set_target_properties(linenoise_shared PROPERTIES OUTPUT_NAME linenoise)
endif()

# Example application
option(BUILD_EXAMPLES "Build example application" ON)
if(BUILD_EXAMPLES)
    add_executable(linenoise-example test/example.c)
    target_link_libraries(linenoise-example PRIVATE linenoise)
endif()

# Tree-sitter based examples with syntax highlighting
if(WITH_TREESITTER AND BUILD_EXAMPLES)
    # Tree-sitter library sources
    set(TREESITTER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter)
    set(TREESITTER_LUA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-lua)
    set(TREESITTER_PYTHON_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-python)
    set(TREESITTER_SCHEME_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-scheme)
    set(TREESITTER_HASKELL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-haskell)
    set(TREESITTER_FORTH_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-forth)
    set(TREESITTER_FAUST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-faust)
    set(TREESITTER_CHUCK_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-chuck)
    set(TREESITTER_MARKDOWN_DIR ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/tree-sitter-markdown)

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
    if(MSVC)
        target_compile_options(tree-sitter PRIVATE /w)
    else()
        target_compile_options(tree-sitter PRIVATE -w)
    endif()

    # Syntax highlighting theme library
    add_library(linenoise-theme STATIC
        src/syntax/theme.c
    )
    target_include_directories(linenoise-theme
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

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
    if(MSVC)
        target_compile_options(tree-sitter-lua PRIVATE /w)
    else()
        target_compile_options(tree-sitter-lua PRIVATE -w)
    endif()

    # Lua highlighter module
    add_library(linenoise-highlight-lua STATIC
        src/syntax/lua.c
    )
    target_include_directories(linenoise-highlight-lua
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-lua
        PRIVATE linenoise-theme tree-sitter tree-sitter-lua
    )

    # Lua REPL example with syntax highlighting
    add_executable(linenoise-lua test/example-lua.c)
    target_include_directories(linenoise-lua
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(linenoise-lua
        PRIVATE linenoise linenoise-highlight-lua tree-sitter tree-sitter-lua
    )

    message(STATUS "  Tree-sitter Lua example: ON")

    # Build tree-sitter-python grammar
    add_library(tree-sitter-python STATIC
        ${TREESITTER_PYTHON_DIR}/src/parser.c
        ${TREESITTER_PYTHON_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-python
        PRIVATE
            ${TREESITTER_PYTHON_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-python PRIVATE /w)
    else()
        target_compile_options(tree-sitter-python PRIVATE -w)
    endif()

    # Python highlighter module
    add_library(linenoise-highlight-python STATIC
        src/syntax/python.c
    )
    target_include_directories(linenoise-highlight-python
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-python
        PRIVATE linenoise-theme tree-sitter tree-sitter-python
    )

    message(STATUS "  Tree-sitter Python: ON")

    # Build tree-sitter-scheme grammar
    add_library(tree-sitter-scheme STATIC
        ${TREESITTER_SCHEME_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-scheme
        PRIVATE
            ${TREESITTER_SCHEME_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-scheme PRIVATE /w)
    else()
        target_compile_options(tree-sitter-scheme PRIVATE -w)
    endif()

    # Scheme highlighter module
    add_library(linenoise-highlight-scheme STATIC
        src/syntax/scheme.c
    )
    target_include_directories(linenoise-highlight-scheme
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-scheme
        PRIVATE linenoise-theme tree-sitter tree-sitter-scheme
    )

    message(STATUS "  Tree-sitter Scheme: ON")

    # Build tree-sitter-haskell grammar
    add_library(tree-sitter-haskell STATIC
        ${TREESITTER_HASKELL_DIR}/src/parser.c
        ${TREESITTER_HASKELL_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-haskell
        PRIVATE
            ${TREESITTER_HASKELL_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-haskell PRIVATE /w)
    else()
        target_compile_options(tree-sitter-haskell PRIVATE -w)
    endif()

    # Haskell highlighter module
    add_library(linenoise-highlight-haskell STATIC
        src/syntax/haskell.c
    )
    target_include_directories(linenoise-highlight-haskell
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-haskell
        PRIVATE linenoise-theme tree-sitter tree-sitter-haskell
    )

    message(STATUS "  Tree-sitter Haskell: ON")

    # Build tree-sitter-forth grammar
    add_library(tree-sitter-forth STATIC
        ${TREESITTER_FORTH_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-forth
        PRIVATE
            ${TREESITTER_FORTH_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-forth PRIVATE /w)
    else()
        target_compile_options(tree-sitter-forth PRIVATE -w)
    endif()

    # Forth highlighter module
    add_library(linenoise-highlight-forth STATIC
        src/syntax/forth.c
    )
    target_include_directories(linenoise-highlight-forth
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-forth
        PRIVATE linenoise-theme tree-sitter tree-sitter-forth
    )

    message(STATUS "  Tree-sitter Forth: ON")

    # Build tree-sitter-faust grammar
    add_library(tree-sitter-faust STATIC
        ${TREESITTER_FAUST_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-faust
        PRIVATE
            ${TREESITTER_FAUST_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-faust PRIVATE /w)
    else()
        target_compile_options(tree-sitter-faust PRIVATE -w)
    endif()

    # Faust highlighter module
    add_library(linenoise-highlight-faust STATIC
        src/syntax/faust.c
    )
    target_include_directories(linenoise-highlight-faust
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-faust
        PRIVATE linenoise-theme tree-sitter tree-sitter-faust
    )

    message(STATUS "  Tree-sitter Faust: ON")

    # Build tree-sitter-chuck grammar
    add_library(tree-sitter-chuck STATIC
        ${TREESITTER_CHUCK_DIR}/src/parser.c
    )
    target_include_directories(tree-sitter-chuck
        PRIVATE
            ${TREESITTER_CHUCK_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-chuck PRIVATE /w)
    else()
        target_compile_options(tree-sitter-chuck PRIVATE -w)
    endif()

    # ChucK highlighter module
    add_library(linenoise-highlight-chuck STATIC
        src/syntax/chuck.c
    )
    target_include_directories(linenoise-highlight-chuck
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-chuck
        PRIVATE linenoise-theme tree-sitter tree-sitter-chuck
    )

    message(STATUS "  Tree-sitter ChucK: ON")

    # Build tree-sitter-markdown grammar
    add_library(tree-sitter-markdown STATIC
        ${TREESITTER_MARKDOWN_DIR}/src/parser.c
        ${TREESITTER_MARKDOWN_DIR}/src/scanner.c
    )
    target_include_directories(tree-sitter-markdown
        PRIVATE
            ${TREESITTER_MARKDOWN_DIR}/src
            ${TREESITTER_DIR}/lib/include
    )
    if(MSVC)
        target_compile_options(tree-sitter-markdown PRIVATE /w)
    else()
        target_compile_options(tree-sitter-markdown PRIVATE -w)
    endif()

    # Markdown highlighter module
    add_library(linenoise-highlight-markdown STATIC
        src/syntax/markdown.c
    )
    target_include_directories(linenoise-highlight-markdown
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
            ${TREESITTER_DIR}/lib/include
    )
    target_link_libraries(linenoise-highlight-markdown
        PRIVATE linenoise-theme tree-sitter tree-sitter-markdown
    )

    message(STATUS "  Tree-sitter Markdown: ON")

    # Multi-language syntax highlighting test
    add_executable(linenoise-syntax test/example-syntax.c)
    target_include_directories(linenoise-syntax
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(linenoise-syntax
        PRIVATE
            linenoise
            linenoise-theme
            linenoise-highlight-lua
            linenoise-highlight-python
            linenoise-highlight-scheme
            linenoise-highlight-haskell
            linenoise-highlight-forth
            linenoise-highlight-faust
            linenoise-highlight-chuck
            linenoise-highlight-markdown
            tree-sitter
            tree-sitter-lua
            tree-sitter-python
            tree-sitter-scheme
            tree-sitter-haskell
            tree-sitter-forth
            tree-sitter-faust
            tree-sitter-chuck
            tree-sitter-markdown
    )

    message(STATUS "  Multi-language syntax test: ON")
endif()

# Tests (POSIX only - uses fork/waitpid/select)
option(BUILD_TESTS "Build test suite" ON)
if(BUILD_TESTS AND NOT WIN32)
    enable_testing()

    add_executable(linenoise-test test/linenoise-test.c src/internal/utf8.c)
    target_include_directories(linenoise-test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    )

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

install(FILES include/linenoise.h
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
