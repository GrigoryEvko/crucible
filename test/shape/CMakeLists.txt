# The probe unit of the core families.
#
# core_shapes.cpp pairs each hot operation of include/foundation/core with
# the raw form that it replaces.  The rule codegen-shapes
# (utils/assay/guard/engines/elf/codegen_shapes.py) compiles the unit with the
# command of the object of this directory in the compile database, reads the
# disassembly, and compares the two functions of each pair.  The build
# compiles the object too, and the quarantine plugin, the warnings and the
# compile budgets then see the unit.
#
# The object takes the flags of the foundation library in each build: it
# links crucible_dialect and it is position-independent, as foundation is.
# Each preset then compiles the two functions of a pair with its own level,
# target, sanitizer, hardening flags and contract semantic, and the rule
# compares the pair with the flags of that build.  The rule refuses a probe
# object with code flags that are not the code flags of the units of the
# foundation library.
#
# The expectations are for x86_64.  On another architecture, the directory
# builds no object, and the rule does not apply.

if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  return()
endif()

add_library(core_shape_probes OBJECT core_shapes.cpp)
target_include_directories(core_shape_probes PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(core_shape_probes PRIVATE crucible_dialect)
set_target_properties(core_shape_probes PROPERTIES POSITION_INDEPENDENT_CODE ON)
