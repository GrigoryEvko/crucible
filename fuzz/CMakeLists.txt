# ═══════════════════════════════════════════════════════════════
# fuzz/CMakeLists.txt
#
# Two flavors of stress testing:
#
#   1. property/   — E2E correctness fuzzers using Philox-derived
#                    randomness; check invariants (determinism,
#                    collision avoidance, round-trip equality,
#                    pool intern correctness, avalanche, differential
#                    equivalence).  No external dependency.
#
#   2. boundary/   — adversarial-input parser harnesses with the
#                    libFuzzer-compatible LLVMFuzzerTestOneInput
#                    signature.  Standalone replay driver runs
#                    against a checked-in corpus for ctest smoke;
#                    AFL++ in QEMU mode (or libFuzzer with Clang)
#                    can drive the same entry point for coverage-
#                    guided extended fuzzing.
#
# Both inherit the project's sanitizer flags (ASan + UBSan +
# bounds-strict in the default preset) so any invariant violation
# at runtime aborts with a sanitizer report identifying the cause.
# ═══════════════════════════════════════════════════════════════

# ── Sanitizer flags (self-contained — not hoisted from test/) ──
#
# Every fuzz binary runs under ASan + UBSan + bounds-strict so any
# invariant violation at runtime aborts with a sanitizer report.
# This mirrors test/'s default preset configuration but is set up
# here so fuzz/ doesn't depend on add_subdirectory(test) side-effects.
set(CRUCIBLE_FUZZ_SAN_COMPILE
  -fsanitize=address
  -fsanitize=undefined,bounds-strict
  -fno-sanitize-recover=all
  -fno-omit-frame-pointer
)
set(CRUCIBLE_FUZZ_SAN_LINK
  -fsanitize=address,undefined,bounds-strict
)

# ── Property-based fuzzers ─────────────────────────────────────

function(crucible_property_fuzzer name)
  add_executable(${name} property/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/property)
  target_compile_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_COMPILE})
  target_link_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_LINK})

  # Smoke iterations — fast, suitable for ctest.  Developers running
  # extended stress can pass --iters=N on the command line.
  add_test(NAME ${name} COMMAND ${name} --iters=1000)
  set_tests_properties(${name} PROPERTIES LABELS "fuzz_property")
endfunction()

crucible_property_fuzzer(prop_hash_determinism)
crucible_property_fuzzer(prop_hash_distinct)
crucible_property_fuzzer(prop_recipe_pool_intern)
crucible_property_fuzzer(prop_reflect_avalanche)
crucible_property_fuzzer(prop_diff_reflect_vs_packed)
crucible_property_fuzzer(prop_recipe_hashed_idempotent)
crucible_property_fuzzer(prop_content_hash_determinism)
crucible_property_fuzzer(prop_content_hash_recipe_disambiguates)
crucible_property_fuzzer(prop_recipe_registry_roundtrip)
crucible_property_fuzzer(prop_arena_alloc_invariants)
crucible_property_fuzzer(prop_header_roundtrip)

# ── Boundary fuzzers (adversarial input) ───────────────────────

function(crucible_boundary_fuzzer name)
  add_executable(${name} boundary/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/boundary)
  target_compile_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_COMPILE})
  target_link_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_LINK})

  # Smoke test: replay every file in the boundary corpus directory.
  add_test(
    NAME ${name}
    COMMAND ${name} ${CMAKE_CURRENT_SOURCE_DIR}/boundary/corpus
  )
  set_tests_properties(${name} PROPERTIES LABELS "fuzz_boundary")
endfunction()

crucible_boundary_fuzzer(fuzz_serialize)
crucible_boundary_fuzzer(fuzz_recipe_bytes)
