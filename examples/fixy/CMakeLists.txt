# ════════════════════════════════════════════════════════════════════
# examples/fixy/ — worked C++26 fixy::fn<...> demonstrations
#
# FIXY-B5 (Phase B finalization).  The reject-by-default analogue of
# examples/fn/.  Each example shows the SAME production-call-site
# shape using fixy::fn<Type, Grants...> instead of safety::fn::Fn<...>:
# every dim either relaxed via a `grant::*` tag or explicitly
# acknowledged-as-strict via `accept_default_strict_for<dim::X>`.
#
# Phase B ships ONE worked example:
#
#   1. example_custom_kernel  — user-supplied compute kernel, the
#                               reject-by-default analogue of
#                               examples/fn/example_custom_kernel.cpp
#
# Phase C will add the remaining four (optimizer, CNTP frame, Forge
# phase, Mimic backend hook) using the 8-stance canonical pre-baked
# packs from fixy/Stance.h so the production surface for greenfield
# Crucible headers is one `cs::mint_fn_for<cs::BgWorker>(...)` call,
# not 20 hand-tagged dim engagements.
#
# Each example is BOTH a build target (compiles under full warning
# matrix + contracts + reflection — a regression in fixy/Fn.h's
# interface surfaces at compile time here) and a ctest entry (runs
# main() — a regression in the value() forwarder shows up in
# `ctest --preset default`).
# ════════════════════════════════════════════════════════════════════

function(crucible_fixy_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name} COMMAND ${name})
  # Cluster under both "examples" (parity with examples/fn/) and
  # "fixy" (so `ctest -L fixy` runs the full reject-by-default
  # battery: fixy_neg fixtures + sentinel TUs + worked examples).
  set_tests_properties(${name} PROPERTIES LABELS "examples;fixy")
endfunction()

crucible_fixy_example(example_fixy_custom_kernel)
