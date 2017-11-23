# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.8)

add_executable(stringify stringify.cpp)
target_compile_options(stringify PRIVATE ${LLVM_VIZ_CXX_FLAGS})
target_include_directories(stringify PRIVATE ${LLVM_VIZ_INCLUDE_DIRECTORIES})
target_link_libraries(stringify PRIVATE llvm-viz-support)
