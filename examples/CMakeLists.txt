# ════════════════════════════════════════════════════════════════════
# examples/ — worked C++26 demonstrations of the substrate
#
# examples/fn/ holds five worked bindings of fixy::fn at call sites of
# production shape: a custom kernel, a custom optimizer, a CNTP frame, a
# Forge phase and a Mimic backend hook.
#
# Examples are executables, not tests.  They build under the same
# warning matrix as tests, so a change to the interface of fixy/Fn.h
# shows here at compile time.  They also register as ctest entries, so a
# change to their behavior at run time shows in the ctest output.
#
# A new example tree has the same structure: one subdirectory for each
# concept of the substrate, each with its own CMakeLists.txt, and one
# add_subdirectory line here.
# ════════════════════════════════════════════════════════════════════

add_subdirectory(fn)
