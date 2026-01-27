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
