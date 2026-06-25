cmake_minimum_required(VERSION 3.10)

# FF-DEFLATE
# Copyright (c) 2026 FF-LEAF. All rights reserved.

project(FF-DEFLATE
    VERSION 0.1.0
    DESCRIPTION "Pure C++ lossless DEFLATE, ZIP, and PNG library"
    LANGUAGES CXX)

option(FF_DEFLATE_BUILD_TESTS "Build FF-DEFLATE tests" ON)
option(FF_DEFLATE_BUILD_EXAMPLES "Build FF-DEFLATE examples" ON)

add_library(ff-deflate STATIC
    src/ff_deflate.cpp)

add_library(FFDEFLATE::ff-deflate ALIAS ff-deflate)

target_include_directories(ff-deflate
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

target_compile_features(ff-deflate PUBLIC cxx_std_11)

if(MSVC)
    target_compile_options(ff-deflate PRIVATE /W4)
else()
    target_compile_options(ff-deflate PRIVATE -Wall -Wextra -pedantic)
endif()

if(FF_DEFLATE_BUILD_TESTS)
    enable_testing()
    add_executable(test_ff_deflate tests/test_ff_deflate.cpp)
    target_link_libraries(test_ff_deflate PRIVATE ff-deflate)
    add_test(NAME ff_deflate_roundtrip COMMAND test_ff_deflate)
endif()

if(FF_DEFLATE_BUILD_EXAMPLES)
    add_executable(ff_deflate_basic examples/basic.cpp)
    target_link_libraries(ff_deflate_basic PRIVATE ff-deflate)
endif()

include(GNUInstallDirs)

install(TARGETS ff-deflate
    EXPORT ff-deflate-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT ff-deflate-targets
    NAMESPACE FFDEFLATE::
    FILE FFDEFLATETargets.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/FF-DEFLATE)
