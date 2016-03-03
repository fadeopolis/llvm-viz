# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.3)
project(llvm_viz)

##### BASIC SANITY CHECKS

if("${CMAKE_BINARY_DIR}" STREQUAL "${PROJECT_SOURCE_DIR}")
  message(FATAL_ERROR "You tried to do an in source build. Please don't.")
endif()


##### DEPENDENCIES

## LLVM
find_package(LLVM 3.9 REQUIRED)

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

## google testing
find_package(GTest QUIET)

##### GENERAL COMPILER & TOOLS FLAGS

### add some C++ compiler flags
## unfortunately we can't use add_compile_options here since that would also add them to C files
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")

include_directories(.)
include_directories("${CMAKE_CURRENT_BINARY_DIR}/gen")

## warn about ALL the things :)
option(LLVM_VIZ_WALL "Use -Wall compiler flag" ON)
if(LLVM_VIZ_WALL)
  add_compile_options("-Wall")
endif()

option(LLVM_VIZ_WERROR "Use -Werror compiler flag" OFF)
if(LLVM_VIZ_WERROR)
  add_compile_options("-Werror")
endif()

## TODO: remove
add_compile_options("-Wno-unused-function")
add_compile_options("-Wno-unused-variable")

## pretty colors in terminal
option(LLVM_VIZ_PRETTY_DIAGNOSTICS "Enable pretty command line diagnostics" OFF)
if(LLVM_VIZ_PRETTY_DIAGNOSTICS)
  ## CLANG
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fcolor-diagnostics")
  endif()

  ## GCC
  if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always")
  endif()
endif()

##### ADD TARGETS

## helper program that turns files into C++ string literals
add_executable(stringify stringify.cpp)

llvm_map_components_to_libnames(LLVM_LIBS
    core support)
target_link_libraries(stringify ${LLVM_LIBS})

llvm_map_components_to_libnames(LLVM_LIBS
    core support
    analysis
    irreader)
target_link_libraries(stringify ${LLVM_LIBS})

set(STRINGIFIED_SOURCES)

function(add_stringified SRC NAME DESC)
  set(CPP "${CMAKE_CURRENT_BINARY_DIR}/gen/${NAME}.cpp")
  set(HPP "${CMAKE_CURRENT_BINARY_DIR}/gen/${NAME}.hpp")

  add_custom_command(
      OUTPUT "${CPP}" "${HPP}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/gen"
      COMMAND stringify -name "${NAME}" -desc "${DESC}" -cpp "${CPP}" -hpp "${HPP}" -input "${CMAKE_CURRENT_SOURCE_DIR}/${SRC}"
      DEPENDS stringify "${SRC}"
      VERBATIM
  )
  list(APPEND STRINGIFIED_SOURCES "${CPP}" "${HPP}")
  set(STRINGIFIED_SOURCES "${STRINGIFIED_SOURCES}" PARENT_SCOPE)
endfunction(add_stringified)

add_stringified("jquery-2.2.1.min.js" jQuerySource       "the jQuery 2.2.1 source code")
add_stringified("bootstrap.min.js"    BootstrapJsSource  "the Bootstrap 3.3.6 JS code")
add_stringified("bootstrap.min.css"   BootstrapCssSource "the Bootstrap 3.3.6 CSS code")

set_directory_properties(ADDITIONAL_MAKE_CLEAN_FILES ${STRINGIFIED_SOURCES})

## LLVM-IR to HTML converter
add_executable(llvm-viz
    main.cpp
    HtmlUtils.cpp
    PassUtils.cpp
    PrintUtils.cpp
    Style.cpp
    ${STRINGIFIED_SOURCES})

llvm_map_components_to_libnames(LLVM_LIBS
    core support
    analysis
    irreader)
target_link_libraries(llvm-viz ${LLVM_LIBS})

## add tests
if(GTest_FOUND)
  enable_testing()
  add_subdirectory(test)
endif()

##### INSTALL TARGETS

install(
    TARGETS llvm-viz
    DESTINATION bin
)
