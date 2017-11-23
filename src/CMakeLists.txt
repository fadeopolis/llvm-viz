# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.8)

add_subdirectory(support)
add_subdirectory(stringify)
add_subdirectory(llvm-viz)

if(GTest_FOUND)
  enable_testing()
endif()
