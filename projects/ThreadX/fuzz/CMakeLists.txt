# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
set(CMAKE_C_STANDARD 99)

set(DISABLE_NEWLIB_STUB 1)

set(GSG_BASE_DIR ${CMAKE_SOURCE_DIR}/..)
set(SHARED_SRC_DIR ${GSG_BASE_DIR}/shared/src)
set(SHARED_LIB_DIR ${GSG_BASE_DIR}/shared/lib)

add_compile_options( -fprofile-instr-generate -fcoverage-mapping )

if(NOT CMAKE_TOOLCHAIN_FILE)
    if(DEFINED FUZZ)
        set(CMAKE_TOOLCHAIN_FILE "${GSG_BASE_DIR}/cmake/linux.cmake")
    endif()
    if(DEFINED NATIVE)
        set(CMAKE_TOOLCHAIN_FILE "${GSG_BASE_DIR}/cmake/linux.native.cmake")
    endif()
endif()

include(${GSG_BASE_DIR}/cmake/utilities.cmake)

project(threadx-fuzz C ASM)

add_subdirectory(${SHARED_SRC_DIR} shared_src)
add_subdirectory(lib)

if(DEFINED FUZZ)
    add_subdirectory(src)
endif()
if(DEFINED NATIVE)
    add_subdirectory(native)
endif()
