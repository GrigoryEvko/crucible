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
crucible_property_fuzzer(prop_memory_plan_no_overlap)
crucible_property_fuzzer(prop_trace_graph_csr)
crucible_property_fuzzer(prop_iteration_detector)
crucible_property_fuzzer(prop_intervals_cover_unit)
crucible_property_fuzzer(prop_factorization_eq)
crucible_property_fuzzer(prop_is_power_of_two_le)
crucible_property_fuzzer(prop_monotonic_predicates)
crucible_property_fuzzer(prop_fractional_lattice)
crucible_property_fuzzer(prop_header_roundtrip)
crucible_property_fuzzer(prop_philox_determinism)
crucible_property_fuzzer(prop_philox_simd_equivalence)
crucible_property_fuzzer(prop_storage_nbytes_simd_equivalence)
crucible_property_fuzzer(prop_compute_storage_nbytes_saturation)
crucible_property_fuzzer(prop_kernel_cache_roundtrip)
crucible_property_fuzzer(prop_callsite_table_dedup)
crucible_property_fuzzer(prop_expr_pool_intern)
crucible_property_fuzzer(prop_trace_ring_spsc)
crucible_property_fuzzer(prop_saturate_math_invariants)
crucible_property_fuzzer(prop_guard_hash_invariants)
crucible_property_fuzzer(prop_integrity_xxhash)
crucible_property_fuzzer(prop_hdr_histogram_buckets)
crucible_property_fuzzer(prop_sdc_tolerance)
crucible_property_fuzzer(prop_checked_arith)
crucible_property_fuzzer(prop_hlc_update)
crucible_property_fuzzer(prop_vector_clock)
crucible_property_fuzzer(prop_swim_membership)
crucible_property_fuzzer(prop_crdt_gset)
crucible_property_fuzzer(prop_crdt_orset)
crucible_property_fuzzer(prop_crdt_counter)
crucible_property_fuzzer(prop_crdt_mvregister)
crucible_property_fuzzer(prop_scuttlebutt_digest)
crucible_property_fuzzer(prop_scuttlebutt_compare)
crucible_property_fuzzer(prop_crdt_rga)

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
