# The layer rule, enforced by the compiler.
#
# foundation names only foundation and std, and fixy names only foundation,
# fixy and std.  utils/scripts/check-layer-boundary.py reads that rule from the
# parse tree.  This directory makes the compiler hold it too: a header that
# includes or names a higher layer does not compile, whatever the spelling.
#
# Each layer gets its own include root in the build tree.  The root holds a
# symbolic link to the real include directory of each layer that the layer
# may name, so nothing is copied.  One sentinel translation unit per public
# header includes that header, alone, against the root of its layer.  An
# include of a higher layer then does not resolve, and a name of a higher
# layer then has no declaration to find.  The sentinels also prove that each
# header compiles alone.  They are in the default build, so a break stops
# the build and not only a test.
#
# The two negative fixtures show that the sentinel fails: a foundation
# header that includes a fixy header, and a foundation header that names a
# fixy type through a declaration that it expects its includer to supply.

set(_layer_roots "${CMAKE_BINARY_DIR}/layer-roots")

# The layers that each layer may name, lowest first.
set(_layer_names_foundation foundation)
set(_layer_names_fixy foundation fixy)

foreach(_layer foundation fixy)
  set(_root "${_layer_roots}/${_layer}")
  file(MAKE_DIRECTORY "${_root}")
  foreach(_named IN LISTS _layer_names_${_layer})
    file(REMOVE "${_root}/${_named}")
    file(CREATE_LINK "${CMAKE_SOURCE_DIR}/include/${_named}" "${_root}/${_named}" SYMBOLIC)
  endforeach()

  # One sentinel per public header.  file(CONFIGURE) rewrites a sentinel only
  # when its text changes, so a configure run does not force a rebuild.
  file(GLOB_RECURSE _headers CONFIGURE_DEPENDS
       RELATIVE "${CMAKE_SOURCE_DIR}/include" "${CMAKE_SOURCE_DIR}/include/${_layer}/*.h")
  list(SORT _headers)
  set(_sentinels "")
  foreach(_header IN LISTS _headers)
    string(MAKE_C_IDENTIFIER "${_header}" _stem)
    set(_sentinel "${CMAKE_CURRENT_BINARY_DIR}/sentinels/${_layer}/${_stem}.cpp")
    file(CONFIGURE OUTPUT "${_sentinel}" CONTENT "#include <${_header}>\n" @ONLY)
    list(APPEND _sentinels "${_sentinel}")
  endforeach()
  if(NOT _sentinels)
    message(FATAL_ERROR "test/layer: include/${_layer} has no header, so the layer sentinel would compile nothing")
  endif()

  add_library(layer_sentinel_${_layer} OBJECT ${_sentinels})
  target_link_libraries(layer_sentinel_${_layer} PRIVATE crucible_dialect)
  target_include_directories(layer_sentinel_${_layer} PRIVATE "${_root}")
endforeach()

# A fixture compiles against the root of the foundation layer, as a
# foundation header would, and the test passes when the compiler refuses it
# for the named reason.
function(layer_neg_compile_test name)
  add_executable(${name} EXCLUDE_FROM_ALL ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible_dialect)
  target_include_directories(${name} PRIVATE "${_layer_roots}/foundation")
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/test/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/${name}.cpp
            ${name}
            ${ARGN})
  set_tests_properties(${name} PROPERTIES LABELS "layer_neg")
endfunction()

layer_neg_compile_test(neg_layer_foundation_includes_fixy
  "fixy/Secret.h: No such file or directory"
  "compilation terminated")
layer_neg_compile_test(neg_layer_foundation_names_fixy
  "fixy. in namespace .::. does not name a type"
  "neg_layer_foundation_names_fixy.cpp:8")
