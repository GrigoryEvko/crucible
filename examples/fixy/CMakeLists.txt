# ════════════════════════════════════════════════════════════════════
# examples/fixy/ — worked C++26 fixy::fn<...> demonstrations
#
# FIXY-B5 (Phase B finalization).  The reject-by-default analogue of
# examples/fn/.  Each example shows the SAME production-call-site
# shape using fixy::fn<Type, Grants...> instead of safety::fn::Fn<...>:
# every dim either relaxed via a `grant::*` tag or explicitly
# acknowledged-as-strict via `accept_default_strict_for<dim::X>`.
#
# Phase B + E ship FIVE worked examples — one per
# examples/fn/example_*.cpp:
#
#   1. example_fixy_custom_kernel       — user-supplied compute kernel
#                                         (Phase B / FIXY-B5)
#   2. example_fixy_custom_optimizer    — Adam optimizer step
#                                         (Phase E / FIXY-E)
#   3. example_fixy_cntp_frame          — untrusted wire frame retag
#                                         (Phase E / FIXY-E)
#   4. example_fixy_forge_phase         — Forge phase D FUSE
#                                         (Phase E / FIXY-E)
#   5. example_fixy_mimic_backend_hook  — Mimic NV SASS emit
#                                         (Phase E / FIXY-E)
#   6. example_fixy_cipher_writer       — Cipher cold-tier append-only
#                                         writer (Phase E ext / FIXY-E2)
#
# Each example is the reject-by-default analogue of its
# examples/fn/example_*.cpp peer.  Both forms compile under the
# project's full warning+contract+reflection matrix; reading them
# side-by-side surfaces the 20-axis grade vector explicitly (fixy)
# vs positionally (substrate).
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
crucible_fixy_example(example_fixy_custom_optimizer)
crucible_fixy_example(example_fixy_cntp_frame)
crucible_fixy_example(example_fixy_forge_phase)
crucible_fixy_example(example_fixy_mimic_backend_hook)
# Cipher cold-tier append-only writer — Mutation=Append stance with
# Bg+IO+Block effect row.  Demonstrates Crucible's event-sourced
# persistence binding shape (CRUCIBLE.md §14, Cipher 3-tier).  No
# examples/fn/ peer today — Cipher entry points still consume raw
# safety::* types; when they migrate to fn<>, the contrast fills in.
crucible_fixy_example(example_fixy_cipher_writer)

# FIXY-G9 — Witness metadata progressive upgrade.
crucible_fixy_example(example_fixy_witness_evidenced)

# FIXY-G10 — Modality-classified Mimic NV emit binding.
crucible_fixy_example(example_fixy_modality_aware)

# FIXY-G11 — cost-semiring + dim::Cost worked example.
crucible_fixy_example(example_fixy_cost_annotated)

# FIXY-G12 — termination + bounded-resource axis worked example.
crucible_fixy_example(example_fixy_bounded)

# FIXY-G13 — stance versioning + lifecycle worked example.
crucible_fixy_example(example_fixy_stance_versioned)

# FIXY-G14 — seam theory matcher keystone worked example.
crucible_fixy_example(example_fixy_seam_safety)
