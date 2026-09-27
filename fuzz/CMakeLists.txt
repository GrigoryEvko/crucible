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
#   2. boundary/   — one harness for each boundary where bytes from
#                    outside become typed values, behind the
#                    LLVMFuzzerTestOneInput signature.  The ctest
#                    smoke run replays generated seeds and mutated
#                    copies.  AFL++ with its GCC plugin drives the
#                    same binary for the coverage-guided run.
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
  # An optional second argument sets the smoke iteration count.
  set(iterations 1000)
  if(ARGC GREATER 1)
    set(iterations ${ARGV1})
  endif()
  add_executable(${name} property/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/property)
  target_compile_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_COMPILE})
  target_link_options(${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_LINK})

  # Smoke iterations — fast, suitable for ctest.  Developers running
  # extended stress can pass --iters=N on the command line.
  add_test(NAME ${name} COMMAND ${name} --iters=${iterations})
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
crucible_property_fuzzer(prop_crdt_rga_merge)
crucible_property_fuzzer(prop_ptp_degradation)

# ── Boundary fuzzers (adversarial input) ───────────────────────
#
# One harness per boundary, in boundary/harnesses/<name>.h, with run_<name>
# and seeds_<name>.  The seeds come from the real writers and include every
# minimized crash.  Two harness headers can include headers that do not
# compile together, so each harness compiles alone, once, into the object
# library fuzz_harness_<name>.  Two kinds of binary link it:
#
#   fuzz_<name>              the fuzz binary.  Its smoke test replays the
#                            seeds, then a fixed stream of mutated copies.
#                            Built with afl-g++-fast, it runs under afl-fuzz
#                            with no arguments: see fuzz/run-afl.sh.
#   prop_boundary_harnesses  the property test that runs every harness on
#                            Philox-edited seeds.

set(CRUCIBLE_FUZZ_HARNESS_NAMES "")
set(CRUCIBLE_FUZZ_HARNESS_LIST "")

function(crucible_boundary_fuzzer name mutations)
  add_library(fuzz_harness_${name} OBJECT boundary/harness_entry.cpp)
  target_link_libraries(fuzz_harness_${name} PRIVATE crucible)
  target_include_directories(fuzz_harness_${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/boundary)
  target_compile_definitions(fuzz_harness_${name} PRIVATE
    "CRUCIBLE_FUZZ_HARNESS_NAME=${name}"
    "CRUCIBLE_FUZZ_HARNESS_HEADER=\"harnesses/${name}.h\"")
  target_compile_options(fuzz_harness_${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_COMPILE})

  add_executable(fuzz_${name} boundary/fuzz_main.cpp)
  target_link_libraries(fuzz_${name} PRIVATE fuzz_harness_${name} crucible)
  target_compile_definitions(fuzz_${name} PRIVATE "CRUCIBLE_FUZZ_HARNESS_NAME=${name}")
  target_compile_options(fuzz_${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_COMPILE})
  target_link_options(fuzz_${name} PRIVATE ${CRUCIBLE_FUZZ_SAN_LINK})

  add_test(NAME fuzz_${name} COMMAND fuzz_${name} --mutate=${mutations})
  set_tests_properties(fuzz_${name} PROPERTIES LABELS "fuzz_boundary;fuzz_smoke")

  set(CRUCIBLE_FUZZ_HARNESS_NAMES ${CRUCIBLE_FUZZ_HARNESS_NAMES} ${name} PARENT_SCOPE)
  set(CRUCIBLE_FUZZ_HARNESS_LIST "${CRUCIBLE_FUZZ_HARNESS_LIST}CRUCIBLE_FUZZ_HARNESS(${name})\n" PARENT_SCOPE)
endfunction()

crucible_boundary_fuzzer(region 2000)
crucible_boundary_fuzzer(branch 2000)
crucible_boundary_fuzzer(federation 4000)
crucible_boundary_fuzzer(cipher_store 300)
crucible_boundary_fuzzer(ledger 2000)
crucible_boundary_fuzzer(trace_file 500)
crucible_boundary_fuzzer(session_log 4000)
crucible_boundary_fuzzer(fec 2000)
crucible_boundary_fuzzer(fountain 2000)
crucible_boundary_fuzzer(discovery_text 4000)
crucible_boundary_fuzzer(telemetry_text 4000)
crucible_boundary_fuzzer(cntp_text 4000)
crucible_boundary_fuzzer(cpu_list 4000)
crucible_boundary_fuzzer(source_position 4000)
crucible_boundary_fuzzer(recipe 4000)

file(GENERATE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/generated/boundary_harness_list.h
  CONTENT "// Generated by fuzz/CMakeLists.txt.  One line per boundary harness.\n${CRUCIBLE_FUZZ_HARNESS_LIST}")

crucible_property_fuzzer(prop_boundary_harnesses 1000)
target_include_directories(prop_boundary_harnesses PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
foreach(name IN LISTS CRUCIBLE_FUZZ_HARNESS_NAMES)
  target_link_libraries(prop_boundary_harnesses PRIVATE fuzz_harness_${name})
endforeach()
set_tests_properties(prop_boundary_harnesses PROPERTIES LABELS "fuzz_property;fuzz_smoke")
