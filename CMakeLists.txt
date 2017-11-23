# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.8)
project(llvm_viz)

##### BASIC SANITY CHECKS

if("${CMAKE_BINARY_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
  message(FATAL_ERROR "You tried to do an in source build. Please don't.")
endif()


##### DEPENDENCIES

## LLVM
find_package(LLVM 5.0 REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")

## google testing
find_package(GTest QUIET)

##### GENERAL COMPILER & TOOLS FLAGS

set(LLVM_VIZ_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src")

set(LLVM_VIZ_CXX_FLAGS ${CMAKE_CXX_FLAGS})
# set(LLVM_VIZ_DEFINITIONS         "${LLVM_DEFINITIONS}")
set(LLVM_VIZ_INCLUDE_DIRECTORIES "${LLVM_VIZ_SOURCE_DIR}" "${LLVM_INCLUDE_DIRS}")

### add common LLVM flags
list(APPEND LLVM_VIZ_CXX_FLAGS "-std=c++14")
list(APPEND LLVM_VIZ_CXX_FLAGS "-fno-rtti")
list(APPEND LLVM_VIZ_CXX_FLAGS "-fno-exceptions")

## warn about ALL the things :)
option(LLVM_VIZ_WALL "Use -Wall compiler flag" ON)
if(LLVM_VIZ_WALL)
  list(APPEND LLVM_VIZ_CXX_FLAGS "-Wall")
endif()

option(LLVM_VIZ_WERROR "Use -Werror compiler flag" OFF)
if(LLVM_VIZ_WERROR)
  list(APPEND LLVM_VIZ_CXX_FLAGS "-Werror")
endif()

## TODO: remove
list(APPEND LLVM_VIZ_CXX_FLAGS "-Wno-unused-function")
list(APPEND LLVM_VIZ_CXX_FLAGS "-Wno-unused-variable")

## pretty colors in terminal
option(LLVM_VIZ_PRETTY_DIAGNOSTICS "Enable pretty command line diagnostics" OFF)
if(LLVM_VIZ_PRETTY_DIAGNOSTICS)
  ## CLANG
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    list(APPEND LLVM_VIZ_CXX_FLAGS "-fcolor-diagnostics")
  endif()

  ## GCC
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    list(APPEND LLVM_VIZ_CXX_FLAGS "-fdiagnostics-color=always")
  endif()
endif()

# put all generated libraries and executables in one place
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

##### ADD TARGETS

if(GTest_FOUND)
  enable_testing()
endif()

add_subdirectory(src)

