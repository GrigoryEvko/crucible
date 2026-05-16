# ════════════════════════════════════════════════════════════════════
# examples/ — worked C++26 substrate demonstrations
#
# Phase 0 P0-5 (#1098) ships `examples/fn/` — five worked examples
# of the safety::fn::Fn<...> aggregator at production-shape callsites
# (custom kernel, custom optimizer, CNTP frame, Forge phase, Mimic
# backend hook).
#
# Examples are EXECUTABLES, not tests.  They build under the same
# warning matrix as tests so a regression in Fn.h's interface
# surfaces here at compile time.  They also register as ctest entries
# so a regression in their RUNTIME behavior surfaces at `ctest`
# invocation.
#
# FIXY-B5 ships `examples/fixy/` — the reject-by-default analogue,
# one example for Phase B (custom_kernel) with the remaining four
# following in Phase C using fixy/Stance.h's 8 canonical stances.
#
# Future example trees (e.g., examples/session/, examples/permission/)
# follow the same structure: one subdirectory per substrate concept,
# each with its own CMakeLists.txt that the top-level wires here.
# ════════════════════════════════════════════════════════════════════

add_subdirectory(fn)
add_subdirectory(fixy)
