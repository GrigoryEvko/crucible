# The shape check of the core families.
#
# core_shapes.cpp pairs each hot operation of include/foundation/core with
# the raw form that it replaces.  utils/scripts/check-core-shapes.py compiles
# the unit with the command of the object of this directory in the compile
# database, reads the disassembly, and compares the two functions of each pair
# with the expectation of the pair.  The build compiles the object too, so the
# quarantine plugin, the warnings and the compile budgets see the unit.
#
# The flags of the object are the same in each preset:
#   - -O3, the optimization level of a Release build.
#   - The code generation flags that each build of the tree takes.
#   - -march=x86-64-v3, the minimum x86 target of CLAUDE.md section XIV.  The
#     check reads the code and does not run it, so each x86 host gives the
#     same disassembly.  With -march=native, the expectations change with
#     the host.
#
# This directory empties CMAKE_CXX_FLAGS and its forms for each build type,
# and the object does not link crucible_dialect.  So no preset adds a level, a
# -march, a sanitizer or a hardening flag, and the Debug, Release and TSan
# builds give the same object.  The object takes the warning flags of the
# dialect, which do not change the code, and the FP floor of
# crucible_fp_strict.  The check compares the code generation flags of the
# object with those of the units of the foundation library in the compile
# database.  When the base flags of the tree change, the check fails until
# the list below follows them.
#
# The expectations are for x86_64.  On another architecture, the directory
# builds no object, and each test exits with status 3, which ctest reports as
# a skip.

set(_core_shapes_script ${CMAKE_SOURCE_DIR}/utils/scripts/check-core-shapes.py)

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  add_test(NAME core_shapes COMMAND python3 ${_core_shapes_script})
  add_test(NAME core_shapes_self_test COMMAND python3 ${_core_shapes_script} --self-test)
  set_tests_properties(core_shapes core_shapes_self_test PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)
  return()
endif()

set(CMAKE_CXX_FLAGS "")
foreach(_core_shapes_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_CXX_FLAGS_${_core_shapes_config} "")
endforeach()

# The flags whose value changes with the preset in the rest of the tree.  The
# object takes one value of each.  The check gets this list, and it leaves
# these flags out of the comparison with the foundation units.
set(_core_shapes_fixed
  -O3 -march=x86-64-v3 -mtune=generic -DNDEBUG -fPIC -fno-strict-overflow -fcontract-evaluation-semantic=observe)

# The code generation flags of crucible_dialect that each build takes.
set(_core_shapes_base
  -freflection -fcontracts -ftrivial-auto-var-init=zero -fno-omit-frame-pointer -fstack-protector-strong -fno-common
  -fstrict-flex-arrays=3 -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fno-plt
  -fno-semantic-interposition -fsized-deallocation -fcf-protection=full -fstack-clash-protection
  -fno-delete-null-pointer-checks -fstrict-enums)

get_target_property(_core_shapes_warnings crucible_dialect INTERFACE_COMPILE_OPTIONS)
list(FILTER _core_shapes_warnings INCLUDE REGEX "^-W")

add_library(core_shape_probes OBJECT core_shapes.cpp)
target_compile_options(core_shape_probes PRIVATE
  ${_core_shapes_warnings} -fconstexpr-ops-limit=${CRUCIBLE_CONSTEXPR_OPS_LIMIT}
  ${_core_shapes_base} ${_core_shapes_fixed})
target_compile_definitions(core_shape_probes PRIVATE _FORTIFY_SOURCE=3)
target_include_directories(core_shape_probes PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(core_shape_probes PRIVATE crucible_fp_strict)

list(JOIN _core_shapes_fixed " " _core_shapes_fixed_text)
set(_core_shapes_arguments
  --build-dir ${CMAKE_BINARY_DIR}
  --probe-object $<TARGET_OBJECTS:core_shape_probes>
  --base-objects $<TARGET_OBJECTS:foundation>
  --fixed-flags "${_core_shapes_fixed_text}")

# The check: the flags of the object, then the shape of each pair.
add_test(NAME core_shapes
  COMMAND python3 ${_core_shapes_script} ${_core_shapes_arguments}
  COMMAND_EXPAND_LISTS)

# The self-test: each finding of the check on planted disassembly, each
# finding of the flag comparison on planted compile commands, and the object
# compiled again with a second bounds test planted in View::window.
add_test(NAME core_shapes_self_test
  COMMAND python3 ${_core_shapes_script} --self-test ${_core_shapes_arguments}
  COMMAND_EXPAND_LISTS)

set_tests_properties(core_shapes core_shapes_self_test PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)
