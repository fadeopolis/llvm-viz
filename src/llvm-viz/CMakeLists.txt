# This file is distributed under the Revised BSD Open Source License.
# See LICENSE.TXT for details.

cmake_minimum_required(VERSION 3.8)

##### generate stringified versions of JS & CSS frameworks

set(STRINGIFIED_SOURCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(STRINGIFIED_SOURCES)

function(add_stringified SRC NAME DESC)
  set(CPP "${STRINGIFIED_SOURCE_DIR}/${NAME}.cpp")
  set(HPP "${STRINGIFIED_SOURCE_DIR}/${NAME}.hpp")

  add_custom_command(
      OUTPUT "${CPP}" "${HPP}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${STRINGIFIED_SOURCE_DIR}"
      COMMAND stringify -name "${NAME}" -desc "${DESC}" -cpp "${CPP}" -hpp "${HPP}" -input "${SRC}"
      DEPENDS stringify "${SRC}"
      VERBATIM
  )
  list(APPEND STRINGIFIED_SOURCES "${CPP}" "${HPP}")
  set(STRINGIFIED_SOURCES "${STRINGIFIED_SOURCES}" PARENT_SCOPE)
endfunction(add_stringified)

add_stringified("${LLVM_VIZ_SOURCE_DIR}/external/jQuery/jquery-2.2.1.min.js"  jQuerySource       "the jQuery 2.2.1 source code")
add_stringified("${LLVM_VIZ_SOURCE_DIR}/external/bootstrap/bootstrap.min.js"  BootstrapJsSource  "the Bootstrap 3.3.6 JS code")
add_stringified("${LLVM_VIZ_SOURCE_DIR}/external/bootstrap/bootstrap.min.css" BootstrapCssSource "the Bootstrap 3.3.6 CSS code")

set_directory_properties(ADDITIONAL_MAKE_CLEAN_FILES ${STRINGIFIED_SOURCES})

##### add targets

## LLVM-IR to HTML converter
add_executable(llvm-viz
    main.cpp
    HtmlUtils.cpp
  ValueNameMangler.cpp
    CfgToDot.cpp
    Style.cpp
    ${STRINGIFIED_SOURCES}
)
target_compile_options(llvm-viz PRIVATE ${LLVM_VIZ_CXX_FLAGS})
target_include_directories(llvm-viz PRIVATE ${LLVM_VIZ_INCLUDE_DIRECTORIES} "${CMAKE_CURRENT_BINARY_DIR}")

llvm_map_components_to_libnames(LLVM_LIBS
    core
    support
    analysis
    irreader
)
target_link_libraries(llvm-viz PRIVATE llvm-viz-support ${LLVM_LIBS})

##### INSTALL TARGETS

install(
    TARGETS llvm-viz
    DESTINATION bin
)
