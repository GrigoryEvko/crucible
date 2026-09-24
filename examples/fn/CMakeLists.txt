# ════════════════════════════════════════════════════════════════════
# examples/fn/ — five worked Fn<...> aggregator demonstrations
#
# Phase 0 P0-5 (#1098).  Each example shows a distinct production
# call-site shape using safety::fn::Fn<...> with concrete per-axis
# grade choices and rationale comments:
#
#   1. example_custom_kernel       — user-supplied compute kernel
#   2. example_custom_optimizer    — user-supplied optimizer step
#   3. example_cntp_frame          — untrusted-input wire frame
#                                    (Source::External + retag)
#   4. example_forge_phase         — pure-functional IR transform
#                                    (Mutation::Immutable + Verified)
#   5. example_mimic_backend_hook  — vendor-specific kernel emitter
#                                    (largest EffectRow: Bg+Alloc+IO)
#
# Each example is BOTH:
#
#   (a) A build target — compiles under the project's full warning
#       matrix (-Werror=*, contracts, reflection, P2795R5), so a
#       regression in Fn.h's interface surfaces here at compile
#       time.
#   (b) A ctest entry — runs main() and validates the binding's
#       runtime behavior.  A regression in Fn.h's runtime surface
#       (e.g., value() returning a wrong reference category) shows
#       up in `ctest --preset default` output.
#
# The examples are INTENTIONALLY hand-written rather than generated
# from a parser — Phase 0 acceptance gate (misc/02_05_2026.md) is
# "the substrate works at production-shape callsites BEFORE any
# parser is built".
# ════════════════════════════════════════════════════════════════════

# ── Per-example target factory ──────────────────────────────────────
#
# Mirrors the test/ pattern but doesn't apply sanitizer flags
# (sanitizers are test-only by project convention; benches and
# examples ship without).  Examples link against the crucible
# header-only library exactly like tests do.
function(crucible_example name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name} COMMAND ${name})
  # Examples cluster under the "examples" label so a developer can
  # invoke `ctest -L examples` to run only the worked-example smoke
  # battery.
  set_tests_properties(${name} PROPERTIES LABELS "examples")
endfunction()

crucible_example(example_custom_kernel)
crucible_example(example_custom_optimizer)
crucible_example(example_cntp_frame)
crucible_example(example_forge_phase)
crucible_example(example_mimic_backend_hook)
