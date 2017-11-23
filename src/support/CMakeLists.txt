# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.8)

add_library(llvm-viz-support
  PrintUtils.cpp PrintUtils.hpp
  safe_ptr.hpp
  VectorAppender.hpp
)
target_compile_options(llvm-viz-support PUBLIC ${LLVM_VIZ_CXX_FLAGS})
target_include_directories(llvm-viz-support PUBLIC ${LLVM_VIZ_INCLUDE_DIRECTORIES})

llvm_map_components_to_libnames(LLVM_LIBS
  support
)
target_link_libraries(llvm-viz-support ${LLVM_LIBS})
