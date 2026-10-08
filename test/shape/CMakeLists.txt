# The probe unit of the core families.
#
# core_shapes.cpp pairs each hot operation of include/foundation/core with
# the raw form that it replaces.  The rule codegen-shapes
# (utils/assay/guard/engines/elf/codegen_shapes.py) compiles the unit with the
# command of the object of this directory in the compile database, reads the
# disassembly, and compares the two functions of each pair with the
# expectation of the pair.  The build compiles the object too, so the
# quarantine plugin, the warnings and the compile budgets see the unit.
#
# The flags of the object are the same in each preset:
#   - The fixed flags below, with -O3, the optimization level of a Release
#     build, and -march=x86-64-v3, the minimum x86 target of CLAUDE.md
#     section XIV.  The rule reads the code and does not run it, so each x86
#     host gives the same disassembly.  With -march=native, the expectations
#     change with the host.
#   - The code flags and the definitions that each build takes, from the
#     lists of cmake/CodeFlags.cmake (crucible_code_target()).
#
# This directory empties CMAKE_CXX_FLAGS and its forms for each build type,
# and the object does not link crucible_dialect.  So no preset adds a level, a
# -march, a sanitizer or a hardening flag, and the Debug, Release and TSan
# builds give the same object.  The object takes the warning flags of the
# dialect, which do not change the code, and the FP floor of
# crucible_fp_strict.  The rule compares the code flags of the object with
# those of the units of the foundation library in the compile database.  The
# hardening flags of a Release build are not on the object: with them, 10 of
# the 21 probes do not hold, measured on 2026-10-08.
#
# The expectations are for x86_64.  On another architecture, the directory
# builds no object, and the rule does not apply.

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  return()
endif()

set(CMAKE_CXX_FLAGS "")
foreach(_core_shapes_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  set(CMAKE_CXX_FLAGS_${_core_shapes_config} "")
endforeach()

# The flags whose value changes with the preset in the rest of the tree.  The
# object takes one value of each.  FIXED_FLAGS of the rule codegen-shapes
# holds the same list, and the rule refuses a probe object with other values.
set(_core_shapes_fixed
  -O3 -march=x86-64-v3 -mtune=generic -DNDEBUG -fPIC -fno-strict-overflow -fcontract-evaluation-semantic=observe)

get_target_property(_core_shapes_warnings crucible_dialect INTERFACE_COMPILE_OPTIONS)
list(FILTER _core_shapes_warnings INCLUDE REGEX "^-W")

add_library(core_shape_probes OBJECT core_shapes.cpp)
target_compile_options(core_shape_probes PRIVATE
  ${_core_shapes_warnings} -fconstexpr-ops-limit=${CRUCIBLE_CONSTEXPR_OPS_LIMIT} -freflection -fcontracts
  ${_core_shapes_fixed})
crucible_code_target(core_shape_probes)
target_include_directories(core_shape_probes PRIVATE ${CMAKE_SOURCE_DIR}/include)
