# ═══════════════════════════════════════════════════════════════
# Test targets — inherit 200+ warning/hardening flags from the
# crucible INTERFACE library, plus sanitizers below.
# ═══════════════════════════════════════════════════════════════

option(CRUCIBLE_SANITIZE "Enable sanitizers in Debug test builds" ON)
option(CRUCIBLE_TSAN "Enable ThreadSanitizer (AxRaceFree, mutually exclusive with CRUCIBLE_SANITIZE)" OFF)

if(CRUCIBLE_TSAN AND CRUCIBLE_SANITIZE)
  message(FATAL_ERROR "CRUCIBLE_TSAN and CRUCIBLE_SANITIZE are mutually exclusive (TSan cannot coexist with ASan/UBSan)")
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 8: Sanitizers — Debug test builds only
#
# AxMemSafe:  ASan detects buffer overflow, use-after-free,
#             double-free, stack-use-after-scope, heap-use-after-free
# AxLeakFree: LeakSan (integrated into ASan on Linux) detects
#             memory leaks at process exit
# AxInitSafe: ASan's use-after-scope catches scoped variable misuse
#
# -fsanitize=address (ASan + LeakSan):
#   325. heap-buffer-overflow    OOB heap access
#   326. stack-buffer-overflow   OOB stack access
#   327. global-buffer-overflow  OOB global access
#   328. use-after-free          heap use after free
#   329. use-after-scope         scoped var use after scope exit
#   330. double-free             free same pointer twice
#   331. alloc-dealloc-mismatch  new[] with delete (not delete[])
#   332. stack-use-after-return  return addr of local
#   333. memory-leaks            LeakSan at exit (Linux only)
#
# -fsanitize=undefined (15 sub-checks):
#   225. alignment               misaligned pointer deref
#   226. bool                    bool with invalid value
#   227. builtin                 __builtin with bad args
#   228. enum                    enum with invalid value
#   229. float-cast-overflow     float→int overflow
#   230. float-divide-by-zero    float /0
#   231. integer-divide-by-zero  int /0
#   232. nonnull-attribute       null to [[nonnull]]
#   233. null                    null pointer deref
#   234. pointer-overflow        ptr arithmetic overflow
#   235. return                  missing return in non-void
#   236. shift                   shift ≥ bitwidth
#   237. signed-integer-overflow signed overflow
#   238. unreachable             __builtin_unreachable hit
#   239. vla-bound               VLA with bad size
#
# -fsanitize=integer (5 sub-checks):
#   240. unsigned-integer-overflow     unsigned wrap
#   241. implicit-unsigned-integer-truncation
#   242. implicit-signed-integer-truncation
#   243. implicit-integer-sign-change  sign flip
#   244. unsigned-shift-base           unsigned shift oflow
#
# -fsanitize=nullability (3 sub-checks):
#   245. nullability-arg         null to _Nonnull param
#   246. nullability-assign      null to _Nonnull var
#   247. nullability-return      null from _Nonnull func
#
# -fsanitize=implicit-conversion (4 sub-checks):
#   248. implicit-integer-truncation (combined)
#   249. implicit-integer-sign-change (combined)
#   250. implicit-integer-arithmetic-value-change
#   251. implicit-integer-bitfield-truncation
#
# Additional:
#   252. float-divide-by-zero (explicit standalone)
#   253. local-bounds           array bounds check
#
# ═══════════════════════════════════════════════════════════════
# SECTION 9: GCC sanitizers — Debug test builds only
#
# -fsanitize=undefined (GCC sub-checks):
#   254. alignment
#   255. bool
#   256. enum
#   257. float-cast-overflow
#   258. float-divide-by-zero
#   259. integer-divide-by-zero
#   260. nonnull-attribute
#   261. null
#   262. pointer-overflow
#   263. return
#   264. returns-nonnull-attribute
#   265. shift
#   266. signed-integer-overflow
#   267. unreachable
#   268. vla-bound
#   269. object-size
#
# Additional GCC-only:
#   270. bounds-strict          strict array bounds
#   271. pointer-compare        pointer comparison UB
#   272. pointer-subtract       pointer subtract UB
#
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CRUCIBLE_SANITIZE)
  set(CRUCIBLE_SAN_COMPILE_FLAGS
    # AxMemSafe: ASan — buffer overflow, use-after-free, double-free
    # AxLeakFree: LeakSan — integrated into ASan on Linux
    -fsanitize=address
    # UBSan — existing checks
    -fsanitize=undefined,bounds-strict
    -fno-sanitize-recover=all         # abort on first violation
    -fno-omit-frame-pointer           # readable stack traces
  )
  set(CRUCIBLE_SAN_LINK_FLAGS
    -fsanitize=address,undefined,bounds-strict
  )
endif()

# ═══════════════════════════════════════════════════════════════
# SECTION 8.5: ThreadSanitizer — AxRaceFree (opt-in)
#
# Detects data races at runtime. Mutually exclusive with ASan/UBSan.
#   cmake --preset tsan && cmake --build --preset tsan && ctest --preset tsan
# ═══════════════════════════════════════════════════════════════
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CRUCIBLE_TSAN)
  set(CRUCIBLE_SAN_COMPILE_FLAGS
    -fsanitize=thread
    -fno-omit-frame-pointer
  )
  set(CRUCIBLE_SAN_LINK_FLAGS
    -fsanitize=thread
  )
endif()

# ── Test target factory ──────────────────────────────────────
function(crucible_test name)
  if(ARGN)
    set(CRUCIBLE_TEST_SOURCES ${ARGN})
  else()
    set(CRUCIBLE_TEST_SOURCES ${name}.cpp)
  endif()

  add_executable(${name} ${CRUCIBLE_TEST_SOURCES})
  target_link_libraries(${name} PRIVATE crucible)
  # Test binaries must keep their assertions in Release too.  CMake's
  # Release defaults inject -DNDEBUG, and some standard headers may
  # redefine assert() after test_assert.h in a TU.  Force the test TU
  # itself back into assertion-enabled mode while linked libraries
  # still use the selected build type.
  target_compile_options(${name} PRIVATE -UNDEBUG)

  # Sanitizers — test targets only (not the shared library).
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()

  add_test(NAME ${name} COMMAND ${name})

  # Wire in TSan suppressions for known UB-adjacency patterns
  # (seqlock byte-race, etc.).  See test/tsan-suppressions.txt for
  # the curated list and rationale.
  if(CRUCIBLE_TSAN)
    set_tests_properties(${name} PROPERTIES
      ENVIRONMENT
        "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan-suppressions.txt:halt_on_error=1")
  endif()

  # Asm dump (gitignored, opt-in via -DCRUCIBLE_DUMP_ASM=ON).
  crucible_dump_asm(${name})
endfunction()

crucible_test(test_safety)
crucible_test(test_pre_post)
crucible_test(test_decide)

# ── FIXY-CLEAN Phase A — engagement-gate test corpus ────────────────
#
# Clean reimplementation per misc/16_05_2026_fixy.md §4 Phase A.  Three
# sentinel TUs cover the positive surface (engagement accepts), the
# 12 neg-compile fixtures cover the per-axis rejection surface, and the
# cheat-probe round covers IsAccepted trait-spec-injection resistance.
#
# Substrate composition (per §2.1):
#   - fixy/Dim.h        aliases safety::DimensionAxis (the 20-axis enum)
#   - fixy/Default.h    aliases safety::fn::Fn<int> defaults per axis
#   - fixy/Grant.h      uses safety::NotInherited final-class discipline
#   - fixy/Reject.h     delegates to safety/CollisionCatalog.h's
#                         ValidComposition (Phase B activation) and
#                         registers per-axis diag tags via safety/
#                         Diagnostic.h's tag_base.

crucible_test(test_fixy_engaged)
crucible_test(test_fixy_cheat_probe)
crucible_test(test_fixy_cheat_probe_theory)
crucible_test(test_fixy_l11_short_circuit)  # fixy-L-11 OR-fold short-circuit + chain-order coherence positive proofs
crucible_test(test_fixy_fn)
crucible_test(test_fixy_profile)
crucible_test(test_fixy_rule_helpers)
crucible_test(test_fixy_rules_compose)  # FIXY-U-062 — §6.8 aggregator surface
crucible_test(test_fixy_modality)       # FIXY-U-060 — top-level modality surface
crucible_test(test_fixy_sess_federation) # FIXY-U-072 — federation per-role reach
crucible_test(test_fixy_cap)
crucible_test(test_fixy_perm)
crucible_test(test_fixy_handle)  # FIXY-U-016 — per-namespace test_fixy_handle sentinel TU
crucible_test(test_fixy_sess)
crucible_test(test_fixy_sess_phi)
crucible_test(test_fixy_sess_patterns)  # FIXY-AUDIT-C6
crucible_test(test_fixy_sess_mint_removed_diag)  # FIXY-AUDIT-B6
crucible_test(test_fixy_contract_cipher)  # FIXY-AUDIT-B9
crucible_test(test_fixy_bridge_fwddecl)  # FIXY-AUDIT-C5
crucible_test(test_fixy_substr_completeness)  # FIXY-AUDIT-C10
crucible_test(test_fixy_pipe)
crucible_test(test_fixy_pipe_chain)  # FIXY-AUDIT-C2
crucible_test(test_fixy_bridge)
crucible_test(test_fixy_warden)  # FIXY-U-120 — fixy::warden:: re-export reach
crucible_test(test_fixy_perf)    # FIXY-U-121 — fixy::perf:: re-export reach
crucible_test(test_fixy_perf_v2) # FIXY-U-122 — fixy::perf::v2:: sub-umbrella reach
crucible_test(test_fixy_substr)
crucible_test(test_fixy_mach_safety)
crucible_test(test_fixy_mach_transitions)  # FIXY-AUDIT-C1
crucible_test(test_fixy_wrap)
crucible_test(test_fixy_struct)
crucible_test(test_fixy_is)
crucible_test(test_fixy_umbrella)
crucible_test(test_fixy_umbrella_reach)  # fixy-A4-001 + fixy-A4-002

# ── FIXY-ALGEBRA / EFF / DIAG / SOURCE — Phase D surfaces ─────────
#
# Algebra: Graded<M, L, T> substrate + 30 lattices + GradedWrapper
#   concept + Modality enum + law verifiers.
# Eff: full Met(X) surface — Row<>, Computation<>, Subrow,
#   row_union_t/difference_t/intersection_t, F* aliases (Pure/Tot/
#   Ghost/Div/ST/All), Capability<>, ExecCtx + 5 canonical ctxs,
#   21+ Resource budget tags, ConcurrentRow, OsUniverse, EffectMask.
# Diag: Category + Catalog + tag_of_t / category_of_v bijection +
#   28 tag classes + stable_name / type_id / function_id +
#   canonicalize_pack + insight_provider + row_hash_contribution.
# Source: source::* / trust::* / access::* / version::* /
#   vessel_trust::* / secret_policy::* / hash_family::* tag
#   namespaces — pure phantom-type aliases.
crucible_test(test_fixy_algebra)
crucible_test(test_fixy_eff)
crucible_test(test_fixy_eff_alias)  # FIXY-AUDIT-B12 HS14 sentinel
crucible_test(test_fixy_diag)
crucible_test(test_fixy_diag_catalog)  # FIXY-AUDIT-C8 reconciliation
crucible_test(test_fixy_insights)      # fixy-H-14 insight_provider sentinel
crucible_test(test_fixy_a3_007_insights)  # fixy-A3-007 substrate-tag insight closure
crucible_test(test_fixy_a3_008_synchronization)  # fixy-A3-008 Wait + MemOrder → Synchronization
crucible_test(test_fixy_a3_009_regime)  # fixy-A3-009 HotPath → Regime (Progress stays on Complexity)
crucible_test(test_fixy_source)
crucible_test(test_fixy_source_federation)  # FIXY-AUDIT-C9

# FIXY-LAT-Security + FIXY-LAT-Trust — every SecLevel and every
# safety::trust::* tag is reachable through one named grant tag.
# Positive-compile witnesses live in test_fixy_lattice_points; the
# matching HS14 neg-compile fixtures land below.
crucible_test(test_fixy_lattice_points)

# ── CONTRACT-090 + CONTRACT-091 — property-based fuzz harness ────
#
# For every fuzzable Decide procedure, the harness runs the production
# fast path against a deliberately SLOW reference oracle (different
# algorithm) on Philox4x32-10–derived random inputs.  10K iterations
# × 11 procedures = 110K equality checks; mismatch fails the test
# with a reproducible (counter, key) tuple.  Oracles live in
# include/crucible/safety/DecideOracle.h.
crucible_test(test_decide_fuzz)
crucible_test(test_hdr_histogram)
crucible_test(test_synthetic_probe)

crucible_test(test_simd)

# ── CONTRACT-008: NDEBUG zero-cost sentinel for CRUCIBLE_PRE ─────
#
# Special test target: built with `-O3 -DNDEBUG` instead of the
# preset's `-Og -g`.  The factory `crucible_test()` injects
# `-UNDEBUG` so every other test keeps assertions live; this test
# specifically requires NDEBUG to be DEFINED so it can verify the
# zero-cost claim of CRUCIBLE_PRE under production flags.
#
# Implementation: two probe functions with identical bodies — one
# routed through CRUCIBLE_PRE, the other through bare `[[assume]]`
# — must compile to byte-identical machine code.  The test
# compares ELF section sizes via `__start_/__stop_` linker
# symbols.  See test/test_pre_post_cost.cpp for the full design.
add_executable(test_pre_post_cost test_pre_post_cost.cpp)
target_link_libraries(test_pre_post_cost PRIVATE crucible)
target_compile_options(test_pre_post_cost PRIVATE
    # Override the preset's debug flags.  We want production codegen
    # so the cost-model claim is measured under the same conditions
    # users will run release binaries.
    -O3
    # Each function in its own section so __start_/__stop_ symbols
    # land at the right boundaries.  Also lets `-fgc-sections` work
    # cleanly, though we use volatile-sink references to keep the
    # probe functions live regardless.
    -ffunction-sections
)
target_compile_definitions(test_pre_post_cost PRIVATE NDEBUG)
add_test(NAME test_pre_post_cost COMMAND test_pre_post_cost)
crucible_dump_asm(test_pre_post_cost)

crucible_test(test_types)
crucible_test(test_kernel_cache_key)
crucible_test(test_row_hash_fold)
crucible_test(test_row_hash_contribution_sentinel)
crucible_test(test_row_hash_distinctness)
crucible_test(test_fixy_mint_inventory)
crucible_test(test_fixy_decide)
crucible_test(test_fixy_sess_mint_handle_reach)  # FIXY-U-022
crucible_test(test_cost_model)
crucible_test(test_saturate)
crucible_test(test_checked)
crucible_test(test_call_site_table)
crucible_test(test_swiss_table)
crucible_test(test_meta_log)
crucible_test(test_symbol_table)
crucible_test(test_hlc)
crucible_test(test_vector_clock)
crucible_test(test_crdt)
crucible_test(test_swim)
crucible_test(test_hyparview)
crucible_test(test_lifeguard)
crucible_test(test_plumtree)
crucible_test(test_scuttlebutt)
crucible_test(test_cntp_backpressure)
crucible_test(test_cntp_af_xdp)
crucible_test(test_cntp_connection_pool)
crucible_test(test_cntp_congestion_control)
crucible_test(test_cntp_incast_control)
crucible_test(test_cntp_ktls_offload)
crucible_test(test_cntp_mtls_transport)
crucible_test(test_cntp_path_swap)
crucible_test(test_cntp_pacing)
crucible_test(test_cntp_quic_transport)
crucible_test(test_cntp_roce_config)
crucible_test(test_cntp_wireguard)
crucible_test(test_cntp_integrity)
crucible_test(test_cntp_fec)
crucible_test(test_cntp_fountain)
crucible_test(test_cntp_overlay_multicast)
crucible_test(test_cntp_gossip_multicast)
crucible_test(test_trace_graph)
crucible_test(test_trace_loader)
crucible_test(test_schema_table)
crucible_test(test_effects)
crucible_test(test_iteration_detector)
crucible_test(test_arena)
crucible_test(test_trace_ring)
crucible_test(test_merkle_dag)
crucible_test(test_merkle_dag_visualizer)
set_target_properties(test_merkle_dag_visualizer PROPERTIES CRUCIBLE_NO_WERROR TRUE)
crucible_test(test_memory_plan)
crucible_test(test_serialize)
crucible_test(test_cipher)
crucible_test(test_transaction)
crucible_test(test_vigil)
crucible_test(test_ckernel)
crucible_test(test_graph)
crucible_test(test_lower)
crucible_test(test_reflect)
crucible_test(test_pool_allocator)
crucible_test(test_replay_engine)
crucible_test(test_crucible_context)
crucible_test(test_vigil_dispatch)
crucible_test(test_mlp_trace)
crucible_test(test_resnet)
crucible_test(test_vit)
crucible_test(test_compute)
crucible_test(test_compute_vit)
crucible_test(test_end_to_end)
crucible_test(test_philox)
crucible_test(test_philox_simd)
crucible_test(test_philox_det)
crucible_test(test_arena_alloc_class)
crucible_test(test_pool_allocator_alloc_class)
crucible_test(test_one_shot_flag_crash)
crucible_test(test_atomic_snapshot_wait)
crucible_test(test_cipher_wait)
crucible_test(test_cipher_publish_tier)
crucible_test(test_cipher_tier_promotion)
crucible_test(test_kernel_cache_residency_heat)
crucible_test(test_trace_ring_hot_path)
crucible_test(test_cipher_commit_lifetime)
crucible_test(test_atomic_snapshot_mem_order)
crucible_test(test_atomic_snapshot)
crucible_test(test_chase_lev_deque)
crucible_test(test_mpsc_ring)
crucible_test(test_algebra_compile)
crucible_test(test_safety_compile)
crucible_test(test_migration_verification)
crucible_test(test_concept_cheat_probe)
# fixy-A3-021: sentinel TU for the 8 algebra/* + effects/* headers that
# shipped header-only static_assert coverage of inline bodies.  Each
# header now exports inline runtime_smoke_test(); this TU calls them
# from main() under project warning flags, closing the header-only
# static_assert blind spot.  See
# feedback_header_only_static_assert_blind_spot.md +
# feedback_algebra_runtime_smoke_test_discipline.md.
crucible_test(test_smoke_algebra_effects)
add_test(NAME trait_guard
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-trait-injection.sh)
# fixy-A2-014: regression guard for the SessionPersistence.h include-lift.
# Probes `g++ -H` to verify the bridge header does not transitively pull
# Cipher.h / MerkleDag.h / Arena.h / FederationProtocol.h / etc.  Without
# this guard, a future edit re-adding the heavy include would silently
# reverse the build-time-cost reduction.
add_test(NAME session_persistence_include_audit
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-session-persistence-include-audit.sh)
set_tests_properties(session_persistence_include_audit PROPERTIES
  ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}")
# CONTRACT-131: contracts-infra adoption regression guard.  --check
# diffs the live tree against a committed baseline snapshot; counter
# increases are silent (adoption growth is good), counter decreases
# fail CI (regression).  Run `scripts/audit-pre-callsite-count.sh
# --baseline scripts/contracts-baseline.json` to refresh after a
# deliberate removal — the regression is then explained and the
# baseline owns the new counts.
add_test(NAME contract_cite_regression
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/audit-pre-callsite-count.sh
          --check ${CMAKE_SOURCE_DIR}/scripts/contracts-baseline.json)
# CONTRACT-125: per-procedure cite-ratio policy enforcer.  For every
# decide::* procedure in the catalog, asks "≥ 2 production cites
# within 180 days of introduction?".  Recently-introduced procedures
# in the GRACE bucket are informational; only procedures past the
# grace window with < 2 cites count as VIOLATIONs and fail CI.
# The audit runs the sibling --json producer and joins per-procedure
# cite counts with `git log -S` introduction dates.
# Catalog grows bottom-up (citing site first, name second per
# feedback_decide_catalog.md); this test enforces the bottom-up
# discipline doesn't decay over time.  CONTRACT-126 trims procedures
# this audit flags.
add_test(NAME contract_decide_cite_ratio
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/audit-decide-cite-ratio.sh)
# CONTRACT-120: Layer-C subsumption guard.  Catches double-enforcement
# of a Refined<Pred, T> parameter via in-body CRUCIBLE_PRE that re-tests
# the same predicate on the same parameter — the type-level proof
# already discharges the predicate at construction.  See harness §10.1.
# Self-test mode is wired separately so a regex regression (e.g. a
# ripgrep upgrade altering backreference semantics) fails CI on its own
# diagnostic rather than silently letting the production scan pass
# because it never matches anything.
add_test(NAME refined_pre_subsumption_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-refined-pre-subsumption.sh
          --self-test)
add_test(NAME refined_pre_subsumption
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-refined-pre-subsumption.sh)
# FIXY-U-001 Phase F discipline gate (misc/16_05_2026_fixy.md §4
# Phase F).  scripts/check-fixy-discipline.sh enforces the
# greenfield rule: directories opted into CRUCIBLE_FIXY_ONLY must
# spell fixy::fn<Type, Grants...> at consumer call sites rather
# than reaching past the umbrella to safety::fn::Fn<...> directly.
# Mirrors the discipline of check-trait-injection.sh +
# check-refined-pre-subsumption.sh — self-test mode is wired
# separately so a regex regression (rg upgrade, anchor drift)
# fails CI on its own diagnostic rather than silently letting the
# production scan pass.  Initial opt-ins: examples/fn/ +
# test/fixy_neg/.
add_test(NAME fixy_discipline_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-fixy-discipline.sh
          --self-test)
add_test(NAME fixy_discipline
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-fixy-discipline.sh)
# FIXY-U-003 §XXI Universal Mint Pattern compliance scanner.  Today's
# Phase 1 check: every mint_<noun>( factory definition MUST carry the
# [[nodiscard]] attribute on the same line or the immediately
# preceding line.  Phase 2 (deferred) will extend to constexpr /
# noexcept / requires-clause checks.  Self-test mode plants both a
# canonical mint and a drift case to guard against regex regression.
add_test(NAME mint_pattern_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-mint-pattern.sh
          --self-test)
add_test(NAME mint_pattern
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-mint-pattern.sh)
# fixy-L-03 #1519 — Permission<Tag> long-lived-struct antipattern
# guard (Permission.h:144-148 doc-block, CLAUDE.md §IX).  Every
# Permission<*> declared as a class/struct member MUST be on a line
# containing [[no_unique_address]] — the canonical handle-storage
# marker (see PermissionedSpscChannel / PermissionedMpscChannel /
# SwmrSession / KernelCacheSlot for the production shape).  Without
# the marker, holders may be aliased across threads silently,
# defeating CSL linearity (Permission move-only at value-level but
# the type system can't see pointer-based escape through struct
# sharing).  Initial state: clean — all 15 current sites use the
# discipline; allowlist is empty.  Self-test mode plants a synthetic
# violation to guard against regex regression.
add_test(NAME permission_storage_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-permission-storage.sh
          --self-test)
add_test(NAME permission_storage
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-permission-storage.sh)
# FIXY-U-097 std::vector::reserve ban enforcement (CLAUDE.md §IV
# opt-out matrix).  scripts/check-no-reserve.sh flags every NEW
# `.reserve(` / `->reserve(` call site outside the allowlist; 12
# grandfathered sites are tracked in scripts/no-reserve-allowlist.txt
# with their migration tickets (fixy-A5-020, WRAP-Cipher-*,
# WRAP-BgThread-6, etc.).  Self-test mode plants a violation,
# allowlist entry, inline-suppressed call, and commented-out call
# to verify all four exemption axes work.
add_test(NAME no_reserve_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-reserve.sh
          --self-test)
add_test(NAME no_reserve
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-reserve.sh)
set_tests_properties(no_reserve no_reserve_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-097")
# FIXY-U-105 reinterpret_cast ban enforcement (CLAUDE.md §III opt-out
# matrix).  scripts/check-no-reinterpret-cast.sh flags every NEW
# `reinterpret_cast<` call site outside the allowlist; 20 grandfathered
# sites are tracked in scripts/no-reinterpret-allowlist.txt with their
# migration tickets (FIXY-U-082, fixy-A5-028).  Self-test mode plants
# five axes: drift, allowlist exempt, inline marker exempt, comment
# filter exempt, and stale allowlist entry detection (stale entries
# red CI at exit 2 so the allowlist drains in lockstep with substrate
# fixes — every FIXY-U-082 site removal forces a same-PR allowlist
# trim, preventing dead entries from masking future drift).
add_test(NAME no_reinterpret_cast_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-reinterpret-cast.sh
          --self-test)
add_test(NAME no_reinterpret_cast
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-reinterpret-cast.sh)
set_tests_properties(no_reinterpret_cast no_reinterpret_cast_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-105")
# FIXY-U-096z migration lock-in.  The FIXY-U-096* sweep brought 14
# top-level include/crucible/*.h headers to literal-zero `safety::`
# (reaching the substrate only through the fixy:: umbrella).
# scripts/check-fixy-clean-headers.sh asserts every header in the
# positive registry scripts/fixy-clean-headers.txt stays safety::-free
# in code AND comments — a regression (a re-added safety::Tagged, or
# doc prose that writes safety::) reds CI at exit 1.  Self-test plants
# one dirty + one clean registered header to prove the pattern catches
# drift and spares umbrella spellings.  Positive registry, not an
# exemption allowlist: the list only grows as more headers migrate.
add_test(NAME fixy_clean_headers_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-fixy-clean-headers.sh
          --self-test)
add_test(NAME fixy_clean_headers
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-fixy-clean-headers.sh)
set_tests_properties(fixy_clean_headers fixy_clean_headers_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-096z")
# FIXY-U-100 syscall capability discipline (closes fixy-A5-016
# regression-prevention surface).  scripts/check-syscall-capability.sh
# flags every NEW direct Linux syscall invocation outside the allowlist
# (sched_setattr, mmap, ioctl, socket family, prctl, madvise, ...).
# 47 grandfathered sites live in scripts/syscall-capability-allowlist.txt,
# each with explicit effects::* capability proof (Init/IO/Bg/Block);
# new sites must be added with cap proof, forcing review through the
# capability-discipline surface.  Self-test verifies drift detection,
# allowlist exemption, inline `// SYSCALL-CAP-OK` marker, and comment
# filter all work.
add_test(NAME syscall_capability_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-syscall-capability.sh
          --self-test)
add_test(NAME syscall_capability
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-syscall-capability.sh)
set_tests_properties(syscall_capability syscall_capability_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-100")
# FIXY-U-101 row-membership discipline (closes fixy-A5-039
# regression-prevention surface).  scripts/check-row-contains-discipline.sh
# flags inline `effects::row_contains_v<row_type_of_t<Ctx>, ...>` sites
# that should migrate to the named lifts (CtxOwnsCapability /
# CtxOwnsAnyOf / CtxOwnsAllOf, shipped in commit c0b53827).  43
# grandfathered sites live in scripts/row-contains-discipline-allowlist.txt,
# each annotated with fixy-U-101-MIGRATE: <target lift> — the allowlist
# IS the migration tracking surface.  When a site migrates to the
# named lift, its allowlist entry MUST be pruned in the same PR (drift-
# free invariant).
add_test(NAME row_contains_discipline_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-row-contains-discipline.sh
          --self-test)
add_test(NAME row_contains_discipline
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-row-contains-discipline.sh)
set_tests_properties(row_contains_discipline row_contains_discipline_self_test
    PROPERTIES LABELS "ci_guard;fixy-U-101")
# FIXY-U-104 fixy::is:: alias-parity enforcement (closes fixy-L-05 +
# fixy-L-06 going forward).  scripts/check-isx-parity.sh diffs the
# public `_v` traits + `_t` slot-extractor aliases in safety/Is*.h
# (at extract:: namespace level, excluding detail:: internals) against
# fixy/Is.h's `using ::crucible::safety::extract::*` re-exports.  Adding
# a new IsX.h header (or growing an existing one) WITHOUT adding the
# matching fixy::is:: re-export now fails CI.  The DETAIL_EXCLUSIONS
# array inside the script lists the one currently-known detail symbol
# (session_base_probe_t in extract::detail::); new detail symbols that
# match the public-alias regex shape get added there.
add_test(NAME isx_parity_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-isx-parity.sh
          --self-test)
add_test(NAME isx_parity
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-isx-parity.sh)
set_tests_properties(isx_parity isx_parity_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-104")
# FIXY-U-087 stub-vs-live deprecation discipline (fixy-A5-001 / A5-002 / A5-025
# / A5-042).  scripts/check-stub-discipline.sh pairs every honesty marker
# (`inline constexpr bool *_implemented = false` / `*_attached = false`) with a
# matching `[[deprecated("CRUCIBLE_STUB:")]]` attribute in the SAME header.
# Without the pair, a stub surface looks live and only fails at runtime
# (BackendUnavailable / Deferred sentinels); WITH the pair, every production
# call site emits a -Wdeprecated-declarations warning at compile time.
# Self-test plants an honesty-marker-without-deprecation header and verifies
# the scan catches it.
add_test(NAME stub_discipline_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-stub-discipline.sh
          --self-test)
add_test(NAME stub_discipline
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-stub-discipline.sh)
set_tests_properties(stub_discipline stub_discipline_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-087")
# FIXY-U-086 cross-thread atomic sibling-assert discipline (CLAUDE.md §IX
# + fixy-A5-029).  scripts/check-lock-free-asserts.sh flags any
# `std::atomic<T>` declaration in canopy/cntp/topology/warden that lacks
# a sibling `static_assert(std::atomic<T>::is_always_lock_free)` in the
# same header — guards against the libatomic-silently-locks-on-prod-ARM
# class.  Self-test mode plants four axes: covered (must not flag),
# missing (must flag), inline LOCK-FREE-OK suppression (must not flag),
# allowlist entry (must not flag).  Allowlist currently empty — the
# source-level sweep is complete, this guard exists to prevent future
# regression.
add_test(NAME lock_free_asserts_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-lock-free-asserts.sh
          --self-test)
add_test(NAME lock_free_asserts
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-lock-free-asserts.sh)
set_tests_properties(lock_free_asserts lock_free_asserts_self_test PROPERTIES
    LABELS "ci_guard;fixy-U-086")
# FIXY-U-106 wire: gen-mint-inventory.sh self-test + --check drift gate.
# Complementary to test_fixy_mint_inventory (the structural CI gate of
# U-002) — the script surfaces the WHOLE substrate-mint table with §XXI
# compliance flags and HS14 fixture counts.  --check fires CI red if
# scripts/gen-mint-inventory.sh has not been re-run after a mint
# add/rename/move, forcing same-PR resync of misc/mint-inventory.md.
add_test(NAME mint_inventory_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/gen-mint-inventory.sh
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME mint_inventory_check
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/gen-mint-inventory.sh
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(mint_inventory_self_test mint_inventory_check PROPERTIES
    LABELS "ci_guard;fixy-U-106")
crucible_test(test_permissions_compile)
crucible_test(test_handles_compile)
crucible_test(test_sessions_compile)
# fixy-A2-015 sentinel: <crucible/sessions/Sessions.h> umbrella must
# expose mint_permissioned_session, payload_row_t, and the
# MpmcChannelSession surface.  Pre-fix this TU fails to compile.
crucible_test(test_sessions_umbrella_surface)
crucible_test(test_bridges_compile)
crucible_test(test_effects_compile)
crucible_test(test_resources)
crucible_test(test_concurrent)
crucible_test(test_concurrent_spinlock)
crucible_test(test_calibrate)
crucible_test(test_cog_identity)
crucible_test(test_target_caps)
crucible_test(test_opcode_latency_table)
crucible_test(test_numa_nic)
crucible_test(test_nic_config)
crucible_test(test_nic_offload_audit)
crucible_test(test_sriov)
crucible_test(test_cntp_gpu_direct)
crucible_test(test_cntp_sharp)
crucible_test(test_cntp_doca)
crucible_test(test_cntp_p4)
crucible_test(test_cntp_tcam)
crucible_test(test_stub_discipline)
crucible_test(test_quarantine)
crucible_test(test_fits_cog)
crucible_test(test_cog_mimic)
crucible_test(test_topology_discovery)
crucible_test(test_topology_graph)
crucible_test(test_topology_health)
crucible_test(test_topology_pingmesh)
crucible_test(test_topology_ptp)
crucible_test(test_topology_telemetry)
crucible_test(test_congestion_telemetry)
crucible_test(test_asymmetric_failure)
crucible_test(test_concurrent_compile)
crucible_test(test_auto_split)
crucible_test(test_diagnostic_compile)
crucible_test(test_stable_name_compile)
crucible_test(test_signature_traits)
crucible_test(test_is_alloc_class)
crucible_test(test_is_bits)
crucible_test(test_is_borrowed)
crucible_test(test_is_borrowed_ref)
crucible_test(test_is_hot_path)
crucible_test(test_is_mem_order)
crucible_test(test_is_progress)
crucible_test(test_is_wait)
crucible_test(test_is_linear)
crucible_test(test_is_refined)
crucible_test(test_is_secret)
crucible_test(test_is_stale)
crucible_test(test_is_tagged)
crucible_test(test_is_owned_region)
crucible_test(test_is_permission)
crucible_test(test_is_reduce_into)
crucible_test(test_graded_extract)
crucible_test(test_inferred_permission_tags)
crucible_test(test_unary_transform)
crucible_test(test_binary_transform)
crucible_test(test_reduction)
crucible_test(test_producer_endpoint)
crucible_test(test_consumer_endpoint)
crucible_test(test_swmr_writer)
crucible_test(test_swmr_reader)
crucible_test(test_pipeline_stage)
crucible_test(test_canonical_shape)
crucible_test(test_is_numerical_tier)
crucible_test(test_is_consistency)
crucible_test(test_is_opaque_lifetime)
crucible_test(test_is_det_safe)
crucible_test(test_is_cipher_tier)
crucible_test(test_is_residency_heat)
crucible_test(test_is_vendor)
crucible_test(test_is_crash)
crucible_test(test_is_budgeted)
crucible_test(test_is_epoch_versioned)
crucible_test(test_is_numa_placement)
crucible_test(test_is_recipe_spec)
crucible_test(test_inferred_row)
crucible_test(test_cheat_probe_wrappers)
crucible_test(test_h10_algebra_diag_bridge)
crucible_test(test_fusion)
crucible_test(test_computation_cache)
crucible_test(test_computation_cache_integration)
crucible_test(test_computation_cache_invalidation)
crucible_test(test_federation_protocol)
crucible_test(test_cipher_record_event)
crucible_test(test_background_thread_run_in_row)
crucible_test(test_computation_cache_federation)
crucible_test(test_federation_permission)

# fixy-CR-02 positive-attack regression — proves the V1 self-signed
# federation handshake is forgeable today.  When HACL* lands and the
# forgery is rejected, this test reddens; that signals the
# [[deprecated]] tag on mint_federation_admittance + the pragma
# suppressions in 4 TUs must be removed.  See header doc-block in
# include/crucible/permissions/FederationPermission.h §fixy-CR-02.
crucible_test(attack_federation_forgery
              safety_attack/attack_federation_forgery.cpp)
set_tests_properties(attack_federation_forgery PROPERTIES
    LABELS "safety_attack;fixy-CR-02")

# fixy-CR-03 positive-attack regression — proves the V1 self-signed
# federation handshake is REPLAYABLE today.  Orthogonal to forgery:
# even a legitimate handshake admits indefinitely once observed
# because the substrate carries no seen-nonce / epoch state.  When
# replay protection lands (HACL*-backed verifier OR a seen-nonces
# Cipher store OR Cipher-epoch binding), this test reddens; that
# signals the fixy-CR-03 axis of the [[deprecated]] message on
# mint_federation_admittance can be dropped.  See header doc-block in
# include/crucible/permissions/FederationPermission.h §fixy-CR-03.
crucible_test(attack_federation_replay
              safety_attack/attack_federation_replay.cpp)
set_tests_properties(attack_federation_replay PROPERTIES
    LABELS "safety_attack;fixy-CR-03")

# fixy-CR-04 positive-attack regression — proves the V1
# mint_federation_admittance accepts ANY const-ref to a
# LocalCipherPermission as proof of local authority, even when
# the caller borrowed it from a singleton, shares it across N
# aliased call sites, or received it through trust-transitive
# ref-passing.  The substrate's `(void)local_permission` body
# discards the bytes; only the TYPE is checked.  When value-based
# local authority lands (HACL*-backed MAC with per-cipher secret
# material in Permission<LocalCipherTag>'s storage), this test
# reddens; that signals the fixy-CR-04 axis of the [[deprecated]]
# message on mint_federation_admittance can be dropped.  See
# header doc-block in
# include/crucible/permissions/FederationPermission.h §fixy-CR-04.
crucible_test(attack_federation_authority_bypass
              safety_attack/attack_federation_authority_bypass.cpp)
set_tests_properties(attack_federation_authority_bypass PROPERTIES
    LABELS "safety_attack;fixy-CR-04")

# fixy-CR-05 positive-attack regression — proves the V1 splits_into
# trait is user-extensible.  A malicious downstream TU specializes
# splits_into<FederatedPeer<OrgA>, FederatedPeer<OrgB>, FederatedPeer<OrgB>>
# as std::true_type; partial-specialization ranking gives the
# malicious explicit specialization higher rank than the
# FederationPermission.h defensive intra-org-only partial.
# mint_permission_split then converts one legitimate same-org
# Permission into two cross-org Permissions with no admittance
# check.  When fixy-M-29 lands (namespace-private or friend-
# constrained splits_into), the malicious specialization fails to
# compile and this TU itself reds.  See header doc-block in
# include/crucible/permissions/FederationPermission.h §fixy-CR-05.
crucible_test(attack_federation_cross_org_escalation
              safety_attack/attack_federation_cross_org_escalation.cpp)
set_tests_properties(attack_federation_cross_org_escalation PROPERTIES
    LABELS "safety_attack;fixy-CR-05")

# fixy-CR-09 positive-attack regression — proves the namespace
# `crucible::fixy::grant` is openable by foreign translation units
# because C++ has no namespace-scoped specialization access control.
# The IsGrantTag_v<T> gate (is_base_of_v<grant_base, T> && is_final_v<T>)
# is the load-bearing defense, NOT the namespace.  When fixy-M-29-
# equivalent lands (closed-world enum via reflection, namespace-
# private specialization access, or friend-only which_dim
# registration), this TU itself fails to compile.  See header doc-
# block in include/crucible/fixy/Grant.h "Defense-surface honesty
# (fixy-CR-09)" block.
crucible_test(attack_fixy_grant_namespace_reopen
              safety_attack/attack_fixy_grant_namespace_reopen.cpp)
set_tests_properties(attack_fixy_grant_namespace_reopen PROPERTIES
    LABELS "safety_attack;fixy-CR-09")

# fixy-CR-09 namespace-purity CI guard — enforces that ONLY
# include/crucible/fixy/Grant.h opens namespace crucible::fixy::grant
# (plus excepted attack-regression and cheat-probe TUs carrying the
# explicit "// fixy-CR-09: known residual gap" acknowledgement
# comment).  Any other file reopening the namespace fails the build
# with a diagnostic naming the offending location.
add_test(NAME fixy_grant_namespace_purity
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-fixy-grant-namespace-purity.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(fixy_grant_namespace_purity PROPERTIES
    LABELS "safety_attack;ci_guard;fixy-CR-09")

# fixy-A1-018 splits_into orphan-specialization guard — enforces that
# `splits_into<Parent, L, R>` and `splits_into_pack<Parent, Children...>`
# specializations live ONLY in the blessed authoring locations
# (include/crucible/permissions/, include/crucible/concurrent/,
# safety/Permission{Tree,Grid}Generator.h, and test/**).  C++ has no
# native orphan-rule; this script fills the gap at build time so a
# foreign TU cannot forge cross-region authority by declaring an
# arbitrary split for a foreign tag (CLAUDE.md §IX, §XVI).
add_test(NAME splits_into_orphan_purity
         COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-splits-into-orphan.sh
         WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(splits_into_orphan_purity PROPERTIES
    LABELS "safety_attack;ci_guard;fixy-A1-018")

crucible_test(test_fx_aliases)
crucible_test(test_fx_alias_diagnostic)
crucible_test(test_effect_row_lattice)
crucible_test(test_os_universe)
crucible_test(test_computation_graded)
crucible_test(test_d25_d29_detector_headers)
crucible_test(test_is_producer_handle)
crucible_test(test_is_consumer_handle)
crucible_test(test_is_session_handle)
crucible_test(test_is_swmr_handle)
crucible_test(test_row_mismatch_compile)
crucible_test(test_insights_compile)
crucible_test(test_cheat_probe_compile)
crucible_test(test_diag_runtime)
crucible_test(test_diag_json_emitter)
crucible_test(test_vessel_api_typed)
crucible_test(test_graded_wrapper_gate)
crucible_test(test_storage_nbytes_simd)
crucible_test(test_trace_ring_pop_batch)
crucible_test(test_expr_pool_fast_symbol)
crucible_test(test_sharded_grid)
crucible_test(test_queue_facade)
crucible_test(test_permission_shared)
crucible_test(test_fair_permission_shared)
crucible_test(test_owned_region)
crucible_test(test_smoke_safety_wrappers)  # fixy-A1-016 — per-header runtime_smoke_test() sentinel
crucible_test(test_permission_tree_generator)
crucible_test(test_permission_grid_generator)
crucible_test(test_read_view)
crucible_test(test_topology)
crucible_test(test_parallelism_rule)
crucible_test(test_pipeline_dispatch)
crucible_test(test_pipeline_dag)
crucible_test(test_permissioned_snapshot)
crucible_test(test_auto_router)
crucible_test(test_permissioned_mpsc_channel)
crucible_test(test_permissioned_mpmc_channel)
crucible_test(test_permissioned_sharded_grid)
crucible_test(test_permissioned_chase_lev_deque)
crucible_test(test_chaselev_deque_session)
crucible_test(test_chaselev_substrate_session_bridge)
crucible_test(test_sharded_grid_substrate_session_bridge)
crucible_test(test_calendar_grid_substrate_session_bridge)
crucible_test(test_sharded_calendar_grid_substrate_session_bridge)
crucible_test(test_permissioned_cross_primitive)
crucible_test(test_permissioned_calendar_grid)
crucible_test(test_scheduler_policies)
crucible_test(test_concurrency_collision_fuzzer)
crucible_test(test_region_cache)
crucible_test(test_numerical_recipe)
crucible_test(test_recipe_pool)
crucible_test(test_recipe_registry)
crucible_test(test_recipe_integration)
crucible_test(test_recipe_coverage)
crucible_test(test_recipe_registry_tier)
crucible_test(test_recipe_registry_spec)
crucible_test(test_network_recipe)
crucible_test(test_ir001_comm)
crucible_test(test_forge_comm_phase)
crucible_test(test_mimic_network_backend)

# bench harness unit tests — include bench/ for bench_harness.h.
# Snapshot now lives at <crucible/perf/SenseHub.h> (post-promotion via
# GAPS-004a, 2026-05-03) and is reached through the standard `crucible`
# include path; the bench-local include below carries only
# bench_harness.h itself.  CRUCIBLE_HAVE_BPF is intentionally unset for
# this TU — the test exercises only the non-BPF statistical primitives
# plus Snapshot::operator- (a header-only inline using
# crucible::sat::sub_sat from Saturate.h, doesn't need libbpf at link
# time).  Use SYSTEM include to silence -Werror diagnostics from the
# headers we don't own (other agents' territory); our .cpp still
# honours -Werror.
crucible_test(test_bench_harness)
# Exercises the crucible::warden module — bounds-enforcement policy rails
# (SCHED_FIFO/RR priority clamp, mlock, CPU-pin helpers). Header-only,
# no special capabilities required at test time (the smoke checks the
# policy arithmetic / guard object semantics, not live scheduler calls).
crucible_test(test_warden_policy)
crucible_test(test_cntp_dataplane_tc_ebpf)
crucible_test(test_cntp_dataplane_xdp)

# crucible::perf::SenseHub sentinel TU (GAPS-004a, 2026-05-03).
# Verifies the promoted observability substrate compiles + links
# through the standard `crucible` PUBLIC include path and that the
# Idx fence-post ordinals haven't drifted.  When CRUCIBLE_HAVE_BPF=1
# the test also calls SenseHub::load() once to confirm the loader
# symbol is link-reachable (return value is don't-care; depends on
# the running kernel's caps, see test docstring).
crucible_test(test_perf_sense_hub_smoke)
crucible_test(test_perf_sense_hub_v2_layout)
if(CRUCIBLE_SENSE_HUB_V2 AND CRUCIBLE_SENSE_HUB_EXTENDED)
  target_compile_definitions(test_perf_sense_hub_v2_layout PRIVATE
    CRUCIBLE_EXPECT_SENSE_HUB_EXTENDED=1)
endif()
crucible_test(test_perf_sched_switch_smoke)
crucible_test(test_perf_pmu_sample_smoke)
crucible_test(test_perf_lock_contention_smoke)
crucible_test(test_perf_syscall_latency_smoke)
crucible_test(test_perf_senses_smoke)
crucible_test(test_perf_sched_tp_btf_smoke)
crucible_test(test_perf_syscall_tp_btf_smoke)
# GAPS-004g (#1283): warden::DeadlineWatchdog wires SchedSwitch context-
# switch telemetry into Policy's miss-budget enforcement.  Smoke
# verifies type-shape (move-only, ≤64 B), nullptr-Senses graceful
# degradation, demote_one_step ladder, and reset() semantics.
crucible_test(test_warden_deadline_watchdog_smoke)
# GAPS-004h (#1284): perf::WorkloadProfiler wires SenseHub counters
# into ParallelismRule's parallel-vs-sequential decision.  Smoke
# verifies type-shape (move-only), nullptr-Senses pass-through over
# bare ParallelismRule, L1-resident short-circuit (cache-tier gate
# fires without telemetry read), reset() semantics, and the never-
# promote invariant.
crucible_test(test_perf_workload_profiler_smoke)
# Vigil ↔ DeadlineWatchdog wiring (GAPS-004h follow-up): verifies the
# bg-thread-write / fg-thread-read atomic counter publication path
# does not block the main thread, and that disabled/enabled Config
# behave correctly across a region transition.
crucible_test(test_vigil_deadline_watchdog)
target_include_directories(test_bench_harness SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/bench)

# ── safety::ScopedView positive + negative tests ─────────────────────
#
# Positive: test_safety_view links normally, must compile + pass.
# Negative: each safety_neg/neg_*.cpp MUST FAIL TO COMPILE; the Tier 1
# (private ctor / deleted assign) or Tier 2 (reflection audit) mechanism
# fires a diagnostic.  CMake builds each via EXCLUDE_FROM_ALL, then
# wraps each in a ctest with WILL_FAIL=TRUE so that a successful build
# is reported as a test failure.
crucible_test(test_safety_view)

# Gay-Hole synchronous session subtyping (SEPLOG-H2a+subtype, task #338).
# Header-only L6 layer; the bulk of coverage is in-header static_asserts,
# this test adds a small runtime harness for ctest integration.
crucible_test(test_session_subtype)

# GAPS-069 protocol ProductLattice grade projection.
crucible_test(test_session_grade)

# Session delegation — Honda 1998 throw/catch higher-order sessions
# (SEPLOG-H2a+delegate, task #337).  Exercises Delegate<T, K> /
# Accept<T, K> runtime via an in-memory mock transport.
crucible_test(test_session_delegate)

# Session-type pattern library (SEPLOG-H2p, task #341) — RequestResponse,
# FanOut / FanIn / Broadcast / ScatterGather, PipelineSource / Stage /
# Sink, Transaction, TwoPhaseCommit, MpmcProducer / Consumer, SwimProbe,
# Handshake.  Most coverage is in-header static_asserts; this harness
# exercises four patterns end-to-end through an in-memory wire.
crucible_test(test_session_patterns)

# PermissionedSessionHandle integration (FOUND-C Phase 3, tasks
# #610-#615) — end-to-end exercises of the CSL × session integration:
# End round trip, plain Send, Transferable Send (PS shrinks),
# Recv Transferable + Send Returned (PS grows then shrinks), Loop
# balance enforcement (Decision D3), Select branch convergence
# (Decision D4 structural).  In-header smoke covers shape and sizeof;
# this harness exercises real Transports through a FakeChannel
# Resource so the runtime path is verified, not just the type level.
crucible_test(test_permissioned_session_handle)

# SpscSession.h (SEPLOG-INT-1 / SAFEINT-R31, #384/#413) — first
# production-shaped wiring of FOUND-C v1 atop concurrent/Permissioned-
# SpscChannel.  Three tiers: (A) file-scope sizeof witness pinned to
# this TU's build flags (catches ABI drift); (B) two-jthread round-
# trip exchanging 1024 items via the typed-session API; (C) immediate-
# detach validation for the canonical shutdown pattern.  Closes the
# Part IX.2 critique of "no production caller exists" — this test is
# the framework's first wired-in end-to-end exercise.
crucible_test(test_spsc_session)

# MetaLogSession.h (GAPS-061) — typed-session facade over the production
# MetaLog TensorMeta side-channel.  Covers permissioned producer/consumer
# endpoints, typed-session round-trip, and immediate detach.
crucible_test(test_metalog_session)

# ChainEdgeSession.h (GAPS-062) — one-shot semaphore signal/wait facade
# over PermissionedChainEdge.  Covers CPU oracle plus stubbed vendor
# surfaces and the Send<SemaphoreSignal, End> / Recv<...> sessions.
crucible_test(test_chainedge_session)

# fixy-A2-017 — §XXI constexpr sentinel TU exercising the 12 linear-
# permission-consume token mint factories across 8 session-substrate
# headers (chaselev/metalog/swmr/chainedge/calendar/sharded_grid/
# sharded_calendar).  Each mint is pinned noexcept via static_assert
# and called with a freshly-minted root Permission; the constexpr
# chain is structurally witnessed by the substrate factory chain.
# Pool-mediated factories (thief / swmr_reader) are excluded per §XXI
# "unless the factory genuinely allocates" — atomic refcount is the
# same runtime-cost class.
crucible_test(test_session_substrate_mint_constexpr)

# MpmcChannelSession.h (SEPLOG-H2 / #326) — typed-session facade over
# PermissionedMpmcChannel (Nikolaev SCQ).  Covers single-producer
# round-trip, multi-producer × multi-consumer concurrent stream
# (set-equality drain), immediate-detach, and endpoint forwarder
# helpers.  The fractional × fractional cell of the channel-permission
# family — last of the four cells to receive a session-typed facade.
crucible_test(test_mpmc_channel_session)

# SwmrSession.h (GAPS-019 / GAPS-020) — typed-session facade for
# AtomicSnapshot + SharedPermissionPool.  Covers writer publication,
# multi-reader lifetime accounting, late-reader visibility, matching
# SharedPermission proof minting, and PSH send/recv wrapper execution.
crucible_test(test_swmr_session)

# SnapshotSession.h (fixy-A4-022) — typed-session facade for
# PermissionedSnapshot (thin protocol layer over the substrate's
# writer/reader endpoints).  Loop<Send/Recv<T,Continue>> with
# EmptyPermSet; covers publish/load round-trip and immediate detach.
crucible_test(test_snapshot_session)
crucible_test(test_observe_metrics_swmr)
crucible_test(test_observe_health)
crucible_test(test_observe_sdc_detect)

# L2 typing context Γ (SEPLOG-I1, task #343) — Entry, Context,
# compose_context_t, lookup_context_t, contains_key_v, domain_of_t,
# update_entry_t, remove_entry_t.  Gateway to L5 (association),
# L7 (φ safety), L8 (crash-stop) and L9 (CSL × session).
crucible_test(test_session_context)

# L8 crash-stop extensions (SEPLOG-I5, task #347) — Stop combinator,
# Crash<PeerTag> payload marker, UnavailableQueue, ReliableSet,
# has_crash_branch_for_peer_v.  Stop as the bottom of the subtype
# order (BSYZ22); crash-branch dispatch via Offer<>; ReliableSet
# membership; Crash<Server> round-trip through an in-memory wire.
crucible_test(test_session_crash)

# L3 queue types σ (SEPLOG-I2, task #344) — QueuedMsg, Queue,
# enqueue_queue_t, head_queue_t, tail_queue_t, queue_contains_v,
# count_matching_v, is_bounded_queue_v, is_unavailable_queue_v.
# Foundation for L7 async reasoning (φ_precise_async_safe) and
# L6.5 SISO async subtyping.
crucible_test(test_session_queue)

# L4 global types + projection (SEPLOG-H2g, task #339) — End_G,
# Transmission, Choice, Rec_G, Var_G, StopG; RoleList + insert_unique
# + union_roles + RolesOf; is_global_well_formed_v; plain_merge_t;
# Project<G, Role> / project_t<G, Role>.  Gates L5 association
# (Δ ⊑_s G) and L7 multiparty φ-aggregate.
crucible_test(test_session_global)

# L5 association Δ ⊑_s G (SEPLOG-I3, task #345) — HYK24's corrected
# invariant connecting L2 Γ to L4 G via L6 subtyping.  Ships
# is_associated_v<Γ, G, SessionTag>, projected_context_t<G, S>,
# AssociatedWith concept, assert_associated helper, and the
# gated per-role refinement check.  Payoff: write G once, prove G's
# properties once, inherit the proofs for every associated Δ.
crucible_test(test_session_assoc)

# CheckpointedSession combinator (SEPLOG-L2, task #362,
# session_types.md Appendix D.2) — first-class structural combinator
# with base/rollback branches; dual/compose/well-formed/subtype
# specialisations + SessionHandle with .base() / .rollback() methods.
# Supports speculative decoding, Transaction (#101), FLR recovery.
crucible_test(test_session_checkpoint)

# L11 diagnostic vocabulary (SEPLOG-H2d, task #342) — 11 manifest-
# bug classification tag types (ProtocolViolation_*, Deadlock_Detected,
# SubtypeMismatch, CrashBranch_Missing, UnboundedQueue, etc.), each
# with ::name / ::description / ::remediation constexpr strings.
# Provides CRUCIBLE_SESSION_ASSERT_CLASSIFIED macro for greppable,
# classified build-log diagnostics.
crucible_test(test_session_diagnostic)

# Scheduler policy tags (SEPLOG-H3, task #329) — 7 compile-time
# policy tags (Fifo, Lifo, RoundRobin, LocalityAware★, Deadline,
# Cfs, Eevdf) parameterising the future NumaMpmcThreadPool.  Each
# tag carries constexpr metadata (name, description, use_case,
# typical_submit_ns, algorithmic properties).  LocalityAware is the
# default per THREADING.md §5.5.2.  Inheritance-based detection; user
# extensions plug in with zero boilerplate.
crucible_test(test_scheduler_policy)

# AdaptiveScheduler Pool<Policy> (GAPS-032) — runnable policy-parametric
# worker pool over scheduler/Policies.h, with dispatch(), topology
# consultation for locality-aware policies, completion smoke coverage
# across all shipped queue-template policies, and the Substrate
# recommendation bridge.
crucible_test(test_adaptive_scheduler)

# ContentAddressed<T> quotient combinator (SEPLOG-L1, task #361,
# session_types.md Appendix D.5) — payload wrapper marking a value
# type as eligible for wire-level dedup at the transport layer.
# Mutual is_subsort integration makes Send<ContentAddressed<T>, K>
# and Send<T, K> interchangeable at the session-type level.
# Supports Cipher publication, KernelCache broadcast, PagedKVCache
# prefix sharing, replay trace dedup.
crucible_test(test_session_content_addressed)

# Cipher federation MPST facade (GAPS-039) — projects the three-role
# Sender/Receiver/Coord global protocol into typed local PSH handles.
# Covers announce/ack and pull/payload loops plus role/protocol matching.
crucible_test(test_federation_session)

# SessionHandle lifetime mechanics (SEPLOG-I7, task #349) — the CRTP
# SessionHandleBase<Proto> that adds abandoned-protocol detection to
# every specialisation.  Debug destructor aborts when a non-terminal,
# non-consumed handle leaves scope; release is a 1-byte-field no-op.
# .detach() is the explicit escape hatch for infinite-Loop protocols
# or out-of-band transport close.
crucible_test(test_session_lifetime)

# Per-derived-class abandonment diagnostic (#429) — SessionHandleBase
# now takes an OPTIONAL `Derived` template argument naming the wrapper
# class.  C++26 P2996 reflection (std::meta::identifier_of(template_of(
# ^^Derived))) extracts the bare wrapper template name ("SessionHandle" /
# "CrashWatchedHandle" / "RecordingSessionHandle") so the destructor's
# abandonment diagnostic distinguishes which wrapper aborted, even when
# many wrappers share the same Proto.  The witness fired by #400's
# debugging episode.  This test verifies wrapper_name() at compile time
# for all three production wrapper classes across multiple Proto heads.
crucible_test(test_session_wrapper_name)

# PermissionedSpscChannel<T, N, UserTag> — SPSC worked example
# backing SEPLOG-INT-1 (task #384, TraceRing wiring) and the broader
# K-series production-integration work.  Validates: role
# discrimination via concept, move-only handles with reference-bound
# channel (move-assign deliberately deleted), splits_into auto-
# specialisation per UserTag, sizeof EBO promise, single-threaded
# FIFO + capacity bound + cross-thread jthread handoff.
crucible_test(test_permissioned_spsc_channel)

# Tagged-provenance + Refined payload subsort axioms (task #395,
# SAFEINT-S6 from misc/24_04_2026_safety_integration.md §6).  The
# new safety/SessionPayloadSubsort.h header bridges Tagged + Refined
# through the session-type subsumption lattice.  Test verifies the
# positive axioms (Sanitized / Validated / FromInternal / FromConfig
# / FromDb / Durable / Computed flow to bare T), the negative
# non-axioms (External / FromUser / FromPytorch / trust::* /
# access::* / version::* deliberately do NOT flow), Send covariance
# + Recv contravariance through the wrappers, and a runtime worked
# scenario for the Vessel-FFI flow (raw FromPytorch input → validate
# → Validated → internal pipeline accepts via subtype subsumption).
crucible_test(test_session_payload_subsort)

# Machine ↔ Session bridge — single-party typestate carriers (#399,
# SAFEINT-A10).  Verifies SessionFromMachine<State, Proto> exposes
# both Machine and SessionHandle views over the same underlying
# machine, is Pinned for address stability, and machine_from_session
# recovers the underlying pointer from both End-state and mid-
# protocol handles without firing the abandonment-check.
crucible_test(test_machine_session_bridge)

# Vigil mode bridge — production atomic-aware MachineSessionBridge
# wiring over Vigil::ModeCell.  Verifies the live RECORDING <->
# COMPILED mode cycle through a session handle without adding state to
# Vigil or reviving stale REPLAYING/SERVING persistent modes.
crucible_test(test_vigil_mode_bridge)

# Non-consuming protocol-position views (#405, SAFEINT-B16).
# Position-tagged ScopedView<SessionHandle, At*> borrows that let
# external code inspect a handle's protocol position without
# consuming it.  Verifies handle_is_at_v positives + negatives
# across all SessionHandle specialisations, view_ok ADL hook,
# mint_session_view requires-clause rejection, multiple-coexisting-
# views, and the typed-metrics callback pattern from §16.
crucible_test(test_session_view)

# Rich subtype-rejection diagnostics (#380, SEPLOG-STRUCT-6).
# subtype_rejection_reason_t<T, U> walks the protocol tree in
# lockstep with is_subtype_sync but returns either SubtypeOk or a
# tagged RejectionReason naming the FIRST failing inner pair.
# Verifies per-rule reason tagging, deeply-nested bubble-up,
# is_subtype_sync_diag_v / is_subtype_sync_v agreement, and the
# protocol-evolution classify pattern that drives structured error
# reports from the rejection tag.
crucible_test(test_session_subtype_reason)

# Lazy-established session channels (#403, SAFEINT-B14).
# LazyEstablishedChannel<Proto, Resource> wraps PublishOnce so
# session establishment is asynchronous and structurally correct:
# the publisher calls establish(Resource*) once, observers call
# observe() and either get a SessionHandle or std::nullopt before
# the publication is visible.  Verifies pre/post-establish behaviour,
# multi-observer fan-out, the Vessel-startup pattern, and worker-
# before-publisher fallback.  Resolves the init-flag-and-condvar
# pattern documented in §14.
crucible_test(test_lazy_established_channel)

# DeclassifyOnSend payload wrapper (#402, SAFEINT-B13).
# DeclassifyOnSend<T, Policy> puts the wire-classification policy
# IN THE PROTOCOL TYPE so audit-grep `DeclassifyOnSend<` finds every
# wire-classified payload.  Move-only wrapping over Secret<T>; the
# transport's declassify_for_wire() is the single chokepoint where
# the Secret bytes escape classification.  Verifies the load-bearing
# subsort asymmetry (DeclassifyOnSend ⩽ bare T is FALSE — defeating
# audit-discoverability), the trait extraction path, and the typed
# auth-handshake worked example from §13.
crucible_test(test_session_declassify)

# Dedicated TUs re-enabling every session-type self-test namespace
# (#372 SEPLOG-PERF-2).  Each *_self_test namespace across the session
# headers is gated on `CRUCIBLE_SESSION_SELF_TESTS`; these TUs define
# the macro before including focused header groups.  The split keeps
# the coverage identical while avoiding one giant serial cc1plus tail.
crucible_test(test_session_self_core)
crucible_test(test_session_self_crash)
crucible_test(test_session_self_patterns_basic)
crucible_test(test_session_self_patterns_fan)
crucible_test(test_session_self_patterns_subtype)
crucible_test(test_session_self_patterns_crash)
crucible_test(test_session_self_patterns_delegate)
crucible_test(test_session_self_patterns_compose)
crucible_test(test_session_self_payload)
crucible_test(test_session_self_subtype)
crucible_test(test_session_self_view)

# Protocol-tree crash-branch walker (#368, SEPLOG-BUG-4).
# every_offer_has_crash_branch_for_peer_v<Proto, PeerTag> recurses
# through End/Stop/Continue/Send/Recv/Select/Offer/Loop/Delegate/
# Accept; catches the "outer Offer has a crash branch, inner Offer
# doesn't" pattern that `has_crash_branch_for_peer_v` alone misses.
# Pure compile-time; main() just confirms the consteval assertion
# point passed for crash-safe protocols.
crucible_test(test_session_crash_walker)

# Sender<Role> role annotation for Offer<> (#367, SEPLOG-STRUCT-1).
# MPST crash analysis needs to know which role sends each Offer to
# answer correctly "does this Offer need a Recv<Crash<PeerTag>, _>
# branch?" — the answer is YES only when PeerTag is the Offer's
# sender.  Offer<Sender<Role>, Branches...> declares the sender;
# unannotated Offer<> keeps 2-party semantics via AnonymousPeer.
crucible_test(test_session_sender_role)

# OneShotFlag × runtime crash transport (#400, SAFEINT-A11).
# Wires the type-level Stop/Crash semantics from SessionCrash.h into
# a runtime wire: CrashWatchedHandle<Proto, R, PeerTag, CrashClass C, LoopCtx, PS>
# wraps a PermissionedSessionHandle with a OneShotFlag& and returns
# std::expected<NextHandle, survivor-aware CrashEvent> from every
# consumer method.
# On-crash the per-op peek routes into the unexpected branch;
# happy-path overhead is one relaxed atomic load.  Validates happy
# path, crash-before-send, crash-mid-protocol, cross-thread crash
# detection (jthread producer + spinning consumer), multi-peer
# independent watches, and the CNTP request/reply + retry pattern.
crucible_test(test_crash_transport)

# GAPS-046 — CrashWatchedPSH permission-recovery race harness.
# Runs under every preset, and under the `tsan` preset with
# -fsanitize=thread through the shared crucible_test factory.  The
# body stress-runs 50 iterations covering clean peer death, death
# racing a send, concurrent deaths, and fallback survivor observation.
crucible_test(test_crash_watched_psh_tsan
  tsan/test_crash_watched_psh_tsan.cpp)

# fixy-A2-031 — CrashWatchedHandle mid-transport TOCTOU witness.
# Pins the race window between `flag_->peek()` (relaxed) and
# `inner_.send/recv(transport)` — the existing psh_tsan suite
# covers flag-set-before-op and concurrent-without-pin paths but
# never lands the kill strictly BETWEEN peek and transport-return.
# This test orchestrates barrier atomics so the kill happens
# deterministically mid-call across recv/send/close heads, with
# WorkerTag → CoordTag → MasterTag survivor inheritance.  Each
# scenario runs 50× under default + tsan presets.
crucible_test(test_crash_watched_recv_toctou_tsan
  tsan/test_crash_watched_recv_toctou_tsan.cpp)

# AppendOnly session event log + RecordingSessionHandle wrapper
# (#404, SAFEINT-B15).  Promotes bit-exact-replay discipline from an
# ad-hoc trace into a typed structure: SessionEventLog (Pinned,
# OrderedAppendOnly-backed, monotonic step counter) plus
# RecordingSessionHandle<Proto, R> wrapper that records every
# Send/Recv/Select/Offer/close into the log.  Validates the log
# primitive, the per-combinator wrapper specialisations, the
# bilateral request/reply capture pattern, the replay-determinism
# property (same protocol → same event sequence modulo step offset),
# the payload-hash opt-in via default_payload_hash_fn specialisation,
# and Offer branch dispatch capture.
crucible_test(test_session_event_log)
crucible_test(test_session_persistence)

# CTPayload + ct::eq for crypto-payload sessions (#409, SAFEINT-C20).
# CTPayload<T> raises constant-time-comparison discipline to the API
# surface: requires_ct<T> trait opt-in, move-only wrapper with
# explicitly-deleted operator==/!= (forces ct::eq), .bytes() byte-
# view for CT byte-comparison, .declassify_ct<Policy>() for explicit
# extraction.  Verifies the API-level rejection of naive comparison,
# subsort asymmetry, and the HMAC-tag verification worked example
# from §20.
crucible_test(test_session_ct)

function(crucible_neg_compile_test name expected_diag)
  # Build target that is NOT part of default build — only reachable
  # via explicit developer build. The ctest harness below replays this
  # target's compile command from compile_commands.json into an isolated
  # object path so negative tests can run in parallel without nested
  # Ninja writers racing on the shared build tree.
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  # PASS_REGULAR_EXPRESSION passes the test iff the build output
  # contains the expected-diagnostic substring.  A "clean" build
  # (i.e., the discipline slipped and the .cpp compiled) produces no
  # such text → test fails.  An unrelated compile error (typo in
  # the .cpp) also produces different text → test fails, surfacing
  # the drift.  This is strictly stronger than WILL_FAIL.
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "safety_neg")
endfunction()

# Sister harness for sessions_neg/.  Mirrors crucible_neg_compile_test
# pattern exactly — only the source directory and label differ.
# Sessions-side neg-compile tests (FOUND-C Phase 7, task #618) live
# in test/sessions_neg/ rather than test/safety_neg/ to keep the
# permission-flow witnesses adjacent to the PSH header they probe.
function(crucible_neg_compile_session_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/sessions_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/sessions_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "sessions_neg")
endfunction()

# Sister harness for effects_neg/.  Mirrors the safety_neg / sessions_neg
# pattern — only the source directory and label differ.  Effects-side
# neg-compile tests (FOUND-B13, task #602) probe the Subrow concept's
# substitution-principle gates on Computation.h: extract on non-empty
# row, weaken to disjoint row, lift with non-Effect template arg.
function(crucible_neg_compile_effects_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/effects_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/effects_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "effects_neg")
endfunction()

# Sister harness for cog_neg/.  Mirrors the safety_neg / sessions_neg
# / effects_neg pattern — only the source directory and label differ.
# Cog-side neg-compile tests (GAPS-185 onward) live in test/cog_neg/
# rather than test/effects_neg/ so the per-Cog soundness gates
# (content_hash precondition, IsComputeKind concept, FOUND-I04 frozen
# positions on CogLevel/CogKind, future TargetCaps fits) sit
# adjacent to the cog/ headers they probe.
function(crucible_neg_compile_cog_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/cog_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/cog_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "cog_neg")
endfunction()

# Sister harness for warden_neg/. Warden-policy negative tests live here
# when they are not primarily probing the generic effects substrate.
function(crucible_neg_compile_warden_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/warden_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/warden_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "warden_neg")
endfunction()

# Sister harness for perf_neg/. Per-perf-hub Ctx-fit negative tests
# (FIXY-U-083) live here — mint_<hub> rejects BgDrainCtx / HotFgCtx
# because the BPF program load + perf_event_open + mmap path is an
# Init-row-only startup operation.
function(crucible_neg_compile_perf_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/perf_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/perf_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "perf_neg")
endfunction()

# Sister harness for dataplane_neg/. XDP/TC negative tests live beside the
# CNT-P dataplane ownership boundary instead of Warden.
function(crucible_neg_compile_dataplane_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/dataplane_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/dataplane_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "dataplane_neg")
endfunction()

# Sister harness for topology_neg/.  Mirrors cog_neg / safety_neg /
# sessions_neg / effects_neg.  Topology-side neg-compile tests live in
# test/topology_neg/ so the per-graph soundness gates
# (CtxFitsTopologyGraph IsExecCtx conjunct, row-contains-Init conjunct,
# future link-layer partition assertions) sit adjacent to the topology/
# headers they probe.
function(crucible_neg_compile_topology_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/topology_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/topology_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "topology_neg")
endfunction()

# fixy-A2-016 — HS14 floor for the six bridges/* mint factories that
# previously had zero negative-compile coverage:
#   mint_recording_session(SessionHandle, ...)
#   mint_recording_session(CrashWatchedHandle, ...)
#   mint_recording_endpoint(Endpoint&&, ...)
#   mint_crash_watched_endpoint<PeerTag>(Endpoint&&, ...)
#   mint_persisted_session(ctx, cipher, view, resource, ...)
#   mint_persisted_session(ctx, SessionHandle, cipher, view, ...)
# Each factory's requires-clause / parameter-shape gate is witnessed
# by ≥2 fixtures probing distinct mismatch classes (non-handle input,
# wrong companion-type, missing template arg, deleted overload via
# `= delete("reason")`, ctx-row admission failure).
function(crucible_neg_compile_bridges_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/bridges_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/bridges_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "bridges_neg")
endfunction()

# ── FIXY-CLEAN Phase A — neg-compile fixture harness ────────────────
#
# 12 per-axis engagement-gap fixtures live in test/fixy_neg/.  Each
# fixture instantiates the IsAccepted concept with a 19-engaged pack
# that omits exactly one axis; the named diagnostic
# `FixyNotEngaged_<Axis>` must appear in the compile output.

function(crucible_neg_compile_fixy_test name expected_diag)
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "fixy_neg")
endfunction()

crucible_neg_compile_fixy_test(neg_fixy_unengaged_usage          "FixyNotEngaged_Usage")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_effect         "FixyNotEngaged_Effect")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_security       "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_protocol       "FixyNotEngaged_Protocol")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_lifetime       "FixyNotEngaged_Lifetime")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_provenance     "FixyNotEngaged_Provenance")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_trust          "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_representation "FixyNotEngaged_Representation")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_complexity     "FixyNotEngaged_Complexity")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_precision      "FixyNotEngaged_Precision")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_mutation       "FixyNotEngaged_Mutation")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_staleness      "FixyNotEngaged_Staleness")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_type           "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_refinement     "FixyNotEngaged_Refinement")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_observability  "FixyNotEngaged_Observability")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_space          "FixyNotEngaged_Space")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_overflow       "FixyNotEngaged_Overflow")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_reentrancy     "FixyNotEngaged_Reentrancy")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_size           "FixyNotEngaged_Size")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_version        "FixyNotEngaged_Version")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_synchronization "FixyNotEngaged_Synchronization")
crucible_neg_compile_fixy_test(neg_fixy_unengaged_regime         "FixyNotEngaged_Regime")

# FIXY-AUDIT-D2 — HS14 floor for mint_fn factory.
crucible_neg_compile_fixy_test(neg_fixy_mint_fn_unengaged_axis   "AllDimsEngaged")
crucible_neg_compile_fixy_test(neg_fixy_mint_fn_invalid_grant    "AllGrantsWellFormed")

# fixy-A4-033 — IsGrantTag rejects cv-/ref-qualified grants.  Two
# fixtures cover orthogonal copy-paste-from-runtime bug classes
# (cv-qualification vs reference-qualification).
crucible_neg_compile_fixy_test(neg_fixy_grant_cv_qualified       "AllGrantsWellFormed")
crucible_neg_compile_fixy_test(neg_fixy_grant_reference          "AllGrantsWellFormed")

# fixy-M-09 — structural-validation concepts for parametric grant
# tags.  Each fixture demonstrates the requires-clause fires on its
# OWN gate's distinct rejection class.  Regex matches the named
# concept OR "constraints not satisfied" (GCC's surface for a
# requires-clause failure on the class template).
crucible_neg_compile_fixy_test(neg_fixy_grant_protocol_non_class
    "constraints not satisfied|IsSessionProtocol|is_class")
crucible_neg_compile_fixy_test(neg_fixy_grant_refined_with_non_class
    "constraints not satisfied|IsRefinementPredicate|is_class")
crucible_neg_compile_fixy_test(neg_fixy_grant_declassify_non_policy
    "constraints not satisfied|DeclassificationPolicy|secret_policy_base")
crucible_neg_compile_fixy_test(neg_fixy_grant_from_source_non_empty
    "constraints not satisfied|IsProvenanceSource|is_empty|is_class")

# fixy-A4-018 — §XXI Universal-Mint-Pattern enforcement.  fixy::fn's
# value ctor is private; only the three canonical mint factories
# (mint_fn, mint_fn_for<UnaryStance>, mint_fn_for<BinaryStance>) are
# friended to construct.  Two fixtures cover orthogonal call-site
# shapes the reviewer might see in production:
#   - direct_ctor_bypass — raw `fixy::fn<T, all-strict-grants>{42}`
#   - stance_ctor_bypass — alias `fixy::stance::PureLinear<int>{42}`
# Both reject with "private within this context" / "declared private".
crucible_neg_compile_fixy_test(neg_fixy_fn_direct_ctor_bypass    "private")
crucible_neg_compile_fixy_test(neg_fixy_fn_stance_ctor_bypass    "private")

# FIXY-AUDIT-E10 + fixy-H-01 — HS14 floor for mint_fn_for stance-bound
# mint.  fixy-H-01 hardened mint_fn_for with explicit requires-clauses
# (`StanceForUnary` for 1-param stances, `StanceForBinary` for 2-param
# stances).  Four fixtures pin distinct Type-axis rejection causes per
# overload (array decay + const-qualified × unary + binary).
crucible_neg_compile_fixy_test(neg_fixy_fn_for_array_type        "StanceForUnary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_const_type        "StanceForUnary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_array_type "StanceForBinary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_const_type "StanceForBinary")
# fixy-A4-019: Policy-axis fixtures (void + reference) prove the
# extended StanceForBinary gate rejects bad-Policy shapes BEFORE
# Stance<Type, Policy> would instantiate.  Sibling to the existing
# Type-axis pair above.
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_void_policy "StanceForBinary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_ref_policy  "StanceForBinary")

# fixy-A4-025: CTAD rejection — `fixy::fn{value}` is intentionally
# non-functional per CLAUDE.md §XXI Universal Mint Pattern.  The
# deduction guide routes CTAD to a sentinel-typed instantiation that
# fires a dedicated tier-0 static_assert naming mint_fn / mint_fn_for
# as the §XXI-correct entry points.  Two fixtures (HS14 ≥2 floor)
# cover distinct mismatch classes: the deduction-guide route
# (`fixy::fn{42}`) AND the direct-sentinel back-door
# (`fixy::fn<sentinel>{}`).  Both regex on the sentinel CLASS NAME
# (which appears verbatim in the compiler "required from" trail) OR
# the tier-0 message prefix.
crucible_neg_compile_fixy_test(neg_fixy_fn_ctad_attempt
    "fn_ctad_blocked_use_mint_fn|tier 0")
crucible_neg_compile_fixy_test(neg_fixy_fn_direct_sentinel_use
    "fn_ctad_blocked_use_mint_fn|tier 0")

# FIXY-AUDIT-E10 — HS14 floor for fixy::sess::mint_federation_channel.
# Surface-level IsExecCtx<Ctx> gate + downstream protocol-shape gate.
crucible_neg_compile_fixy_test(neg_fixy_federation_channel_non_ctx
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_federation_channel_wrong_endpoint
    "SessionResource_NotPinned")

# fixy-CR-13 — HS14 floor for the CtxFitsFederation row gate at the
# fixy wrapper boundary.  Two rejection classes: (a) Fg cap empty row
# (HotFgCtx can't widen — Fg::permitted_row is Row<>), (b) strict
# subset (BgCompileCtx carries Row<Bg, Alloc, IO> — missing Block).
crucible_neg_compile_fixy_test(neg_fixy_federation_channel_no_row_fg
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_federation_channel_no_row_bg_compile
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|no matching function")

# FIXY-AUDIT-E10 — HS14 floor for fixy::safety::mint_secret re-export.
# is_constructible_v gate covered with two distinct argument shapes:
# type-mismatch (int <- const char*) and arity-mismatch (int(a,b,c)).
crucible_neg_compile_fixy_test(neg_fixy_safety_secret_not_constructible
    "is_constructible_v")
crucible_neg_compile_fixy_test(neg_fixy_safety_secret_too_many_args
    "is_constructible_v")

# FIXY-AUDIT-D3 — HS14 floor for fixy::fn class-body static_assert.
# Distinct from D2 (mint_fn requires-clause): the class-body assert
# fires inside class-template instantiation, not overload resolution.
crucible_neg_compile_fixy_test(neg_fixy_fn_class_body_unengaged
    "FixyNotEngaged_<Axis>|requires every DimensionAxis to be engaged")

# ── FIXY-AUDIT-D4 — stance alias HS14 floor ───────────────────────
#
# Each of the 8 shipped stance aliases (PureLinear / PureCopy /
# IoFunction / BgWorker / SecretConsumer / AsyncEndpoint / CtCrypto /
# PublicEmit) ships >=2 fixtures exercising a Type-axis failure mode.
# Distinct rejection classes across the family:
#
#   *_array         — C array decay
#   *_void          — void is not an object
#   *_ref           — lvalue reference
#   *_rvalue_ref    — rvalue reference
#   *_const         — top-level const
#   *_volatile      — top-level volatile
#   *_function      — bare function type
#
# All route through IsAccepted's type-axis check; the substrate's
# safety::fn::Fn class-body static_assert also fires (named "Fn<...>
# is malformed"), so the driver greps for "IsAccepted" which is the
# satisfaction-chain root.
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_linear_array       "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_linear_void        "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_copy_ref           "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_copy_function      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_iofunction_const        "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_iofunction_volatile     "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_bgworker_rvalue_ref     "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_bgworker_void           "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_secret_consumer_array   "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_secret_consumer_void    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_async_endpoint_ref      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_async_endpoint_array    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_ctcrypto_void           "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_publicemit_ref          "IsAccepted")

# FIXY-U-041 — HS14 floor for the 4 new stance aliases.  2 fixtures
# per stance, each a distinct Type-axis rejection class (void vs
# array vs reference).
crucible_neg_compile_fixy_test(neg_fixy_stance_named_session_void      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_named_session_array     "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_cooperative_bg_void     "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_cooperative_bg_ref      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_sync_blocking_void      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_sync_blocking_array     "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_realtime_hot_void       "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_realtime_hot_ref        "IsAccepted")

# fixy-A4-027 — [[nodiscard]] preservation regression guard for the
# `mint_federation_admittance` using-decl re-export.  Audit flagged a
# §XXI mint discipline risk; this fixture empirically witnesses that
# discarding the return triggers -Werror=unused-result.
crucible_neg_compile_fixy_test(neg_fixy_federation_admittance_nodiscard "unused-result")

# FIXY-AUDIT-A3 — HS14 floor for UniqueEngagementPerAxis.
crucible_neg_compile_fixy_test(neg_fixy_duplicate_engagement     "UniqueEngagementPerAxis")

# fixy-H-02 — HS14 floor for the BRANCHED fn<> class-body static_assert
# chain.  Three fixtures, one per tier-of-interest (tier 2 / 3 / 4).
# Each pin a distinct rejection diagnostic so the chain's branching is
# verified — pre-H-02 ALL THREE would have surfaced the same misleading
# "axis not engaged" message; post-H-02 each tier names its own
# inspection helper + diagnostic-tag family.
#
#   * neg_fixy_fn_static_assert_missing_axis     — tier 3 AllDimsEngaged
#   * neg_fixy_fn_static_assert_duplicate_axis   — tier 4 UniqueEngagementPerAxis
#   * neg_fixy_fn_static_assert_malformed_grant  — tier 2 AllGrantsWellFormed
crucible_neg_compile_fixy_test(neg_fixy_fn_static_assert_missing_axis
    "first_missing_axis_v|FixyNotEngaged_")
crucible_neg_compile_fixy_test(neg_fixy_fn_static_assert_duplicate_axis
    "first_duplicate_axis_v|FixyDuplicate_")
crucible_neg_compile_fixy_test(neg_fixy_fn_static_assert_malformed_grant
    "FixyMalformedGrant|AllGrantsWellFormed")

# fixy-H-05 — HS14 floor for the wrapper-discipline IsAccepted.
#
# Two fixtures pin that the auto-injected ImplicitTypeMarker collides
# with a user-spelled `strict<D::Type>`, surfacing the duplicate-axis
# rejection from BOTH the concept-level gate (Reject.h) AND the
# mint_fn factory's requires-clause (Fn.h).
#
#   * neg_fixy_h05_is_accepted_explicit_type_marker — concept-level
#   * neg_fixy_h05_mint_fn_explicit_type_marker     — mint_fn factory
crucible_neg_compile_fixy_test(neg_fixy_h05_is_accepted_explicit_type_marker
    "UniqueEngagementPerAxis|first_duplicate_axis_v|FixyDuplicate_")
crucible_neg_compile_fixy_test(neg_fixy_h05_mint_fn_explicit_type_marker
    "IsAccepted|UniqueEngagementPerAxis|FixyDuplicate_")

# fixy-CR-08 — HS14 floor for engages_dim_one<D, G> gating.
#
# Pre-CR-08 the `engaged_for` and `count_engagements_for` folds
# substituted `grant::which_dim_v<G>` eagerly for every G in the
# pack — a non-grant G hit the undefined `which_dim` primary template
# inside the fold rather than failing cleanly at IsAcceptedGrants's
# AllGrantsWellFormed gate.  The fix routes each G through
# `detail::engagement::engages_dim_one<D, G>()` which uses
# `if constexpr (grant::IsGrantTag_v<G>)` to gate the lookup.
# Both fixtures inject a non-grant member alongside an otherwise-
# complete strict pack and rely on AllGrantsWellFormed / IsGrantTag
# to surface the rejection cleanly (NOT a `which_dim` substitution
# failure deep inside the fold).
crucible_neg_compile_fixy_test(neg_fixy_engaged_for_non_grant
    "IsGrantTag|AllGrantsWellFormed|IsAccepted|which_dim|incomplete type")
crucible_neg_compile_fixy_test(neg_fixy_count_engagements_non_grant
    "IsGrantTag|AllGrantsWellFormed|IsAccepted|which_dim|incomplete type")

# FIXY-AUDIT-D5 — federation mint HS14 floor.
#
# `mint_federation_admittance(const LocalCipherPermission&, FederationHandshake)`
# is a constexpr token mint with a runtime expected<> error surface.
# The compile-time gates are the parameter types: the proof slot
# demands a `Permission<tag::LocalCipherTag>` reference, and the
# handshake slot demands the `FederationHandshake` POD.  Each
# fixture exercises one of those type-level rejections.
crucible_neg_compile_fixy_test(neg_fixy_federation_wrong_org
    "mint_federation_admittance|cannot bind|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_federation_missing_proof
    "mint_federation_admittance|cannot bind|no matching function")

# FIXY-AUDIT-D8 — mint_session_handle HS14 floor.
#
# `mint_session_handle<Proto>(resource)` (sessions/Session.h §2252)
# ships three internal static_asserts: (1) is_well_formed_v<Proto>,
# (2) !is_empty_choice_v<Proto>, (3) SessionResource<Resource>.
# Fixtures exercise (1) and (3) — the two most common author errors.
crucible_neg_compile_fixy_test(neg_fixy_session_handle_malformed_proto
    "Protocol_Ill_Formed")
crucible_neg_compile_fixy_test(neg_fixy_session_handle_non_resource
    "SessionResource_NotPinned")

# FIXY-AUDIT-B1 — HS14 floor for Theory.h §30.14 corpus.
# fixy-H-16: the tier-5 rejection diagnostic must literally name the
# matched corpus entry (struct identifier) AND surface the paper +
# year + remediation prose from cite().  Regex anchors require BOTH
# the concept-fail "NotInTheoryCorpus" trace AND the entry name —
# proves the doc-block §IsAccepted-composition claim ("names which
# corpus entry matched") is honored in code.
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_io
    "NotInTheoryCorpus|classified_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_bg
    "NotInTheoryCorpus|classified_bg_without_declassify")

# fixy-CR-01 — strict-default Security bypass closure.
# Each corpus entry gets a second fixture using `strict<D::Security>`
# (= accept_default_strict_for<Security>, resolves to Classified) to
# witness that the §30.14 detection covers BOTH spellings of the
# Security grant: explicit (as_secret) AND strict-default form.  Before
# Theory.h's second is_secret_grant specialization, the strict-default
# form silently bypassed the corpus despite semantic equivalence.
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_io_strict_default
    "NotInTheoryCorpus|classified_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_bg_strict_default
    "NotInTheoryCorpus|classified_bg_without_declassify")

# ── FIXY-AUDIT-G4 — stance composition rejection.  Stance aliases are
# typedefs `fn<Type, ...full pack...>`; they cannot be supplied as
# grant tags, and a stance's implicit relaxation cannot be supplemented
# with a strict marker on the same axis without colliding on
# UniqueEngagementPerAxis.  Two fixtures pin both shapes:
crucible_neg_compile_fixy_test(neg_fixy_relaxation_plus_strict_same_axis
    "UniqueEngagementPerAxis|IsAcceptedGrants|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_stance_as_grant
    "AllGrantsWellFormed|IsGrantTag|constraints not satisfied|associated constraints are not satisfied")

# FIXY-AUDIT-C8 — HS14 floor for FixyCatalog reconciliation surface.
#
# Two distinct rejection classes for `fixy::diag::is_fixy_diag_v<T>`:
#   - substrate Catalog tag (must NOT register as fixy diagnostic) —
#     the closed-set discipline that keeps Catalog ∩ FixyCatalog == ∅
#   - plain primitive type (catalog membership is NOT just tag_base
#     inheritance — it requires actual tuple presence)
crucible_neg_compile_fixy_test(neg_fixy_diag_catalog_substrate_tag
    "is_fixy_diag_v")
crucible_neg_compile_fixy_test(neg_fixy_diag_catalog_plain_type
    "is_fixy_diag_v")

# FIXY-AUDIT-B3 — HS14 floor for stance::CtCrypto + stance::PublicEmit.
#
# CtCrypto pins as_secret + empty Effect row; the §30.14 detector
# fires when a CtCrypto-shape pack adds IO (classified-IO-without-
# declassify pattern).  Removing the Security engagement leaves the
# axis unengaged and fires the engagement gate.
#
# PublicEmit pins declassify<Policy> + IO; swapping declassify for
# as_secret recreates the §30.14 implicit-flow pattern.  Removing
# Effect engagement leaves the axis unengaged.
crucible_neg_compile_fixy_test(neg_fixy_ctcrypto_with_io
    "NotInTheoryCorpus")
crucible_neg_compile_fixy_test(neg_fixy_ctcrypto_unengaged_security
    "AllDimsEngaged")
crucible_neg_compile_fixy_test(neg_fixy_publicemit_with_secret
    "NotInTheoryCorpus")
crucible_neg_compile_fixy_test(neg_fixy_publicemit_unengaged_effect
    "AllDimsEngaged")

# ── FIXY-AUDIT-B11 — Type-axis well-formedness fixtures ───────────
#
# IsAccepted's `type_is_accepted_payload<Type>` check rejects array,
# reference, top-level const, top-level volatile, and bare function
# types.  Each fixture instantiates mint_fn / fixy::fn with the offending
# Type so the requires-clause fires.  The driver greps for "IsAccepted"
# — the satisfaction-failure chain names the top-level concept since
# Type-axis rejection happens inside the IsAccepted body, NOT via a
# named per-axis diagnostic tag.
crucible_neg_compile_fixy_test(neg_fixy_fn_type_array      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_ref        "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_const      "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_volatile   "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_function   "IsAccepted")

# ── FIXY-CLEAN-B4 — 12 collision-rule neg-compile fixtures ─────────
#
# Each fixture instantiates fixy::fn<int, all-strict-pack> after
# specialising one or more `marks_*` traits on the resolved Fn type,
# tripping exactly one §6.8 collision rule.  The named static_assert
# message from CollisionCatalog.h's validate() carries the rule's
# short code (e.g. "I002:") which the driver greps.
crucible_neg_compile_fixy_test(neg_fixy_rule_i002 "I002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_l002 "L002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_e044 "E044:")
crucible_neg_compile_fixy_test(neg_fixy_rule_i003 "I003:")
crucible_neg_compile_fixy_test(neg_fixy_rule_m012 "M012:")
crucible_neg_compile_fixy_test(neg_fixy_rule_p002 "P002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_i004 "I004:")
crucible_neg_compile_fixy_test(neg_fixy_rule_n002 "N002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_l003 "L003:")
crucible_neg_compile_fixy_test(neg_fixy_rule_m011 "M011:")
crucible_neg_compile_fixy_test(neg_fixy_rule_s010 "S010:")
crucible_neg_compile_fixy_test(neg_fixy_rule_s011 "S011:")

# ── FIXY-LAT-Security + FIXY-LAT-Trust HS14 neg-compile fixtures ──
#
# Each fixture demonstrates a distinct mismatch class for the new
# Security / Trust lattice grant tags introduced by FIXY-LAT-Security
# (gr::as_unclassified / as_public / as_internal / as_classified /
# as_secret) and FIXY-LAT-Trust (gr::trust_verified / trust_tested /
# trust_unverified / trust_external).
#
#   _secret_wrong_axis    — as_secret in lieu of Trust grant: Trust
#                            axis unengaged, FixyNotEngaged_Trust fires
#   _unclassified_only    — as_unclassified is the only engagement: 19
#                            other axes unengaged, FixyNotEngaged_Type
#   _external_wrong_axis  — trust_external in lieu of Security grant:
#                            Security axis unengaged, FixyNotEngaged_Security
#   _tested_only          — trust_tested is the only engagement: 19
#                            other axes unengaged, FixyNotEngaged_Type
crucible_neg_compile_fixy_test(neg_fixy_lat_secret_wrong_axis   "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_unclassified_only   "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_external_wrong_axis "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_tested_only         "FixyNotEngaged_Type")

# ── FIXY-AUDIT-D10 — full Security/Trust lattice neg-compile sweep
#
# 13 additional fixtures bring per-lattice-point coverage to >=14
# total (4 pre-existing + 13 new = 17).  For each lattice point on
# the Security axis (as_unclassified / as_public / as_internal /
# as_classified / as_secret) and Trust axis (trust_verified /
# trust_tested / trust_unverified / trust_external), at least one
# wrong-axis fixture and one only-engagement fixture proves the
# IsAccepted gate fires with the expected per-axis diagnostic tag.
#
#   *_wrong_axis  — tag used in the OTHER axis's slot leaves that
#                   axis unengaged → FixyNotEngaged_<OtherAxis>
#   *_only        — tag alone leaves 19 other axes unengaged →
#                   FixyNotEngaged_Type (first missing axis)
crucible_neg_compile_fixy_test(neg_fixy_lat_public_wrong_axis      "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_internal_wrong_axis    "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_classified_wrong_axis  "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_public_only            "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_internal_only          "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_classified_only        "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_secret_only            "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_verified_wrong_axis    "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_unverified_wrong_axis  "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_tested_wrong_axis      "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_verified_only          "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_unverified_only        "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_external_only          "FixyNotEngaged_Type")

# ── FIXY-MINT-Cap — fixy/Cap.h re-export of effects::mint_cap +
# effects::mint_from_ctx.  Two HS14 fixtures pin the alias's gate:
crucible_neg_compile_fixy_test(neg_fixy_cap_mint_from_fg
    "associated constraints are not satisfied|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_cap_mint_from_ctx_wrong_effect
    "associated constraints are not satisfied|constraints not satisfied")

# ── FIXY-MINT-Perm — fixy/Perm.h re-export of permission token mints
# (mint_permission_root/split/combine/split_n/combine_n/share/fork/
# inherit).  Six HS14 fixtures pin the alias's gate — two per frame-rule
# direction (split/combine binary, split_n/combine_n N-ary) plus the
# original root + fork ctx fixtures.  Each fixture witnesses a distinct
# rejection class (CtxAdmitsPermission / CtxFitsPermissionFork /
# splits_into / splits_into_pack):
crucible_neg_compile_fixy_test(neg_fixy_perm_root_no_row_admission
    "CtxAdmitsPermission|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perm_root_non_exec_ctx
    "IsExecCtx|constraints not satisfied|associated constraints are not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_perm_fork_ctx_no_bg
    "CtxFitsPermissionFork|row_contains_v|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perm_fork_no_splits_into_pack
    "CtxFitsPermissionFork|splits_into_pack|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_undeclared
    "splits_into")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_undeclared
    "splits_into")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_n_undeclared
    "splits_into_pack")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_n_undeclared
    "splits_into_pack")
# fixy-M-29 HS14 floor: two fixtures pin the type-level companion
# witness traits (splits_into_authoring_witness +
# splits_into_pack_authoring_witness) — orphan splits_into / _pack
# specializations without the corresponding witness must fail at
# mint_permission_split / _n body-level static_assert.  Distinct
# mismatch classes: binary vs N-ary trait pair, distinct rejection
# points in Permission.h.
crucible_neg_compile_fixy_test(neg_fixy_perm_split_witness_missing
    "splits_into_authoring_witness")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_n_witness_missing
    "splits_into_pack_authoring_witness")
# FIXY-U-074b HS14 floor: combine is the symmetric inverse of split and
# reads the SAME witness-trait pair (Permission.h:753 / :850), so the
# combine minters need the same two distinct mismatch classes split has.
# These two bring mint_permission_combine / _n from HS14:1 -> 2: the
# witness-missing class (splits_into[_pack] true but the M-29 authoring
# witness absent), companion to the existing *_undeclared fixtures.
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_witness_missing
    "splits_into_authoring_witness")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_n_witness_missing
    "splits_into_pack_authoring_witness")

# ── FIXY-MINT-Contract — fixy/Contract.h re-export of Cipher tier
# migration mints (mint_promote, mint_demote, mint_restore).  HS14
# floor: two fixtures pin the chain-direction concept gate
# (PromotableTier / DemotableTier reduce to can_promote_tier_v /
# can_demote_tier_v, which reduce to tier_replaces() over the
# CipherTier lattice ordinal: Cold=0 < Warm=1 < Hot=2).  A promotion
# DOWN the chain (Hot → Cold) and a demotion UP the chain (Cold → Hot)
# are both structurally invalid; routing through the fixy:: alias must
# reject identically to the substrate:
crucible_neg_compile_fixy_test(neg_fixy_contract_promote_wrong_direction
    "PromotableTier|can_promote_tier|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_contract_promote_source_tier_mismatch
    "no matching function|could not match|deduced conflicting|cannot convert.*CipherTier")
crucible_neg_compile_fixy_test(neg_fixy_contract_demote_wrong_direction
    "DemotableTier|can_demote_tier|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_contract_demote_source_tier_mismatch
    "no matching function|could not match|deduced conflicting|cannot convert.*CipherTier")

# ── FIXY-MINT-Sess — fixy/Sess.h re-export of session minters
# (mint_session=deleted, mint_permissioned_session, mint_channel,
# mint_session_handle, mint_recording_session, mint_crash_watched_session)
# plus federation::mint_sender/receiver/coord.  Two HS14 fixtures
# pin the deleted-overload diagnostic and the channel ctx-fit gate:
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_session_deleted
    "is removed|mint_permissioned_session|use of deleted function")
# fixy-A4-014: bare mint_session<Proto>(resource) — second deleted overload.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_session_bare_deleted
    "is removed|mint_permissioned_session|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_sess_channel_recv_row_drift
    "CtxFitsChannel|CtxFitsPermissionedProtocol|constraints not satisfied|associated constraints are not satisfied")

# ── FIXY-MINT-Pipe — fixy/Pipe.h re-export of Tier-3 composition
# (mint_endpoint, mint_stage, mint_pipeline, mint_pipeline_dag,
# mint_stage_from_endpoints, mint_mpmc_stage_from_endpoints,
# mint_swmr_stage, mint_substrate_session).  Two HS14 fixtures pin
# the endpoint substrate-fit gate and the pipeline chain gate:
crucible_neg_compile_fixy_test(neg_fixy_pipe_endpoint_non_bridgeable
    "IsBridgeableDirection|CtxFitsEndpointMint|constraints not satisfied|associated constraints are not satisfied|handle_for|incomplete type")
crucible_neg_compile_fixy_test(neg_fixy_pipe_pipeline_non_stage
    "CtxFitsPipeline|pipeline_chain|IsStage|constraints not satisfied|associated constraints are not satisfied")
# FIXY-U-074c HS14 floor: mint_pipeline sat at HS14:1 (only non_stage).
# Second distinct class — two valid Stages that fail the stages_chain
# adjacent-compatibility fold (int->int then double->double), distinct
# from non_stage's IsStage-conjunct failure.  Brings mint_pipeline to 2.
crucible_neg_compile_fixy_test(neg_fixy_pipe_pipeline_chain_break
    "CtxFitsPipeline|pipeline_chain|stages_chain|constraints not satisfied|associated constraints are not satisfied")

# ── FIXY-AUDIT-D7 — mint_pipeline_dag / mint_mpmc_stage_from_endpoints
# / mint_swmr_stage HS14 floor.
#
# Three ctx-bound mints with multi-conjunct requires-clauses.  Two
# fixtures per factory pin distinct rejection classes:
#
#   mint_pipeline_dag:
#     _non_graph              — Graph param is not IsStageGraph
#     _stage_pack_mismatch    — pack size/types disagree with Graph
#
#   mint_mpmc_stage_from_endpoints:
#     _non_endpoint           — variadic pack contains non-Endpoint
#     _arity_mismatch         — sizeof...(Endpoints) != FnPtr arity
#
#   mint_swmr_stage:
#     _non_consumer_ep        — first param fails IsConsumerEndpoint
#     _non_writer             — second param fails is_swmr_writer_v
crucible_neg_compile_fixy_test(neg_fixy_pipe_pipeline_dag_non_graph
    "CtxFitsPipelineDagMint|CtxFitsPipelineDag|IsStageGraph|pipeline_dag_mint_gate|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_pipe_pipeline_dag_stage_pack_mismatch
    "CtxFitsPipelineDagMint|stage_pack_type|pipeline_dag_mint_gate|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_pipe_mpmc_stage_non_endpoint
    "CtxFitsMpmcStageFromEndpoints|mpmc_stage_from_endpoints_gate|StageHandlesMatchEndpointsExtended|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_pipe_mpmc_stage_arity_mismatch
    "CtxFitsMpmcStageFromEndpoints|mpmc_stage_from_endpoints_gate|arity|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_pipe_swmr_stage_non_consumer_ep
    "CtxFitsSwmrStageFromEndpoint|swmr_stage_from_endpoint_gate|IsConsumerEndpoint|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_pipe_swmr_stage_non_writer
    "CtxFitsSwmrStageFromEndpoint|swmr_stage_from_endpoint_gate|is_swmr_writer|constraints not satisfied|associated constraints are not satisfied")

# ── FIXY-MINT-Bridge — fixy/Bridge.h re-export of wrap factories
# (mint_recording_session, mint_crash_watched_session,
# mint_persisted_session, mint_recording_endpoint,
# mint_crash_watched_endpoint, mint_vigil_mode_bridge).  Two HS14
# fixtures pin the persisted-session ctx gate and the
# crash-watched NoThrow contradiction:
crucible_neg_compile_fixy_test(neg_fixy_bridge_persisted_session_hot_ctx
    "PersistedSession|CipherTier|HotFgCtx|constraints not satisfied|associated constraints are not satisfied|IO|Block")
# fixy-HS14-01 — mint_recording_session HS14 floor (≥2 fixtures via
# the fixy::bridge:: re-export).  Two distinct mismatch classes:
# (#1) non-handle first argument trips overload-set rejection across
# all three substrate overloads (bare SessionHandle, CrashWatched,
# PSH); (#2) valid first argument selects the bare overload, then
# wrong-log-type second argument fails reference-binding.  Mirrors
# the substrate-side pair (test/bridges_neg/neg_mint_recording_session_*)
# at the fixy:: wrapping boundary.
crucible_neg_compile_fixy_test(neg_fixy_bridge_recording_session_non_handle
    "no matching function|mint_recording_session|IsSessionHandle|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_bridge_recording_session_wrong_log_type
    "no matching function|mint_recording_session|cannot bind|could not convert|SessionEventLog")
crucible_neg_compile_fixy_test(neg_fixy_bridge_crashwatched_nothrow
    "CrashWatched_NoThrow_Rejected|crash-watched|NoThrow")
# fixy-HS14-02 — mint_crash_watched_session HS14 floor (≥2 fixtures).
# Sibling fixture exercises the survivor-registry gate via the MINT
# factory call site (distinct mismatch class + distinct entry point
# from _nothrow which goes through the class-template ctor).
crucible_neg_compile_fixy_test(neg_fixy_bridge_crashwatched_unregistered_peer
    "survivor_registry|require_crash_survivors_declared_|mint_permission_inherit survivors|inheritance_list_empty|static assertion failed")

# fixy-HS14-03 — mint_recording_endpoint HS14 floor (≥2 fixtures via
# the fixy::bridge:: re-export).  Two GENUINELY distinct mismatch
# classes — substrate-side equivalent collapsed both into the
# first-param gate; the fixy-side pair separates them:
# (#1) non_endpoint — first-param template deduction fails (passing
#      `int` to `Endpoint<Substr, Dir, Ctx>&&` — no specialization
#      matches), producing "no matching function for call to /
#      could not deduce" against the whole template;
# (#2) wrong_log_type — first-param successfully deduces (a real
#      Endpoint is built via fpipe::mint_endpoint<Channel,
#      Direction::Consumer>(ctx, cons)), THEN the second-param
#      reference type `SessionEventLog&` cannot bind to `int`.  This
#      is the post-deduction reference-binding gate — distinct
#      diagnostic shape from #1.
crucible_neg_compile_fixy_test(neg_fixy_bridge_recording_endpoint_non_endpoint
    "no matching function|mint_recording_endpoint|could not deduce|no matching template|deduction failed")
crucible_neg_compile_fixy_test(neg_fixy_bridge_recording_endpoint_wrong_log_type
    "no matching function|mint_recording_endpoint|cannot bind|could not convert|SessionEventLog")

# fixy-HS14-04 — mint_crash_watched_endpoint HS14 floor (≥2 fixtures
# via the fixy::bridge:: re-export).  Two distinct mismatch classes,
# mirroring the HS14-03 pair structure but on the crash-watched
# wrapper family:
# (#1) non_endpoint — first-param `Endpoint<Substr, Dir, Ctx>&&`
#      template deduction fails when passed `int`; PeerTag is
#      explicit, so substitution still bails out.
# (#2) wrong_flag_type — first-param deduces successfully (a real
#      Endpoint built via fpipe::mint_endpoint), PeerTag has
#      survivor_registry specialization (body static_assert
#      passes), and the failure is the second-parameter
#      `OneShotFlag&` reference binding rejecting `int`.
# Distinct from HS14-02 fixtures: HS14-02 covers the substrate
# session-level mint (mint_crash_watched_session); HS14-04 covers
# the endpoint wrapper mint surface (mint_crash_watched_endpoint).
crucible_neg_compile_fixy_test(neg_fixy_bridge_crashwatched_endpoint_non_endpoint
    "no matching function|mint_crash_watched_endpoint|could not deduce|no matching template|deduction failed")
crucible_neg_compile_fixy_test(neg_fixy_bridge_crashwatched_endpoint_wrong_flag_type
    "no matching function|mint_crash_watched_endpoint|cannot bind|could not convert|OneShotFlag")

# FIXY-U-117b — mint_atomic_session HS14 floor (≥2 fixtures via the
# `fixy::bridge::` re-export at Bridge.h:105 — FIXY-U-117 landing).
# Substrate at bridges/MachineSessionBridge.h:281 in `crucible::safety::`
# namespace (NOT bridges::, despite the file path).  Concept gate is
# `AtomicMachineCell<Cell> && safety::proto::is_well_formed_v<Proto>`
# — two distinct mismatch classes:
# (#1) non_cell — Cell-side concept gate.  Empty struct lacks
#      `state_type` + `load(memory_order)` → AtomicMachineCell<Cell>
#      false → requires-clause unsatisfied → overload removed.
# (#2) ill_formed_proto — Proto-side trait gate.  Empty struct
#      matches no `is_well_formed` specialization → access to
#      ::value is ill-formed → substitution fails → overload removed.
# Each fixture isolates ONE conjunct of the requires-clause (the
# other argument is valid), so a regression that drops a single
# conjunct surfaces in exactly one fixture.
crucible_neg_compile_fixy_test(neg_fixy_bridge_atomic_session_non_cell
    "no matching function|mint_atomic_session|AtomicMachineCell|state_type|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_bridge_atomic_session_ill_formed_proto
    "no matching function|mint_atomic_session|is_well_formed|incomplete type|no type named|constraints not satisfied")

# FIXY-U-120b — fixy::warden:: re-export HS14 floor (≥2 fixtures per
# mint × 4 warden mints; `fixy/Warden.h` landed in FIXY-U-120).
# Each mint's substrate gate is
#   CtxFitsXMint = IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Init>
# decomposing into two orthogonal soundness conjuncts.  We exercise
# BOTH via two fixtures so a regression that drops EITHER half
# (the row-membership check OR the structural IsExecCtx introducer)
# surfaces in exactly one fixture, naming the failing conjunct in
# the diagnostic:
#   (#1 bg_ctx)       — Ctx satisfies IsExecCtx but row lacks Init
#                       → diagnostic names `Init` / row_contains.
#   (#2 not_exec_ctx) — Ctx fails the structural IsExecCtx concept
#                       → diagnostic names `IsExecCtx` / `row_type`.
# Substrate parity: warden_neg/ ships the same pair shape against
# `crucible::warden::mint_X`; THESE fixtures route through the
# `fixy::warden::` using-decl to witness that the wrapper-layer
# name lookup preserves the substrate gate (a regression that
# replaced the using-decl with a free-function shim would leave
# warden_neg/ green while THIS block unexpectedly compiles).
crucible_neg_compile_fixy_test(neg_fixy_warden_hardening_bg_ctx
    "no matching function|mint_hardening|CtxFitsHardeningMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_hardening_not_exec_ctx
    "no matching function|mint_hardening|IsExecCtx|NotAnExecCtx|row_type|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_deadline_watchdog_bg_ctx
    "no matching function|mint_deadline_watchdog|CtxFitsDeadlineWatchdogMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_deadline_watchdog_not_exec_ctx
    "no matching function|mint_deadline_watchdog|IsExecCtx|NotAnExecCtx|row_type|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_hot_region_registry_bg_ctx
    "no matching function|mint_hot_region_registry_handle|CtxFitsHotRegionRegistryMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_hot_region_registry_not_exec_ctx
    "no matching function|mint_hot_region_registry_handle|IsExecCtx|NotAnExecCtx|row_type|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_quarantine_policy_bg_ctx
    "no matching function|mint_quarantine_policy|CtxFitsQuarantineMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_warden_quarantine_policy_not_exec_ctx
    "no matching function|mint_quarantine_policy|IsExecCtx|NotAnExecCtx|row_type|constraints not satisfied")

# FIXY-U-121b — HS14 ≥2 floor for the 8 V1 perf mints surfaced
# through `fixy::perf::` (Perf.h:120-180, FIXY-U-121 landing).  Each
# mint's `CtxFitsXMint = IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Init>`
# gets 2 fixtures targeting DISTINCT conjuncts:
# (#1) bg_ctx       — BgDrainCtx (IsExecCtx ✓, Init capability ✗);
# (#2) not_exec_ctx — struct missing row_type (IsExecCtx ✗).
#
# 16 fixtures total.  Sentinel test_fixy_perf.cpp pins identity +
# concept-resolution + cardinality; these fixtures pin the requires-
# clause FIRES through the using-decl name lookup (a regression that
# replaced the using-decl with a free-function shim would leave
# substrate's `perf_neg/neg_X_mint_bg_ctx.cpp` green while these
# fixtures unexpectedly compiled).  V2 hub deferred per FIXY-U-121.
crucible_neg_compile_fixy_test(neg_fixy_perf_lock_contention_bg_ctx
    "no matching function|mint_lock_contention|CtxFitsLockContentionMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_lock_contention_not_exec_ctx
    "no matching function|mint_lock_contention|CtxFitsLockContentionMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_pmu_sample_bg_ctx
    "no matching function|mint_pmu_sample|CtxFitsPmuSampleMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_pmu_sample_not_exec_ctx
    "no matching function|mint_pmu_sample|CtxFitsPmuSampleMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sched_switch_bg_ctx
    "no matching function|mint_sched_switch|CtxFitsSchedSwitchMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sched_switch_not_exec_ctx
    "no matching function|mint_sched_switch|CtxFitsSchedSwitchMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sched_tp_btf_bg_ctx
    "no matching function|mint_sched_tp_btf|CtxFitsSchedTpBtfMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sched_tp_btf_not_exec_ctx
    "no matching function|mint_sched_tp_btf|CtxFitsSchedTpBtfMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sense_hub_bg_ctx
    "no matching function|mint_sense_hub|CtxFitsSenseHubMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_sense_hub_not_exec_ctx
    "no matching function|mint_sense_hub|CtxFitsSenseHubMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_syscall_latency_bg_ctx
    "no matching function|mint_syscall_latency|CtxFitsSyscallLatencyMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_syscall_latency_not_exec_ctx
    "no matching function|mint_syscall_latency|CtxFitsSyscallLatencyMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_syscall_tp_btf_bg_ctx
    "no matching function|mint_syscall_tp_btf|CtxFitsSyscallTpBtfMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_syscall_tp_btf_not_exec_ctx
    "no matching function|mint_syscall_tp_btf|CtxFitsSyscallTpBtfMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_workload_profiler_bg_ctx
    "no matching function|mint_workload_profiler|CtxFitsWorkloadProfilerMint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_workload_profiler_not_exec_ctx
    "no matching function|mint_workload_profiler|CtxFitsWorkloadProfilerMint|IsExecCtx|NotAnExecCtx|constraints not satisfied")

# FIXY-U-122 — `fixy::perf::v2::mint_sense_hub_v2` HS14 floor (2
# fixtures, BgDrainCtx + NotAnExecCtx) routed through the sibling
# sub-umbrella `<crucible/fixy/perf/V2.h>`.  Closes the final
# `[✗ NO-FIXY]` entry in mint-inventory.md by surfacing the V2 mint
# under fixy:: without violating the V1/V2 alternative-build
# contract.
crucible_neg_compile_fixy_test(neg_fixy_perf_v2_sense_hub_v2_bg_ctx
    "no matching function|mint_sense_hub_v2|CtxFitsSenseHubV2Mint|Init|row_contains|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perf_v2_sense_hub_v2_not_exec_ctx
    "no matching function|mint_sense_hub_v2|CtxFitsSenseHubV2Mint|IsExecCtx|NotAnExecCtx|constraints not satisfied")

# fixy-HS14-06 — per-role federation mints HS14 floor (≥2 fixtures
# each across mint_sender / mint_receiver / mint_coord, all via the
# `fixy::sess::` re-export — `using federation::mint_{sender,
# receiver,coord};` at Sess.h:285-287).
#
# Each mint has the same 3-arg shape (ctx, endpoint, admittance) and
# the same requires-clause `CtxFitsFederation<Ctx>`.  The HS14 floor
# requires per-mint coverage of TWO distinct mismatch classes:
# (#1) non_ctx     — `int` fails the IsExecCtx prerequisite that
#                    CtxFitsFederation transitively requires;
# (#2) no_row_fg   — `HotFgCtx` IS IsExecCtx but its `Row<>` lacks
#                    the IO+Block atoms federation demands, so the
#                    row-subset check fires.
#
# 6 fixtures total — distinct from the 4-arg mint_federation_channel
# fixtures (HS14-05) which exercise the combined sender+receiver
# forwarder.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_non_ctx
    "CtxFitsFederation|IsExecCtx|constraints not satisfied|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_no_row_fg
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|row_subset|federation_required_row|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_non_ctx
    "CtxFitsFederation|IsExecCtx|constraints not satisfied|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_no_row_fg
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|row_subset|federation_required_row|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_non_ctx
    "CtxFitsFederation|IsExecCtx|constraints not satisfied|mint_coord")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_no_row_fg
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|row_subset|federation_required_row|mint_coord")

# fixy-HS14-06 super-set — third mismatch class per mint (wrong
# admittance template-class binding).  IsExecCtx + row-subset both
# satisfied via TestRunnerCtx (Row<Test, Alloc, IO, Block> ⊃
# federation_required_row).  Failure is the third parameter:
# `SharedPermission<FederatedPeer<Org>>` cannot bind to `int`.
# Pushes HS14-06 from "floor" (2 per mint) to "100%+" (3 per
# mint), witnessing per-role mint signature preservation through
# the using-decl across ALL THREE parameter slots independently.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_wrong_admittance
    "no matching function|cannot convert|SharedPermission|FederatedPeer|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_wrong_admittance
    "no matching function|cannot convert|SharedPermission|FederatedPeer|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_wrong_admittance
    "no matching function|cannot convert|SharedPermission|FederatedPeer|mint_coord")

# ── FIXY-AUDIT-D6 — mint_vigil_mode_bridge HS14 floor.
#
# `mint_vigil_mode_bridge(const Vigil&)` is a parameter-typed token
# mint — the gate is the `const Vigil&` parameter slot.  Two
# rejection classes:
#   _non_vigil  — unrelated user struct fails reference binding
#   _nullptr    — nullptr_t cannot bind to class-typed reference
crucible_neg_compile_fixy_test(neg_fixy_bridge_vigil_mode_non_vigil
    "mint_vigil_mode_bridge|invalid initialization|no matching function|cannot bind|could not convert|invalid conversion")
crucible_neg_compile_fixy_test(neg_fixy_bridge_vigil_mode_nullptr
    "mint_vigil_mode_bridge|invalid initialization|no matching function|cannot bind|could not convert|invalid conversion")

# ── FIXY-MINT-Substr — fixy/Substr.h re-export of per-substrate
# session minters (SPSC, SWMR, ChaseLev, MetaLog, ChainEdge, MPMC,
# CalendarGrid, ShardedCalendarGrid, ShardedGrid).  Two HS14
# fixtures pin the producer/consumer + writer/reader role gates:
crucible_neg_compile_fixy_test(neg_fixy_substr_spsc_wrong_handle
    "cannot convert|no matching function|ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")

# fixy-A4-022 — Substr.h promotes PermissionedSnapshot from the
# `concurrent::` sub-namespace into its own `snapshot::` sub-namespace
# with a typed-session surface (SnapshotSession.h).  Two HS14 fixtures
# pin distinct mismatch classes: role swap (writer-mint receives
# reader handle) and concept rejection (substrate fails
# SnapshotSessionSurface).
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_unfit_snap
    "constraints not satisfied|SnapshotSessionSurface|no matching function")

# FIXY-U-074h HS14 floor: mint_snapshot_reader_session was HS14:0 (the
# writer_session variant is covered by snapshot_wrong_handle).  Role-swap
# (WriterHandle->reader_session) + non-ExecCtx → HS14:2.  Closes the last
# ctx-bound substr session mint below floor.
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_session_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")

# FIXY-U-074i HS14 floor: the SWMR TOKEN mints (no ctx param, so the
# non-ExecCtx class does not apply).  mint_swmr_writer gates on
# SwmrSessionSurface + an exact-type Permission<writer_tag>&& proof;
# mint_swmr_reader gates on SwmrSessionSurface (1-arg) and takes a
# fractional SharedPermission<reader_tag> proof (2-arg).  Two bespoke
# distinct classes each: wrong-perm-tag vs non-surface (writer);
# non-surface vs exclusive-vs-shared permission mode (reader).
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_wrong_perm_tag
    "cannot convert|no matching function|writer_tag|Permission")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_non_surface
    "SwmrSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_non_surface
    "SwmrSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_exclusive_perm
    "cannot convert|no matching function|SharedPermission|reader_tag")

# FIXY-U-074j HS14 floor: the SNAPSHOT TOKEN mints (no ctx param).
# mint_snapshot_writer gates on SnapshotSessionSurface + an exact-type
# Permission<writer_tag>&& proof → wrong-perm-tag vs non-surface.
# mint_snapshot_reader is single-overload surface-gated (no SharedPermission
# variant, unlike SWMR) → two surface-failure modes: bare non-surface (fails
# first nested-type clause) vs near-miss (reader() returns ReaderHandle not
# optional<ReaderHandle>, failing exactly one method-signature clause).
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_writer_wrong_perm_tag
    "cannot convert|no matching function|writer_tag|Permission")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_writer_non_surface
    "SnapshotSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_non_surface
    "SnapshotSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_near_miss_surface
    "SnapshotSessionSurface|constraints not satisfied|no matching function")

# FIXY-U-074k HS14 floor: the MPMC ENDPOINT token mints (single-arg,
# surface-only gated on MpmcChannelSessionSurface, no permission param).
# Same shape as snapshot_reader → two surface-failure modes each: bare
# non-surface (fails first nested-type clause) vs near-miss (the relevant
# factory — producer()/consumer() — returns the handle directly instead of
# optional<Handle>, failing exactly one method-signature clause).
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_endpoint_non_surface
    "MpmcChannelSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_endpoint_near_miss
    "MpmcChannelSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_endpoint_non_surface
    "MpmcChannelSessionSurface|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_endpoint_near_miss
    "MpmcChannelSessionSurface|constraints not satisfied|no matching function")

# FIXY-U-074l HS14 floor: fixy::safety::mint_view + mint_linear_view.
# These gate via a `pre(view_ok(c, type_identity<Tag>{}))` contract (NOT a
# requires-concept) — view_ok is a per-Carrier hidden friend with no global
# default.  Two distinct classes each: no view_ok at all (ADL miss) vs
# view_ok wired for a different Tag (overload exists, doesn't match).
crucible_neg_compile_fixy_test(neg_fixy_safety_mint_view_no_view_ok
    "view_ok|not declared|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_safety_mint_view_wrong_tag
    "view_ok|no matching function|type_identity")
crucible_neg_compile_fixy_test(neg_fixy_safety_mint_linear_view_no_view_ok
    "view_ok|not declared|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_safety_mint_linear_view_wrong_tag
    "view_ok|no matching function|type_identity")

# FIXY-U-074m HS14 floor: fixy::sess::mint_session_view gates via
# `requires HandleIsAt<Handle, Tag>`.  Probed with static_assert(requires{...})
# — the header's own can_mint_session_view discipline, which avoids
# namespace-scope Send-state handle instances (abandonment-destructor).
# Two distinct classes: SessionHandle at the WRONG position (Send@AtRecv)
# vs a non-SessionHandle type (int).
crucible_neg_compile_fixy_test(neg_fixy_sess_session_view_wrong_position
    "static assertion failed|HandleIsAt|mint_session_view")
crucible_neg_compile_fixy_test(neg_fixy_sess_session_view_non_handle
    "static assertion failed|HandleIsAt|mint_session_view")

# FIXY-U-074n HS14 floor: fixy::source::federation::mint_self_signed_handshake
# gates via `requires FederationOrgTag<Org>` = is_class_v && is_empty_v.
# (Substrate-side equivalents are in test/safety_neg/, which gen-mint-inventory
# does not count toward the fixy umbrella.)  Two distinct classes: non-class
# Org (int → fails is_class_v) vs non-empty class Org (fails is_empty_v).
crucible_neg_compile_fixy_test(neg_fixy_source_mint_self_signed_non_class_org
    "constraints not satisfied|FederationOrgTag|no matching function|is_class")
crucible_neg_compile_fixy_test(neg_fixy_source_mint_self_signed_non_empty_org
    "constraints not satisfied|FederationOrgTag|no matching function|is_empty")

# FIXY-U-074o HS14 floor: fixy::pipe::mint_stage_from_endpoints — the last
# fixy-re-exported mint below floor (4-conjunct CtxFitsStageFromEndpoints
# gate + IsExecCtx Ctx template constraint).  (Substrate equivalents in
# test/effects_neg/ don't count toward the fixy umbrella.)  Two orthogonal
# classes: non-ExecCtx ctx (ctx axis, valid endpoints) vs swapped endpoint
# directions (endpoint-shape axis, valid ctx).
crucible_neg_compile_fixy_test(neg_fixy_pipe_stage_from_endpoints_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_pipe_stage_from_endpoints_swapped_direction
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|IsConsumerEndpoint|IsProducerEndpoint")

# FIXY-U-074d HS14 floor: fixy::substr::mint_substrate_session sat at
# HS14:0.  Two distinct rejection conjuncts of its requires-clause
# (IsBridgeableDirection ∧ SubstrateFitsCtxResidency ∧
# CtxFitsPermissionedProtocol ∧ IsExecCtx Ctx): non-bridgeable direction
# (Snapshot+Producer) and non-ExecCtx first argument.  Brings it to 2.
crucible_neg_compile_fixy_test(neg_fixy_substr_substrate_session_non_bridgeable
    "IsBridgeableDirection|constraints not satisfied|no matching function|handle_for")
crucible_neg_compile_fixy_test(neg_fixy_substr_substrate_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")

# FIXY-U-074e HS14 floor: SWMR session mints.  mint_writer_session sat
# at HS14:1 (only swmr_wrong_handle, role swap); mint_reader_session at
# HS14:0.  Two distinct classes per ctx-bound mint — role mismatch
# (wrong handle type) and non-ExecCtx first arg — bring both to 2.
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_session_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")

# FIXY-U-074f HS14 floor: SWMR runtime-session mints (writer/reader),
# both HS14:0.  Same handle types + IsExecCtx constraint as the typed
# session variants (SwmrSession.h:241/250) → same two distinct classes
# (role-swap wrong-handle + non-ExecCtx) bring both to 2.
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_runtime_session_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_runtime_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_runtime_session_wrong_handle
    "cannot convert|no matching function|WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_runtime_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")

# FIXY-U-074g HS14 floor: MPMC session mints (producer/consumer), both
# HS14:0.  ctx-bound, take Channel::{Producer,Consumer}Handle&
# (MpmcChannelSession.h:271/281) — same two distinct classes
# (role-swap wrong-handle + non-ExecCtx) bring both to 2.
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_session_wrong_handle
    "cannot convert|no matching function|ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_session_wrong_handle
    "cannot convert|no matching function|ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function")

# fixy-HS14-07 — Substr.h re-export of ChaseLevDeque mints
# (mint_chaselev_owner / mint_chaselev_thief / mint_owner_session /
# mint_thief_session).  Four mints × two distinct mismatch classes
# each = 8 fixtures.  The pairs split:
#   _non_surface  / _wrong_perm  — owner token mint
#   _non_surface  / _wrong_proof — thief token mint
#   _non_ctx      / _wrong_handle — owner ctx-bound session mint
#   _non_ctx      / _wrong_handle — thief ctx-bound session mint
#
# Witnesses the using-decl preserves the per-mint requires-clause
# AND the per-parameter binding shape independently across all
# four ChaseLev mints.
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_non_surface
    "constraints not satisfied|ChaseLevSessionSurface|no matching function|mint_chaselev_owner")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_wrong_perm
    "no matching function|cannot convert|Permission|mint_chaselev_owner")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_non_surface
    "constraints not satisfied|ChaseLevSessionSurface|no matching function|mint_chaselev_thief")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_wrong_proof
    "no matching function|cannot convert|SharedPermission|mint_chaselev_thief")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_owner_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_session_wrong_handle
    "no matching function|cannot convert|OwnerHandle|mint_owner_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_thief_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_session_wrong_handle
    "no matching function|cannot convert|ThiefHandle|mint_thief_session")

# fixy-HS14-08 — Substr.h re-export of MetaLog mints
# (mint_metalog_producer / mint_metalog_consumer /
# mint_metalog_producer_session / mint_metalog_consumer_session).
# Four mints × two distinct mismatch classes each = 8 fixtures.
# The pairs split:
#   _non_surface  / _wrong_perm    — producer/consumer token mints
#   _non_ctx      / _wrong_handle  — producer/consumer ctx-bound
#                                    session mints
#
# Witnesses the using-decl preserves the per-mint requires-clause
# AND the per-parameter binding shape independently across all
# four MetaLog mints.
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_producer_non_surface
    "constraints not satisfied|MetaLogSessionSurface|no matching function|mint_metalog_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_producer_wrong_perm
    "no matching function|cannot convert|Permission|mint_metalog_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_consumer_non_surface
    "constraints not satisfied|MetaLogSessionSurface|no matching function|mint_metalog_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_consumer_wrong_perm
    "no matching function|cannot convert|Permission|mint_metalog_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_metalog_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_producer_session_wrong_handle
    "no matching function|cannot convert|ProducerHandle|mint_metalog_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_consumer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_metalog_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_metalog_consumer_session_wrong_handle
    "no matching function|cannot convert|ConsumerHandle|mint_metalog_consumer_session")

# fixy-HS14-09 — Substr.h re-export of ChainEdge mints
# (mint_chainedge_signaler / mint_chainedge_waiter /
# mint_chainedge_signaler_session / mint_chainedge_waiter_session).
# Four mints × two distinct mismatch classes each = 8 fixtures.
# The pairs split:
#   _non_surface  / _wrong_perm    — signaler/waiter token mints
#   _non_ctx      / _wrong_handle  — signaler/waiter ctx-bound
#                                    session mints
#
# Witnesses the using-decl preserves the per-mint requires-clause
# AND the per-parameter binding shape independently across all
# four ChainEdge mints.
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_signaler_non_surface
    "constraints not satisfied|ChainEdgeSessionSurface|no matching function|mint_chainedge_signaler")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_signaler_wrong_perm
    "no matching function|cannot convert|Permission|mint_chainedge_signaler")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_waiter_non_surface
    "constraints not satisfied|ChainEdgeSessionSurface|no matching function|mint_chainedge_waiter")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_waiter_wrong_perm
    "no matching function|cannot convert|Permission|mint_chainedge_waiter")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_signaler_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_chainedge_signaler_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_signaler_session_wrong_handle
    "no matching function|cannot convert|SignalerHandle|mint_chainedge_signaler_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_waiter_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_chainedge_waiter_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chainedge_waiter_session_wrong_handle
    "no matching function|cannot convert|WaiterHandle|mint_chainedge_waiter_session")

# fixy-HS14-10 — Substr.h re-export of CalendarGrid mints
# (mint_calendar_grid_producer / mint_calendar_grid_consumer /
# mint_producer_session / mint_consumer_session).  Four mints ×
# two distinct mismatch classes each = 8 fixtures.  The pairs
# split:
#   _non_surface  / _wrong_perm    — producer/consumer token mints
#   _non_ctx      / _wrong_handle  — producer/consumer ctx-bound
#                                    session mints
#
# Note: producer side carries a non-deducible producer-row
# index `P` (std::size_t); consumer side does not.  Both
# variants are witnessed.
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_non_surface
    "constraints not satisfied|CalendarGridSessionSurface|no matching function|mint_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_wrong_perm
    "no matching function|cannot convert|Permission|mint_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_non_surface
    "constraints not satisfied|CalendarGridSessionSurface|no matching function|mint_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_wrong_perm
    "no matching function|cannot convert|Permission|mint_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_session_wrong_handle
    "no matching function|cannot convert|ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_session_wrong_handle
    "no matching function|cannot convert|ConsumerHandle|mint_consumer_session")

# fixy-HS14-11 — Substr.h re-export of ShardedCalendarGrid mints
# (mint_sharded_calendar_grid_producer /
# mint_sharded_calendar_grid_consumer / mint_producer_session /
# mint_consumer_session).  Four mints × two distinct mismatch
# classes each = 8 fixtures.  The pairs split:
#   _non_surface  / _wrong_perm    — producer/consumer token mints
#   _non_ctx      / _wrong_handle  — producer/consumer ctx-bound
#                                    session mints
#
# Note: BOTH producer and consumer sides carry a non-deducible
# shard-index `S` (std::size_t) — distinct from CalendarGrid where
# only the producer carries `P`.  Both variants are witnessed at
# the sharded mints to prove the using-decl preserves the
# non-deducible-parameter discipline on both sides.
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_non_surface
    "constraints not satisfied|ShardedCalendarGridSessionSurface|no matching function|mint_sharded_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_wrong_perm
    "no matching function|cannot convert|Permission|mint_sharded_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_non_surface
    "constraints not satisfied|ShardedCalendarGridSessionSurface|no matching function|mint_sharded_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_wrong_perm
    "no matching function|cannot convert|Permission|mint_sharded_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_session_wrong_handle
    "no matching function|cannot convert|ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_session_wrong_handle
    "no matching function|cannot convert|ConsumerHandle|mint_consumer_session")

# fixy-HS14-12 — Substr.h re-export of ShardedGrid mints
# (mint_sharded_grid_producer / mint_sharded_grid_consumer /
# mint_producer_session / mint_consumer_session).  Four mints ×
# two distinct mismatch classes each = 8 fixtures.  The pairs
# split:
#   _non_surface  / _wrong_perm    — producer/consumer token mints
#   _non_ctx      / _wrong_handle  — producer/consumer ctx-bound
#                                    session mints
#
# Note: BOTH producer and consumer sides carry a non-deducible
# shard index (`I` producer / `J` consumer).  Grid stub uses
# PermissionedShardedGrid<int, 2, 3, 8, UserTag> with the
# default RoundRobinRouting policy — the routing axis is
# orthogonal to the mint surface and the 4-mint × 2-class shape
# is unaffected by the routing template parameter.
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_non_surface
    "constraints not satisfied|ShardedGridSessionSurface|no matching function|mint_sharded_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_wrong_perm
    "no matching function|cannot convert|Permission|mint_sharded_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_non_surface
    "constraints not satisfied|ShardedGridSessionSurface|no matching function|mint_sharded_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_wrong_perm
    "no matching function|cannot convert|Permission|mint_sharded_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_session_wrong_handle
    "no matching function|cannot convert|ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_session_wrong_handle
    "no matching function|cannot convert|ConsumerHandle|mint_consumer_session")

# fixy-M-20 — fixy::substr::mpsc:: namespace shipped (closes the
# discipline-inconsistent gap where PermissionedMpscChannel had a
# working SubstrateSessionBridge default_proto_for<> specialization
# but no per-substrate sub-namespace mirroring spsc / swmr / mpmc).
# HS14 floor: ≥2 distinct-mismatch fixtures.  Fixture #1 trips the
# IsExecCtx prerequisite on `mint_mpsc_producer_session`'s ctx slot;
# fixture #2 trips the ConsumerHandle reference binding on
# `mint_mpsc_consumer_session`'s second slot AFTER IsExecCtx passes.
crucible_neg_compile_fixy_test(neg_fixy_substr_mpsc_producer_session_non_ctx
    "IsExecCtx|constraints not satisfied|no matching function|mint_mpsc_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpsc_consumer_session_wrong_handle
    "no matching function|cannot convert|cannot bind|ConsumerHandle|mint_mpsc_consumer_session")

# fixy-A4-030 — substrate-vs-fixy disjointness discriminator witness.
# Reject.h's main guard is the std::index_sequence fold that walks every
# safety::diag::Catalog entry and asserts !is_fixy_diag_v<entry>; these
# fixtures witness the discriminator from the user side on two
# orthogonal Catalog axes (hot-path tier vs Met(X) effect row).  Both
# fixtures invert the discriminator's correct verdict and watch the
# static_assert fire — the fold can't be tripped from outside source
# without forking headers, so the HS14 witness is the discriminator
# itself.
crucible_neg_compile_fixy_test(neg_fixy_reject_hotpath_misclassified
    "static assertion failed|is_fixy_diag|HotPathViolation|fixy-A4-030")
crucible_neg_compile_fixy_test(neg_fixy_reject_effectrow_misclassified
    "static assertion failed|is_fixy_diag|EffectRowMismatch|fixy-A4-030")

# ── FIXY-MINT-Mach + FIXY-MINT-Safety — fixy/Mach.h + fixy/Safety.h
# re-export of state-machine + safety-wrapper token mints
# (mint_machine, transition_to, mint_linear, mint_secret, mint_view).
# Two HS14 fixtures pin the Linear-copy ban and the is_constructible
# gate via fixy:::
crucible_neg_compile_fixy_test(neg_fixy_safety_linear_copy
    "deleted function|move-only|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_mach_construct_unbuildable
    "is_constructible|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_mach_too_many_args
    "is_constructible|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_mach_transition_unspecialized
    "MachineTransition|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_mach_transition_wrong_direction
    "MachineTransition|constraints not satisfied|associated constraints are not satisfied")

# ── FIXY-WRAP — fixy/Wrap.h re-export of value-level safety wrappers
# (Linear / Refined / SealedRefined / Tagged / Secret / Monotonic /
# AppendOnly / Stale / TimeOrdered / SharedPermission + Mutation.h
# derivatives WriteOnce / WriteOnceNonNull / BoundedMonotonic /
# OrderedAppendOnly / AtomicMonotonic).  10 HS14 fixtures pin
# distinct violation classes across the family:
crucible_neg_compile_fixy_test(neg_fixy_wrap_linear_copy
    "deleted function|move-only|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_wrap_refined_predicate_fails
    "contract|not a constant expression|pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_sealedrefined_no_into
    "no member named|has no member|'into'|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_wrap_tagged_mismatch
    "could not convert|cannot convert|no matching function|invalid conversion|cannot bind")
crucible_neg_compile_fixy_test(neg_fixy_wrap_monotonic_backward
    "contract|not a constant expression|pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_writeonce_double_set
    "contract|not a constant expression|pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_writeoncenonnull_nonpointer
    "WriteOnceNonNull_NonPointer_Type|requires a pointer|static assertion")
crucible_neg_compile_fixy_test(neg_fixy_wrap_bounded_monotonic_overflow
    "contract|not a constant expression|pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_appendonly_redundant_writeonce
    "AppendOnly<WriteOnce|is redundant|static assertion")
crucible_neg_compile_fixy_test(neg_fixy_wrap_timeordered_zero_processes
    "TimeOrdered<T, 0>|forbidden|static assertion|N > 0|N >= 1")

# ── FIXY-ALGEBRA / EFF / DIAG / SOURCE HS14 fixtures ────────────────
#
# Phase D surfaces: 2 fixtures per surface area (8 total) pinning
# distinct concept-rejection classes.
#
# Algebra:
#   _non_lattice  — Lattice concept rejects type with no element_type
#   _non_graded   — IsGraded rejects bare types
# Eff:
#   _subrow_missing            — Subrow rejects when atom is absent
#   _capability_wrong_source   — CanMintCap rejects Fg → Block
# Diag:
#   _non_tag              — is_diagnostic_class_v rejects int
#   _diagnostic_non_tag   — Diagnostic<int, ...> requires-clause fires
# Source:
#   _cross_axis_tagged   — Tagged<T, FromUser> != Tagged<T, Sanitized>
#   _trust_vs_source     — source::FromUser != trust::Verified
crucible_neg_compile_fixy_test(neg_fixy_algebra_non_lattice
    "Lattice|element_type|static assertion|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_algebra_non_graded
    "IsGraded|static assertion|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_eff_subrow_missing
    "Subrow|is_subrow|static assertion|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_eff_capability_wrong_source
    "CanMintCap|static assertion|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_diag_non_tag
    "is_diagnostic_class_v|static assertion|tag_base|DiagnosticAccessor_NonTag")
crucible_neg_compile_fixy_test(neg_fixy_diag_diagnostic_non_tag
    "is_diagnostic_class_v|associated constraints are not satisfied|constraints not satisfied|tag_base")
crucible_neg_compile_fixy_test(neg_fixy_source_cross_axis_tagged
    "is_same|static assertion|cross-axis")
crucible_neg_compile_fixy_test(neg_fixy_source_trust_vs_source
    "is_same|static assertion|cross-axis")

# ── FIXY-STRUCT — fixy/Struct.h re-export of structural wrappers
# (Pinned / NonMovable / NotInherited / FinalBy / Checked /
# ConstantTime / Simd / OwnedRegion / Workload).  11 HS14 fixtures
# pin distinct violation classes across the family:
crucible_neg_compile_fixy_test(neg_fixy_struct_pinned_copy
    "deleted function|stable address|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_struct_pinned_move
    "deleted function|stable address|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_struct_nonmovable_copy
    "deleted function|exclusive ownership|use of deleted function")
crucible_neg_compile_fixy_test(neg_fixy_struct_notinherited_non_final
    "NotInherited_Not_Final|not marked|static assertion")
crucible_neg_compile_fixy_test(neg_fixy_struct_finalby_subclass
    "FinalBy_Subclass_Forbidden|private within this context|is private")
crucible_neg_compile_fixy_test(neg_fixy_struct_checked_signed_shift
    "constraints not satisfied|associated constraints|std::integral|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_struct_checked_safe_capacity_overflow
    "Checked_Capacity_Overflow|overflow|static assertion")
crucible_neg_compile_fixy_test(neg_fixy_struct_ct_signed_select
    "constraints not satisfied|associated constraints|unsigned_integral|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_struct_ct_signed_is_zero
    "constraints not satisfied|associated constraints|unsigned_integral|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_struct_simd_detsafe_float
    "constraints not satisfied|associated constraints|DetSafeSimd|std::integral")
crucible_neg_compile_fixy_test(neg_fixy_struct_simd_iota_non_vec
    "value_type|no type named|substitution failure|no matching function|template argument deduction")
crucible_neg_compile_fixy_test(neg_fixy_struct_owned_region_copy
    "deleted function|move-only|use of deleted function|Permission")
crucible_neg_compile_fixy_test(neg_fixy_struct_workload_zero_shards
    "requires N > 0|static assertion|N > 0|N >= 1")

# ── FIXY-IS — fixy/Is.h concept-gate aliases for safety/Is*.h.
# 3 HS14 fixtures pin canonical alias-rejects-unrelated cases:
crucible_neg_compile_fixy_test(neg_fixy_is_linear_rejects_plain
    "constraints not satisfied|associated constraints|IsLinear|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_secret_rejects_plain
    "constraints not satisfied|associated constraints|IsSecret|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_session_handle_rejects_plain
    "constraints not satisfied|associated constraints|IsSessionHandle|no matching function")

# GAPS-047: Delegate<T, K> now bonds recipient-crash propagation into
# the public crash walker.  This fixture pins the unrecovered outbound
# delegated-protocol case at the consteval assertion boundary.
crucible_neg_compile_session_test(neg_delegate_recipient_crash_unrecovered
    "static assertion failed.*CrashBranch_Missing_In_Tree")

# GAPS-049: exact-name delegated-crash rejection fixtures.  These pin
# the public consteval assertion boundary so custom call sites do not
# have to infer the failure from the broader crash-walker diagnostic.
crucible_neg_compile_session_test(neg_delegate_to_unreliable_no_crash_branch
    "DelegatedCrashPropagation_MissingRecovery|recipient is unreliable but carrier K has no crash-recovery branch")

# GAPS-048: Delegate<Stop, K> has no K continuation at the handle
# surface; the operation is explicitly deleted with a framework-owned
# diagnostic.
crucible_neg_compile_session_test(neg_delegate_stop_continuation
    "DelegateStop_NoContinuation")

crucible_neg_compile_session_test(neg_delegate_stop_without_continuation_capability
    "DelegateStop_NoContinuation")

crucible_neg_compile_session_test(neg_delegate_compose_incompatible_crash_classes
    "CrashLattice composition rejects: Abort x NoThrow incompatible")

crucible_neg_compile_session_test(neg_delegate_recipient_crash_no_propagation_metafunction
    "DelegatedCrashPropagation_PrimaryTemplate|primary template fires")

# GAPS-064: CrashWatchedHandle consumes Stop_g<CrashClass C>.
# These fixtures pin the NoThrow rejection and the exact Stop_g grade
# match at the watched crash boundary.
crucible_neg_compile_session_test(neg_crashwatched_noThrow_with_unreliable_peer
    "CrashWatched_NoThrow_Rejected")
crucible_neg_compile_session_test(neg_crashwatched_strong_class_recovery_path_missing
    "CrashWatched_StopClass_Mismatch")
crucible_neg_compile_session_test(neg_crashwatched_class_lattice_violation
    "CrashWatched_StopClass_Mismatch")
crucible_neg_compile_session_test(neg_crashwatched_stop_class_mismatch
    "CrashWatched_StopClass_Mismatch")

# fixy-A2-020: HS14 floor for the new `!is_terminal_state<B>` conjunct
# in the is_well_formed<Loop<B>, LoopCtx> specialization at
# include/crucible/sessions/Session.h.  Together these two fixtures
# witness the rejection of `Loop<End>` and `Loop<Stop>` — both
# semantically equivalent to their terminal body, neither containing
# a reachable Continue.
crucible_neg_compile_session_test(neg_loop_body_terminal_end
    "static assertion failed|Protocol_Ill_Formed|is_well_formed")
crucible_neg_compile_session_test(neg_loop_body_terminal_stop
    "static assertion failed|Protocol_Ill_Formed|is_well_formed")

# GAPS-066: NumericalTier Send covariance / Recv contravariance and
# channel-boundary fixtures.  These pin the compile-time recipe-tier
# discipline that feeds MIMIC §41: weak producers cannot satisfy strict
# receivers, bare payloads cannot regain tier evidence, and loop
# composition cannot hide a downgrade.
crucible_neg_compile_session_test(neg_send_weaker_tier_to_recv_stronger
    "NumericalTier_ChannelBoundary_ProducerTierTooWeak")
crucible_neg_compile_session_test(neg_recv_promotes_unauthorized
    "NumericalTier_RecvPromotion_RequiresTightening")
crucible_neg_compile_session_test(neg_session_subtype_tier_mismatch
    "NumericalTier_SendSubtype_ProducerTierTooWeak")
crucible_neg_compile_session_test(neg_payload_unwrap_loses_tier
    "NumericalTier_BarePayload_CannotGainTier")
crucible_neg_compile_session_test(neg_default_recipe_to_strict_recv
    "NumericalTier_DefaultRecipe_CannotSatisfyStrictRecv")
crucible_neg_compile_session_test(neg_tier_compose_in_loop
    "NumericalTier_LoopBody_TierDowngrade")

# GAPS-067: VendorCtx pins PermissionedSessionHandle chains to a
# VendorLattice backend.  NV and AMD are mutually-incomparable, so the
# framework-owned composition assertion rejects an NV provider for an
# AMD consumer.
crucible_neg_compile_session_test(neg_vendor_ctx_nv_to_amd
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_vendor_ctx_none_to_nv
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")

# GAPS-068: VendorCtx/VendorPinned admission at session mint,
# substrate bridge, Delegate_seq, subtype, and payload boundaries.
crucible_neg_compile_session_test(neg_mint_substrate_nv_to_amd
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_cntp_layer_vendor_mismatch
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_collective_session_vendor_unspecified
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_session_subtype_vendor_violation
    "SessionSubtype\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_session_compose_cross_vendor_no_portable
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_session_promote_to_portable_unauthorized
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_send_payload_vendor_in_pure_session
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_recv_payload_unauthorized_vendor
    "ProtocolVendorAdmittedByLoopCtx")

# GAPS-070: ProductLattice protocol-grade filter on synchronous
# subtyping.  Each fixture installs a deliberately-too-permissive
# local is_subsort axiom; the structural Gay-Hole relation would pass,
# but the orthogonal grade filter must still reject.
crucible_neg_compile_session_test(neg_session_grade_vendor_axis
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numerical_axis
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_cipher_axis
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_epoch_axis
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numa_axis
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_vendor_numerical_axes
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_vendor_cipher_axes
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numerical_cipher_axes
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_evolution_weakens_numerical
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_evolution_changes_vendor
    "ProtocolGradeMismatch")

# GAPS-072: EpochedDelegate ctx-bound minting pins cross-Relay reshard
# handoffs to the current sender epoch/generation while EpochedAccept
# rejects stale recipients.
crucible_neg_compile_session_test(neg_epoched_delegate_stale_epoch_send
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")
crucible_neg_compile_session_test(neg_epoched_delegate_recipient_stale_recv
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleRecipient")
crucible_neg_compile_session_test(neg_epoched_delegate_generation_decrease
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")
crucible_neg_compile_session_test(neg_epoched_delegate_compose_stale_chain
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")

# FOUND-B13 (#602): Met(X) negative-compile witnesses.  Three
# fixtures probing the Subrow / IsEffect concept gates that enforce
# the substitution principle for capability propagation.
crucible_neg_compile_effects_test(neg_extract_nonempty_row
    "associated constraints are not satisfied|row_size_v.*== 0")
crucible_neg_compile_effects_test(neg_weaken_to_disjoint_row
    "associated constraints are not satisfied|Subrow")
crucible_neg_compile_effects_test(neg_lift_unknown_capability
    "associated constraints are not satisfied|IsEffect")

# WRAP-Bits-Integration-2 (#1087): Row → Bits<Effect> projection
# bridge.  IsEffectRow concept gate rejects non-Row inputs at the
# call site — neither bits_from_row<int>() nor row_subsumes_bits<int>
# are admissible.
crucible_neg_compile_effects_test(neg_bits_from_row_non_row
    "associated constraints are not satisfied|constraints not satisfied|no matching|IsEffectRow")
crucible_neg_compile_effects_test(neg_row_subsumes_bits_non_row
    "associated constraints are not satisfied|constraints not satisfied|no matching|IsEffectRow")

# fixy-A3-027 (#1634): ExecCtx Progress axis (8th axis).
# Termination / liveness claim per F* Pure/Tot/Div vocabulary.
# Four fixtures pin the four orthogonal failure modes introduced by
# the new axis: (1) direct instantiation with Heat=Hot+MayDiverge
# fires heat_progress_coherent_v in the class body; (2) the
# WellFormedExecCtxAxes concept rejects non-IsProgressClass arg at
# the trailing slot; (3) with_progress<>()'s requires-clause rejects
# non-class arg at the builder call site; (4) the builder-chain trap
# — .with_heat<Hot>() without first setting Progress fires the
# class-body static_assert on the intermediate return type.
crucible_neg_compile_effects_test(neg_exec_ctx_hot_maydiverge
    "static assertion failed|heat_progress_coherent|Heat × Progress|fixy-A3-027")
crucible_neg_compile_effects_test(neg_exec_ctx_progress_non_class
    "constraints not satisfied|WellFormedExecCtxAxes|IsProgressClass|fixy-A3-027")
crucible_neg_compile_effects_test(neg_exec_ctx_with_progress_int
    "associated constraints are not satisfied|IsProgressClass|with_progress|fixy-A3-027")
crucible_neg_compile_effects_test(neg_exec_ctx_builder_hot_default_progress
    "static assertion failed|heat_progress_coherent|Heat × Progress|fixy-A3-027")

# fixy-A3-026 (#1633): signalling-channel workload-hint shapes.
# ProducerOnlyChannel<Bytes, Producers, LatestOnly> and
# ConsumerOnlyChannel<Bytes, Consumers> declare producer-only and
# consumer-only fan shapes (telemetry emitters, audit log readers).
# Four fixtures pin all four orthogonal degenerate parameters at the
# type level — every static_assert in the wrappers is load-bearing,
# no half-broken impl could silently admit Bytes=0 / Producers=0 /
# Consumers=0 shapes.  ChannelBudget already pins the bidirectional
# fan case; these complete the signalling-channel family.
crucible_neg_compile_effects_test(neg_producer_only_channel_zero_bytes
    "static assertion failed|Bytes > 0|ProducerOnlyChannel")
crucible_neg_compile_effects_test(neg_producer_only_channel_zero_producers
    "static assertion failed|Producers > 0|ProducerOnlyChannel")
crucible_neg_compile_effects_test(neg_consumer_only_channel_zero_bytes
    "static assertion failed|Bytes > 0|ConsumerOnlyChannel")
crucible_neg_compile_effects_test(neg_consumer_only_channel_zero_consumers
    "static assertion failed|Consumers > 0|ConsumerOnlyChannel")

# fixy-A3-022 (#1629): EffectMask::from_raw rejects unknown bits.
# The runtime dual of Row<Es...> is now hardened against wire-
# format bit-poisoning — a peer sending bits outside the
# `(1u << effect_count) - 1` valid mask trips CRUCIBLE_PRE at
# consteval (__builtin_trap poisoning the constant expression).
# Two fixtures pin orthogonal bug classes — single-bit poison
# (0x80) catches mask-on-read impls; multi-bit poison (0xC0)
# catches half-broken / parity-based fence impls.
crucible_neg_compile_effects_test(neg_effect_mask_from_raw_single_high_bit
    "non-constant condition|not a constant expression|__builtin_trap|call to non-constexpr function|non-constexpr")
crucible_neg_compile_effects_test(neg_effect_mask_from_raw_high_bits
    "non-constant condition|not a constant expression|__builtin_trap|call to non-constexpr function|non-constexpr")

# FOUND-H03-AUDIT-2 (#838): non-Row R rejection in ComputationGraded.
# Witnesses that effect_row_to_at_t<NotARow> hard-errors at the alias
# instantiation point — the helper's primary template is undefined on
# purpose, so non-Row arguments produce an incomplete-type diagnostic.
crucible_neg_compile_effects_test(neg_computation_graded_non_row
    "incomplete type|incomplete and cannot be|has no member named .type|invalid use of incomplete type")

# FOUND-H04-AUDIT-1: encapsulation witness for the H04 façade migration.
# Proves the cross-specialization friendship is bounded — external
# code reaching for the private impl_ field must hard-fail.  If the
# friendship is ever accidentally widened (or impl_ moved back to
# public), this fixture starts compiling and CI catches the
# regression.
crucible_neg_compile_effects_test(neg_computation_impl_private
    "is private within this context|private member|non-public member|is private")

# ─── P1-AUDIT (#852): ExecCtx soundness invariants ─────────────────
#
# Five fixtures locking the cap-permitted-row coherence rule, the
# per-axis concept gates on builder methods, the Heat × Resid
# cross-axis rule, and the ctx_numa::Pinned<N> non-negative-N guard.
crucible_neg_compile_effects_test(neg_exec_ctx_with_cap_non_axis
    "associated constraints are not satisfied|IsCapType")
crucible_neg_compile_effects_test(neg_exec_ctx_in_row_non_row
    "associated constraints are not satisfied|IsEffectRow")
crucible_neg_compile_effects_test(neg_exec_ctx_cap_row_incoherent
    "associated constraints are not satisfied|WellFormedExecCtxAxes|static assertion failed|cap_permitted_row|Subrow")
crucible_neg_compile_effects_test(neg_exec_ctx_heat_resid_incoherent
    "static assertion failed|heat_resid_coherent|Heat × Resid")
crucible_neg_compile_effects_test(neg_exec_ctx_pinned_negative_node
    "static assertion failed|Pinned.*requires N >= 0|Node >= 0")

# ─── fixy-A3-020 (#1627): WellFormedExecCtxAxes concept hoist ──────
#
# The 7 per-axis recognition static_asserts + the Cap×Row Subrow
# check are hoisted from the class body into a single concept
# applied at the class template's `requires` clause.  A typo like
# `ExecCtx<int>` used to fire 7 cascaded body static_asserts; after
# the hoist it must fail at substitution with ONE diagnostic.  Two
# fixtures pin both endpoints of the conjunction: typo at the
# leading axis (short-circuits at IsCapType) and typo at the
# trailing axis (must still fire after every upstream atom passes).
crucible_neg_compile_effects_test(neg_exec_ctx_int_typo_clean_diag
    "constraints not satisfied|WellFormedExecCtxAxes|IsCapType|fixy-A3-020")
crucible_neg_compile_effects_test(neg_exec_ctx_well_formed_axes_workload_typo
    "constraints not satisfied|WellFormedExecCtxAxes|IsWorkloadHint|fixy-A3-020")

# ─── fixy-A3-004 (#1611): IsExecCtx / IsEffectRow strip cv-ref ─────
#
# Widening is_exec_ctx_v / is_effect_row_v with std::remove_cvref_t
# (so the concepts match the IsLinear / IsTagged / IsRefined family
# convention) must NOT accidentally accept non-Ctx / non-Row types
# just because they carry cv-ref qualifiers.  Two fixtures pin both
# directions of the asymmetry: `int const&` and `int&&` still reject.
crucible_neg_compile_effects_test(neg_is_exec_ctx_non_ctx_cvref_rejected
    "static assertion failed|fixy-A3-004|IsExecCtx|is_exec_ctx")
crucible_neg_compile_effects_test(neg_is_effect_row_non_row_cvref_rejected
    "static assertion failed|fixy-A3-004|IsEffectRow|is_effect_row")

# ─── fixy-A3-005 (#1612): cap-tag default ctors are private ─────────
#
# Bg / Init / Test contexts default ctors are PRIVATE; only the
# matching `mint_*_context(detail::ctx_mint::*_key)` factory can
# fabricate one.  Bg's key is friended only on BackgroundThread (+
# the test-witness); Init's key on Vigil + BackgroundThread (+ the
# test-witness); Test's key only on the test-witness.  An
# unprivileged TU that writes `effects::Bg bg{};` or `effects::Init
# i{};` must fail to compile.  Mirrors the H-25 passkey-via-passkey
# pattern from permissions/PermissionInherit.h.
crucible_neg_compile_effects_test(neg_effects_bg_default_ctor_private
    "private within this context|fixy-A3-005|is private|explicitly marked as private")
crucible_neg_compile_effects_test(neg_effects_init_default_ctor_private
    "private within this context|fixy-A3-005|is private|explicitly marked as private")

# ─── FIXY-U-109: context-mint passkey-gate witnesses ──────────────
#
# The three context mints (mint_bg_context / mint_init_context /
# mint_test_context) are passkey-gated, not requires-gated: their
# soundness gate is the private default ctor on bg_key / init_key /
# test_key (friended only to the runtime-authority classes).  Each
# mint gets two HS14 fixtures exercising the gate from BOTH attack
# surfaces — (forge) name a key but fail its private ctor, and
# (keyless) omit the mandatory key argument.  Unlike the
# neg_effects_*_default_ctor_private fixtures (which construct the
# CONTEXT directly), these genuinely CALL the mint factory.
crucible_neg_compile_effects_test(neg_effects_mint_bg_context_forge_key
    "is private within this context|private member|bg_key")
crucible_neg_compile_effects_test(neg_effects_mint_bg_context_keyless
    "too few arguments|no matching function|mint_bg_context")
# FIXY-V-017: exercises the CanMintBgContext concept's same_as<Key, bg_key>
# clause — pass init_key (the wrong context's passkey).  Distinct from
# forge_key (key-private-ctor) and keyless (missing argument).
crucible_neg_compile_effects_test(neg_effects_mint_bg_context_wrong_key
    "constraints not satisfied|CanMintBgContext|same_as|init_key|bg_key|is private")
crucible_neg_compile_effects_test(neg_effects_mint_init_context_forge_key
    "is private within this context|private member|init_key")
crucible_neg_compile_effects_test(neg_effects_mint_init_context_keyless
    "too few arguments|no matching function|mint_init_context")
# FIXY-V-018: exercises CanMintInitContext<Key> with the wrong passkey.
crucible_neg_compile_effects_test(neg_effects_mint_init_context_wrong_key
    "constraints not satisfied|CanMintInitContext|same_as|bg_key|init_key|is private")
crucible_neg_compile_effects_test(neg_effects_mint_test_context_forge_key
    "is private within this context|private member|test_key")
crucible_neg_compile_effects_test(neg_effects_mint_test_context_keyless
    "too few arguments|no matching function|mint_test_context")

# ─── P1-AUDIT-2 (#853): composition-point witnesses ─────────────────
#
# Four fixtures locking the composition concepts and bridge traits:
# CtxAdmits substitution, IsSubCtx parent-derivation, CtxOwnsCapability
# per-atom check, and the deliberate hole in to_alloc_class_tag_v for
# ctx_alloc::Unbound (no analogue in the lattice enum).
crucible_neg_compile_effects_test(neg_exec_ctx_admits_unfit_row
    "associated constraints are not satisfied|CtxAdmits|Subrow")
crucible_neg_compile_effects_test(neg_exec_ctx_subctx_violation
    "associated constraints are not satisfied|IsSubCtx")
crucible_neg_compile_effects_test(neg_exec_ctx_ctx_owns_capability
    "associated constraints are not satisfied|CtxOwnsCapability|row_contains")
crucible_neg_compile_effects_test(neg_exec_ctx_alloc_unbound_no_bridge
    "incomplete type|incomplete and cannot be|has no member named|invalid use of incomplete type")

# ─── P1-AUDIT-3 (#854): Capability linearity + mint authorization ──
#
# Four fixtures locking the Capability<E, S> proof-token discipline:
# CanMintCap rejects Fg, Init→Block, copy is deleted (linearity),
# default ctor is private.
crucible_neg_compile_effects_test(neg_capability_mint_from_fg
    "associated constraints are not satisfied|CanMintCap")
crucible_neg_compile_effects_test(neg_capability_init_mint_block
    "associated constraints are not satisfied|CanMintCap")
crucible_neg_compile_effects_test(neg_capability_copy_attempt
    "use of deleted function|deleted copy|copy constructor.*deleted")
crucible_neg_compile_effects_test(neg_capability_default_construct
    "no matching function|matching function for call to|cap_mint_key|candidate expects 1 argument|is private")

# ─── P1-AUDIT-4 (#855): ExecCtx-driven Capability composition ──────
#
# Three fixtures locking the cross-layer composition gates:
# CtxCanMint rejects Fg-driven minting; CapMatchesCtx rejects caps
# whose effect isn't in the consumer's row; extract_bare rejects
# thread-effect atoms (no value-level cap::* analogue).
crucible_neg_compile_effects_test(neg_capability_mint_from_fg_ctx
    "associated constraints are not satisfied|CtxCanMint")
crucible_neg_compile_effects_test(neg_capability_cap_matches_ctx
    "associated constraints are not satisfied|CapMatchesCtx")
crucible_neg_compile_effects_test(neg_capability_extract_bare_thread_atom
    "no matching function for call|constraints not satisfied|extract_bare")

# ─── fixy-A3-012 (#1619): Capability::consume() rvalue-only pin ────
#
# consume() is non-const-rvalue-ref-qualified by linearity discipline.
# Two fixtures lock the qualifier so a future maintainer cannot drop
# the `&&` (lvalue path) or sneak in a `const &&` overload (const-
# rvalue path) without the static_asserts in Capability.h firing.
crucible_neg_compile_effects_test(neg_capability_consume_lvalue
    "discards qualifiers|ref-qualified|cannot bind|rvalue|no matching")
crucible_neg_compile_effects_test(neg_capability_consume_const_rvalue
    "discards qualifiers|ref-qualified|no matching|binding reference")

# ─── fixy-A3-013 (#1620): cap_mint_key passkey-via-passkey pin ─────
#
# Capability has no default ctor at any access level — only the
# passkey ctor `Capability(cap_mint_key)` exists, and cap_mint_key's
# default ctor is private + friended only to mint_cap.  Two fixtures
# pin both halves of the gate: (1) default-initializing a Capability
# fails because no default ctor exists, (2) attempting to forge a
# cap_mint_key from a non-friended TU fails because its default ctor
# is private.  Together these foreclose every user-side path to a
# fresh Capability that bypasses mint_cap's CanMintCap authorization.
crucible_neg_compile_effects_test(neg_capability_forge_default_ctor
    "no matching function for call|no default constructor|deleted|no matching")
crucible_neg_compile_effects_test(neg_capability_forge_via_passkey
    "is private within this context|private member|private|cap_mint_key")

# ─── fixy-A3-015 (#1622): NSDMI noexcept defense-in-depth pin ──────
#
# Capabilities.h carries a three-layer noexcept pin: cap::* atoms
# (self-test), Bg/Init/Test contexts (friended factory bodies), and
# TestWitness::bg/init/test() (mint-level self-test).  The two fixtures
# below witness the REJECTION side of that gate by defining a
# structurally cap::*-shaped struct with an explicitly throwing default
# ctor and asserting (via the two distinct standard paths — noexcept(T{})
# operator + is_nothrow_default_constructible_v<T> trait) that the
# production pins would fire.  Both pin halves run in lockstep: if
# either standard path silently weakens, only ONE fixture admits and
# the other still fires, surfacing the broken path immediately.
crucible_neg_compile_effects_test(neg_capability_throwing_nsdmi_noexcept_pin
    "static assertion failed|static_assert|fixy-A3-015")
crucible_neg_compile_effects_test(neg_capability_throwing_nsdmi_trait_pin
    "static assertion failed|static_assert|fixy-A3-015")

# ─── fixy-A3-018 (#1625): OsUniverse carrier-width pins ────────────
#
# OsUniverse.h ships TWO pins guarding the uint64_t bitmask carrier:
# (1) cardinality ≤ 64; (2) atom_t underlying type == uint8_t.  Both
# fire at the descriptor site, foreclosing FOUND-I04 append-only
# Universe extension landing a 65th atom or widening atom_t silently.
# The fixtures witness the REJECTION side: a synthetic Universe with
# cardinality = 65 trips (1); a synthetic Universe with uint16_t
# underlying trips (2).  Sister pin to A3-011 (Capabilities.h side).
crucible_neg_compile_effects_test(neg_os_universe_cardinality_overflow
    "static assertion failed|static_assert|OsUniverse_Overflow|fixy-A3-018")
crucible_neg_compile_effects_test(neg_os_universe_atom_underlying_widened
    "static assertion failed|static_assert|OsUniverse_Underlying|fixy-A3-018")

# ─── fixy-A3-019: F*-style value-carrying aliases (FxAliases.h) ────
#
# CLAUDE.md §XVI canonical surface promises Pure<T> / Tot<E,T> /
# Div<E,T> as Progress<Class, DetSafe<Pure, Computation<R, T>>>.
# Two fixtures pin the structural distinctness contract: (1) Pure
# does NOT collapse with a MayDiverge-pinned stack of the same row;
# (2) Pure does NOT collapse with Tot when the row gains IO.
# Without these the F* substitution chain (Pure ⊑ Tot ⊑ Div ⊑ ST ⊑
# All) can be inverted at call sites — exactly the bug class the
# value-carrying aliases are designed to close.
crucible_neg_compile_effects_test(neg_fxalias_pure_collapses_div_terminating
    "static assertion failed|static_assert|fixy-A3-019")
crucible_neg_compile_effects_test(neg_fxalias_tot_collapses_pure_on_io_row
    "static assertion failed|static_assert|fixy-A3-019")

# ─── P1-AUDIT-5 (#856): wrapper-lift + tier-discrimination gates ───
#
# Two fixtures: AllocClassFromCtx rejects Unbound (no wrapper
# analogue); IsL1ResidentCtx rejects non-L1 contexts.
crucible_neg_compile_effects_test(neg_alloc_class_from_unbound_ctx
    "static assertion failed|AllocClassFromCtx|Unbound")
crucible_neg_compile_effects_test(neg_ctx_dispatch_wrong_tier
    "associated constraints are not satisfied|IsL1ResidentCtx")

# ─── GAPS-189 (#1214): effects/Resources.h ResourceTag concept ─────
#
# Two HS14 fixtures locking the ResourceTag soundness gate:
# bare-scalar rejection (no kind/value/name members) and
# bogus-kind rejection (canonical triple but kind escapes the
# IsResourceKind 0..22 atom range — the FOUND-I04 forgery defense).
crucible_neg_compile_effects_test(neg_resources_concept_rejects_bare_scalar
    "static assertion failed|ResourceTag|constraint|GAPS-189")
crucible_neg_compile_effects_test(neg_resources_concept_rejects_bogus_kind
    "static assertion failed|ResourceTag|IsResourceKind|GAPS-189")

# ─── GAPS-190 (#1215): effects/Concurrent.h ConcurrentlySchedulable ─
#
# Two HS14 fixtures locking the dual concept gates:
# * overflow rejection — pairwise sum on a ResourceKind wraps
#   uint64_t (HbmBytes<UINT64_MAX> + HbmBytes<1>).  Witnesses that
#   the no-overflow guard inside ConcurrentlySchedulable's body
#   actually fires.
# * carrier-shape rejection — non-ResourceTag in the
#   ConcurrentRow<...> pack fails the parameter constraint
#   (`template <ResourceTag... Tags>`).  Witnesses that the
#   carrier concept gate rejects up front.
crucible_neg_compile_effects_test(neg_concurrent_row_overflow_rejected
    "static assertion failed|ConcurrentlySchedulable|constraint|GAPS-190")
crucible_neg_compile_effects_test(neg_concurrent_row_non_resource_tag
    "constraint|constraints not satisfied|ResourceTag|ConcurrentRow|GAPS-190")

# ─── GAPS-185 (#1210): cog/CogIdentity.h soundness gates ───────────
#
# Two HS14 fixtures locking the dual gates on CogIdentity:
# * zero-uuid rejection — content_hash's pre clause refuses
#   default-constructed (uuid.is_zero() true) CogIdentity at
#   precondition contract evaluation.  Witnesses the data-invariant
#   gate that prevents KernelCache slot aliasing across uninitialized
#   Cogs.
# * non-compute kind rejection — IsComputeKind<K> concept refuses
#   non-compute CogKind atoms (NicPort, NvSwitch, ...) at template
#   substitution time.  Witnesses the concept gate that prevents
#   future per-Cog Mimic factories from accepting non-compute Cogs
#   as compute targets.
crucible_neg_compile_cog_test(neg_cog_identity_content_hash_zero_uuid
    "static assertion|contract|precondition|content_hash|is_zero|non-constant|GAPS-185")
crucible_neg_compile_cog_test(neg_cog_identity_compute_kind_rejects_nic
    "constraint|constraints not satisfied|IsComputeKind|kernel_count_for|GAPS-185")

# ─── GAPS-186 cog/TargetCaps.h soundness gates (HS14) ──────────────
#
# Two fixtures, two distinct mismatch classes:
# * neg_target_caps_caps_for_rejects_psu_rail — CONCEPT gate.  HasCaps
#   refuses non-substrate CogKind atoms (PsuRail / BmcSensor /
#   aggregates) at template substitution time.  Witnesses the gate
#   that prevents future mint_cog_mimic / FitsCog / OpcodeLatencyTable
#   factories from binding to non-schedulable Cogs.
# * neg_target_caps_warp_size_not_power_of_two — DATA-INVARIANT gate.
#   PowerOfTwoLane Refined refuses non-power-of-two construction at
#   the precondition contract.  Witnesses the gate that prevents
#   GpuTargetCaps::warp_size = 33 from corrupting downstream occupancy
#   / wave-efficiency math.
crucible_neg_compile_cog_test(neg_target_caps_caps_for_rejects_psu_rail
    "constraint|constraints not satisfied|HasCaps|lookup_caps|PsuRail|GAPS-186")
crucible_neg_compile_cog_test(neg_target_caps_warp_size_not_power_of_two
    "static assertion|contract|precondition|power_of_two|non-constant|Refined|PowerOfTwoLane|GAPS-186")

# ─── GAPS-187 OpcodeLatencyTable HS14 fixtures ────────────────────
#
# Two fixtures, distinct mismatch classes:
#   * Concept gate (HasOpcodeTable<K>) — refuses non-substrate Cogs
#     (PsuRail) at template substitution.  Mirrors the GAPS-186 Caps
#     concept gate; double-rejection on HasCaps + opcodes_for-defined.
#   * Data-invariant gate (OrderedLatencyQuantiles) — refuses non-
#     monotone p50/p99/p999 triples at Refined construction.  This is
#     THE structural soundness gate that protects every downstream
#     consumer (Mimic MAP-Elites pruning, partition optimiser, FitsCog
#     throughput envelope) from disk-corrupted / federation-mismatched
#     / hostile-preset percentile measurements.
crucible_neg_compile_cog_test(neg_opcode_latency_rejects_psu_rail
    "constraint|constraints not satisfied|HasOpcodeTable|lookup_opcodes|PsuRail|GAPS-187")
crucible_neg_compile_cog_test(neg_opcode_latency_quantiles_misordered
    "static assertion|contract|precondition|quantile_ordered|non-constant|Refined|OrderedLatencyQuantiles|GAPS-187")

# ─── GAPS-191 cog/FitsCog.h soundness gates (HS14) ────────────────
#
# Two fixtures, two distinct mismatch classes:
# * neg_fits_cog_oversubscribed — QUANTITY gate.  FitsCog refuses a
#   Row whose declared demand exceeds the substrate's compile-time
#   per-axis ceiling.  Witness: 999 SMs > 320 (max GPU SM count any
#   shipped silicon has).  Distinct class: numeric exceedance on a
#   SUBSTRATE-EXPOSED axis.  Without this gate, oversubscribed
#   schedules silently degrade in the runtime scheduler or fail late
#   inside vendor backends.
# * neg_fits_cog_non_substrate — SUBSTRATE-VALIDITY gate.
#   HasCogCapacity refuses non-substrate CogKind atoms (PsuRail /
#   BmcSensor / aggregates) at template substitution.  Distinct
#   class: structural binding failure ("this Cog has no budget axes
#   at all").  Without this gate, future mint_cog_mimic factories
#   would silently bind to PsuRail and produce no-op stubs that
#   drop scheduled work.
crucible_neg_compile_cog_test(neg_fits_cog_oversubscribed
    "constraint|constraints not satisfied|FitsCog|schedule_kernel|row_fits_cog|999|GAPS-191")
crucible_neg_compile_cog_test(neg_fits_cog_non_substrate
    "constraint|constraints not satisfied|FitsCog|HasCogCapacity|schedule_kernel|PsuRail|GAPS-191")

# ─── GAPS-140 cog/NicOffloadAudit.h soundness gates (HS14) ─────────
#
# Two fixtures, two distinct mismatch classes:
# * neg_nic_offload_audit_rejects_gpu — SUBSTRATE-KIND gate.
#   NicOffloadAuditableCog<K> only admits CogKind::NicPort; a GPU has
#   no TSO/GSO/GRO/RSS audit surface.
# * neg_nic_offload_audit_raw_required_offloads — TYPE gate.
#   required_offloads is Bits<NicFeature>, not a raw integer mask, so
#   wrong-universe feature masks cannot silently cross the policy API.
crucible_neg_compile_cog_test(neg_nic_offload_audit_rejects_gpu
    "constraint|constraints not satisfied|NicOffloadAuditableCog|audit_gate|GAPS-140")
crucible_neg_compile_cog_test(neg_nic_offload_audit_raw_required_offloads
    "cannot convert|could not convert|Bits|NicFeature|GAPS-140")

# ─── GAPS-194 cog/NumaNic.h soundness gates (HS14) ─────────────────
#
# Two fixtures, two distinct mismatch classes:
# * neg_numa_nic_rejects_gpu — SUBSTRATE-KIND gate.
#   NumaNicAuditableCog<K> only admits CogKind::NicPort.
# * neg_numa_nic_raw_node — TYPE gate.
#   NUMA nodes are NumaNodeId, not raw integers.
crucible_neg_compile_cog_test(neg_numa_nic_rejects_gpu
    "constraint|constraints not satisfied|NumaNicAuditableCog|audit_gate|GAPS-194")
crucible_neg_compile_cog_test(neg_numa_nic_raw_node
    "cannot convert|could not convert|NumaNodeId|GAPS-194")

# ─── HS14 fixtures for warden::Quarantine (GAPS-118 #1236) ────────
#
#   * neg_quarantine_mint_bg_ctx — only Init-row contexts mint the
#     bounded policy carrier.
#   * neg_quarantine_record_fg_ctx — hot foreground contexts cannot
#     mutate quarantine state from health/probe facts.
#   * neg_quarantine_override_missing_permission — operator override
#     requires the explicit OperatorOverride Permission token.
#   * neg_quarantine_zero_slots — policy state must have bounded slots.
crucible_neg_compile_warden_test(neg_quarantine_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsQuarantineMint|Init|Bg")
crucible_neg_compile_warden_test(neg_quarantine_record_fg_ctx
    "constraint|constraints not satisfied|CtxFitsQuarantineRecord|Bg|HotFgCtx")
crucible_neg_compile_warden_test(neg_quarantine_override_missing_permission
    "no matching function|candidate|operator_override|Permission")
crucible_neg_compile_warden_test(neg_quarantine_zero_slots
    "static assertion failed|QuarantinePolicy requires Cog slots|MaxCogs > 0")

# ─── FIXY-U-084 §XXI Universal Mint Pattern for warden/ hubs ──────
# Three new mints (mint_hardening / mint_hot_region_registry_handle /
# mint_deadline_watchdog) each ship two HS14 neg-compile fixtures
# proving the CtxFits* concept gates fire on missing Init.
crucible_neg_compile_warden_test(neg_hardening_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsHardeningMint|Init|Bg")
crucible_neg_compile_warden_test(neg_hardening_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsHardeningMint|Init|HotFgCtx")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsHotRegionRegistryMint|Init|Bg")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsHotRegionRegistryMint|Init|HotFgCtx")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsDeadlineWatchdogMint|Init|Bg")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsDeadlineWatchdogMint|Init|HotFgCtx")

# ─── FIXY-U-083 §XXI Universal Mint Pattern for perf/ hubs ────────
# Per-perf-hub mint_<hub> (Init-row-gated startup factory) ships HS14
# floor: ≥2 neg-compile fixtures per mint demonstrating BgDrainCtx and
# HotFgCtx are both rejected by the CtxFits<Hub>Mint concept.  Batch 1
# of 2: SenseHub + SenseHubV2 (the two ABI-stable hubs; BPF facade
# hubs ship in batch 2).
crucible_neg_compile_perf_test(neg_sense_hub_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSenseHubMint|Init|Bg")
crucible_neg_compile_perf_test(neg_sense_hub_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSenseHubMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSenseHubV2Mint|Init|Bg")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSenseHubV2Mint|Init|HotFgCtx")

# FIXY-U-083 batch 2: 7 remaining perf/ hubs.  Six load-based BPF
# facade hubs (LockContention / PmuSample / SchedSwitch / SchedTpBtf /
# SyscallLatency / SyscallTpBtf) share the SenseHub-shape gate.
# WorkloadProfiler is ctor-based but rides the same Init-row Ctx-fit
# concept.  Two fixtures per hub (BgDrainCtx + HotFgCtx) = 14 total.
crucible_neg_compile_perf_test(neg_lock_contention_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsLockContentionMint|Init|Bg")
crucible_neg_compile_perf_test(neg_lock_contention_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsLockContentionMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsPmuSampleMint|Init|Bg")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsPmuSampleMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_sched_switch_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSchedSwitchMint|Init|Bg")
crucible_neg_compile_perf_test(neg_sched_switch_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSchedSwitchMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSchedTpBtfMint|Init|Bg")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSchedTpBtfMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSyscallLatencyMint|Init|Bg")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSyscallLatencyMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsSyscallTpBtfMint|Init|Bg")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsSyscallTpBtfMint|Init|HotFgCtx")
crucible_neg_compile_perf_test(neg_workload_profiler_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsWorkloadProfilerMint|Init|Bg")
crucible_neg_compile_perf_test(neg_workload_profiler_mint_hot_fg_ctx
    "constraint|constraints not satisfied|CtxFitsWorkloadProfilerMint|Init|HotFgCtx")

# 3 strengthening fixtures: SECOND mismatch class per mint = non-IsExecCtx.
# CtxFitsXMint = IsExecCtx<Ctx> ∧ row_contains_v<…, Init>; fixtures
# above exercise the row half (different Init-less ExecCtx shapes);
# fixtures below exercise the IsExecCtx half via raw struct rejection.
crucible_neg_compile_warden_test(neg_hardening_mint_not_exec_ctx
    "constraint|constraints not satisfied|CtxFitsHardeningMint|IsExecCtx|NotAnExecCtx")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_not_exec_ctx
    "constraint|constraints not satisfied|CtxFitsHotRegionRegistryMint|IsExecCtx|NotAnExecCtx")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_not_exec_ctx
    "constraint|constraints not satisfied|CtxFitsDeadlineWatchdogMint|IsExecCtx|NotAnExecCtx")

# ─── GAPS-130 cntp/dataplane/Xdp.h soundness gates ────────────────
#
#   * neg_xdp_raw_program_spec — NIC admission requires source::Xdp
#     provenance, not a raw descriptor.
#   * neg_xdp_mint_bg_ctx — only Init-row contexts mint attach plans.
#   * neg_xdp_map_non_pod_value — BPF map values must be verifier-safe POD.
#   * neg_xdp_map_key_without_equality — in-process map images require typed
#     key equality, not bytewise accidental comparison.
crucible_neg_compile_dataplane_test(neg_xdp_raw_program_spec
    "cannot convert|could not convert|DeclaredXdpProgram|Tagged")
crucible_neg_compile_dataplane_test(neg_xdp_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsXdpMint|Init|Bg")
crucible_neg_compile_dataplane_test(neg_xdp_map_non_pod_value
    "constraint|constraints not satisfied|BpfScalar|string")
crucible_neg_compile_dataplane_test(neg_xdp_map_key_without_equality
    "constraint|constraints not satisfied|BpfKey|operator==")

# ─── GAPS-193 cntp/dataplane/TcEbpf.h soundness gates ─────────────
#
#   * neg_tc_ebpf_raw_program_spec — NIC admission requires source::TcEbpf.
#   * neg_tc_ebpf_mint_bg_ctx — only Init-row contexts mint attach plans.
#   * neg_tc_ebpf_raw_flow_class — map values carry tagged flow classes.
#   * neg_tc_ebpf_zero_flow_map — zero-capacity maps are rejected.
crucible_neg_compile_dataplane_test(neg_tc_ebpf_raw_program_spec
    "cannot convert|could not convert|DeclaredTcProgram|Tagged")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsTcMint|Init|Bg")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_raw_flow_class
    "cannot convert|could not convert|DeclaredTcFlowClass|Tagged")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_zero_flow_map
    "constraint|constraints not satisfied|MaxFlows")

# ─── HS14 fixtures #1 + #2 for mimic::CogMimic (GAPS-188 #1213) ──
#
# Two fixtures per the HS14 mandate, each demonstrating a distinct
# mismatch class:
#
#   * #1 SUBSTRATE-FAMILY gate — IsMimicSubstrate refuses Power /
#     Sensor / Container CogKind atoms (PsuRail / BmcSensor /
#     Datacenter / ...) at the CtxFitsCogMimic requires-clause.
#     Without this gate, a future GAPS-196 calibrate pass or
#     GAPS-810 partition optimiser would silently bind a Mimic
#     instance to a non-substrate Cog and produce either a deep
#     substitution failure or a stub bound to a Cog with no
#     emit-shape.
#
#   * #2 CTX-ROW gate — CtxFitsCogMimic refuses contexts whose
#     effect row carries neither Effect::Init nor Effect::Bg.
#     Without this gate, a Test-context worker (or a foreground
#     hot-path call site) could mint a CogMimic instance bound to
#     a transient arena, producing a dangling identity pointer at
#     teardown.
crucible_neg_compile_cog_test(neg_cog_mimic_non_substrate
    "constraint|constraints not satisfied|CogMimic|IsMimicSubstrate|CtxFitsCogMimic|PsuRail|GAPS-188")
crucible_neg_compile_cog_test(neg_cog_mimic_ctx_row_missing
    "constraint|constraints not satisfied|CogMimic|CtxFitsCogMimic|row_subset|Init|Bg|Test|GAPS-188")

# ─── HS14 fixtures #1 + #2 for topology::TopologyGraph (GAPS-110 #1209) ──
#
# Two fixtures per the HS14 mandate, each a distinct mismatch class:
#
#   * #1 IsExecCtx CONJUNCT — CtxFitsTopologyGraph refuses a bare
#     `int` (or any non-ExecCtx type) at the first conjunct.  Without
#     this gate, mint_topology_graph(0, ...) would either deduce
#     Ctx=int and produce a deep-template error inside the body, or
#     silently construct a graph with a garbage row tag, polluting
#     federation row_hash slots.
#
#   * #2 ROW-CONTAINS-INIT CONJUNCT — CtxFitsTopologyGraph refuses
#     contexts whose effect row carries neither Init nor (in this
#     immutable carrier's case) ANY non-Init effect.  The Test-row
#     ctx fixture witnesses rejection.  Without this gate, a unit-test
#     mock could mint a TopologyGraph with a Test-tagged row, polluting
#     any cross-test federation cache slot.
crucible_neg_compile_topology_test(neg_topology_graph_non_ctx_first_arg
    "constraint|constraints not satisfied|CtxFitsTopologyGraph|IsExecCtx|GAPS-110")
crucible_neg_compile_topology_test(neg_topology_graph_test_ctx_refused
    "constraint|constraints not satisfied|CtxFitsTopologyGraph|row_contains|Init|Test|GAPS-110")

# ─── HS14 fixtures for topology::Discovery (GAPS-111 #1217) ─────────
#
#   * neg_topology_discovery_mint_test_ctx — structural discovery snapshots
#     are Init-owned; test-only rows cannot mint production topology storage.
#   * neg_topology_discovery_raw_lspci_text — parser boundaries consume
#     Tagged<source::External> text, not raw string views.
#   * neg_topology_discovery_zero_nodes — snapshot shape requires at least
#     one node and one edge slot.
crucible_neg_compile_topology_test(neg_topology_discovery_mint_test_ctx
    "constraint|constraints not satisfied|CtxFitsDiscoveryInit|Init|Test|GAPS-111")
crucible_neg_compile_topology_test(neg_topology_discovery_raw_lspci_text
    "could not convert|no matching function|Tagged|External|GAPS-111")
crucible_neg_compile_topology_test(neg_topology_discovery_zero_nodes
    "constraint|constraints not satisfied|DiscoveryShape|GAPS-111")

# ─── HS14 fixtures for topology::Health (GAPS-113 #1219) ───────────
#
#   * neg_topology_health_mint_bg_ctx — only Init-row contexts mint the
#     bounded health substrate; Bg may update supplied facts but may not
#     create the policy carrier.
#   * neg_topology_health_update_fg_ctx — hot foreground contexts cannot
#     mutate heartbeat/thermal/ECC/drop/wear state.
#   * neg_topology_health_raw_score — quarantine thresholds consume the
#     HealthScore strong type, not raw integers.
crucible_neg_compile_topology_test(neg_topology_health_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsHealthMint|Init|Bg")
crucible_neg_compile_topology_test(neg_topology_health_update_fg_ctx
    "constraint|constraints not satisfied|CtxFitsHealthUpdate|Bg|HotFgCtx")
crucible_neg_compile_topology_test(neg_topology_health_raw_score
    "could not convert|invalid conversion|HealthScore")

# ─── HS14 fixtures for topology::Telemetry (GAPS-112 #1218) ────────
#
#   * neg_topology_telemetry_mint_bg_ctx — history storage is Init-owned.
#   * neg_topology_telemetry_raw_netdev_text — parser ingress consumes
#     Tagged<source::External>, not raw strings.
#   * neg_topology_telemetry_record_fg_ctx — sample publication is Bg-only.
#   * neg_topology_telemetry_window_overflow — bounded history indices cannot
#     silently wrap their uint16_t carrier.
crucible_neg_compile_topology_test(neg_topology_telemetry_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsNicTelemetryMint|Init|Bg|GAPS-112")
crucible_neg_compile_topology_test(neg_topology_telemetry_raw_netdev_text
    "could not convert|no matching function|Tagged|External|GAPS-112")
crucible_neg_compile_topology_test(neg_topology_telemetry_record_fg_ctx
    "constraint|constraints not satisfied|CtxFitsNicTelemetryRecord|Bg|GAPS-112")
crucible_neg_compile_topology_test(neg_topology_telemetry_window_overflow
    "static assertion failed|uint16_t|bounded history|GAPS-112")

# ─── HS14 fixtures for topology::AsymmetricFailure (GAPS-127 #1220) ──
#
#   * neg_asymmetric_failure_mint_bg_ctx — only Init-row contexts mint
#     the bounded detector.
#   * neg_asymmetric_failure_record_fg_ctx — hot foreground contexts
#     cannot mutate probe/witness state.
#   * neg_asymmetric_failure_zero_window — a detector must carry a
#     non-empty bounded probe window.
crucible_neg_compile_topology_test(neg_asymmetric_failure_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsAsymmetricFailureMint|Init|Bg")
crucible_neg_compile_topology_test(neg_asymmetric_failure_record_fg_ctx
    "constraint|constraints not satisfied|CtxFitsAsymmetricFailureRecord|Bg|HotFgCtx")
crucible_neg_compile_topology_test(neg_asymmetric_failure_zero_window
    "static assertion failed|non-empty probe window|Window > 0")

# ─── HS14 fixtures for topology::Pingmesh (GAPS-134 #1237) ──────────
#
#   * neg_pingmesh_mint_bg_ctx — only Init-row contexts mint the bounded
#     all-pairs carrier.
#   * neg_pingmesh_record_fg_ctx — HotFgCtx cannot mutate latency
#     histograms; probe workers publish on the Bg row.
#   * neg_pingmesh_raw_measurement — raw probe outcomes cannot bypass
#     source::Pingmesh provenance.
#   * neg_pingmesh_zero_peer_capacity — all-pairs storage must have at
#     least two peers.
crucible_neg_compile_topology_test(neg_pingmesh_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsPingmeshMint|Init|Bg")
crucible_neg_compile_topology_test(neg_pingmesh_record_fg_ctx
    "constraint|constraints not satisfied|CtxFitsPingmeshRecord|Bg|HotFgCtx")
crucible_neg_compile_topology_test(neg_pingmesh_raw_measurement
    "cannot convert|no matching function|could not convert|Tagged|PingmeshMeasurement|Pingmesh")
crucible_neg_compile_topology_test(neg_pingmesh_zero_peer_capacity
    "static assertion failed|requires at least two peers")

# ─── HS14 fixtures for topology::Ptp (GAPS-129 #1221) ───────────────
#
#   * neg_ptp_mint_bg_ctx — only Init-row contexts mint PTP handles.
#   * neg_ptp_record_fg_ctx — HotFgCtx cannot publish PTP samples.
#   * neg_ptp_raw_fd — raw int fds cannot mint PTP handles.
#   * neg_ptp_raw_timestamp — raw nanoseconds cannot substitute for
#     source::Ptp timestamps.
#   * neg_ptp_open_raw_device_index — raw integers cannot name /dev/ptpN.
#   * neg_ptp_enable_raw_socket — raw socket fds cannot program timestamping.
#   * neg_ptp_raw_daemon_report_status — daemon status conversion requires
#     source::Ptp provenance at the external-state boundary.
#   * neg_ptp_daemon_report_fg_ctx — foreground contexts cannot admit
#     external daemon reports.
crucible_neg_compile_topology_test(neg_ptp_mint_bg_ctx
    "constraint|constraints not satisfied|CtxFitsPtpMint|Init|Bg")
crucible_neg_compile_topology_test(neg_ptp_record_fg_ctx
    "constraint|constraints not satisfied|CtxFitsPtpRecord|Bg|HotFgCtx")
crucible_neg_compile_topology_test(neg_ptp_raw_fd
    "cannot convert|no matching function|could not convert|PtpClockFd|int")
crucible_neg_compile_topology_test(neg_ptp_raw_timestamp
    "cannot convert|no matching function|could not convert|PtpTimestampNs|Tagged")
crucible_neg_compile_topology_test(neg_ptp_open_raw_device_index
    "cannot convert|no matching function|could not convert|PtpDeviceIndex|int")
crucible_neg_compile_topology_test(neg_ptp_enable_raw_socket
    "cannot convert|no matching function|could not convert|SocketFd|int")
crucible_neg_compile_topology_test(neg_ptp_raw_daemon_report_status
    "cannot convert|no matching function|could not convert|DeclaredPtpDaemonReport|Tagged")
crucible_neg_compile_topology_test(neg_ptp_daemon_report_fg_ctx
    "constraint|constraints not satisfied|CtxFitsPtpRecord|Bg|HotFgCtx")

# ─── Tier 1 #4 (#857): Substrate metafunction gates ───────────────
#
# Two fixtures: IsSubstrate rejects non-Permissioned* types;
# IsOneToOneSubstrate selective dispatch rejects non-SPSC substrates.
crucible_neg_compile_effects_test(neg_substrate_unrecognized
    "associated constraints are not satisfied|IsSubstrate")
crucible_neg_compile_effects_test(neg_substrate_topology_dispatch
    "associated constraints are not satisfied|IsOneToOneSubstrate")

# ─── Tier 1 cross-tree audit (#858): Substrate ↔ Ctx fit gates ────
#
# Two fixtures: SubstrateFitsCtxResidency rejects L1-claiming ctx
# with 4 MB substrate; same for L2-claiming ctx.  Bound shifts
# detected by either fixture.
crucible_neg_compile_effects_test(neg_substrate_does_not_fit_l1
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|fits_in_tier")
crucible_neg_compile_effects_test(neg_substrate_required_tier_unfit_ctx
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|fits_in_tier")

# ─── Session integration B (#860): SessionMint walker fit-checks ───
#
# Four fixtures that prove the proto_row_admitted_by walker fires on
# each kind of mismatch: too-narrow ctx for Send, unfit Recv payload,
# non-IsExecCtx parameter, CheckpointedSession with unfit rollback
# branch.
crucible_neg_compile_effects_test(neg_session_mint_too_narrow_ctx
    "associated constraints are not satisfied|CtxFitsProtocol")
crucible_neg_compile_effects_test(neg_session_mint_unfit_recv
    "associated constraints are not satisfied|CtxFitsProtocol")
crucible_neg_compile_effects_test(neg_session_mint_non_exec_ctx
    "associated constraints are not satisfied|IsExecCtx|CtxFitsProtocol")
crucible_neg_compile_effects_test(neg_session_mint_checkpointed_unfit_rollback
    "associated constraints are not satisfied|CtxFitsProtocol")

# AUDIT-2 closure (CLAUDE.md §XXI HS14): payload_row transparent-
# unwrap for HotPath<Tier, T> sees through to the inner Computation's
# row.  Without the spec, the walker silently admits Bg-effect
# payloads on HotFgCtx — soundness false-negative.  This fixture
# pins the soundness fix.
crucible_neg_compile_effects_test(neg_session_mint_hotpath_hides_bg
    "associated constraints are not satisfied|CtxFitsProtocol")

# AUDIT-3 closure (CLAUDE.md §XXI HS14): Tier 2 keystone gate
# fixtures.  Endpoint and SubstrateSessionBridge mints are required
# to fire on (a) substrate-residency mismatch, (b) non-bridgeable
# (Substrate, Direction) pair, (c) non-IsExecCtx ctx parameter.
# 5 fixtures pin all three failure classes across both factories.
crucible_neg_compile_effects_test(neg_endpoint_unfit_residency
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|fits_in_tier")
crucible_neg_compile_effects_test(neg_endpoint_non_bridgeable_direction
    "associated constraints are not satisfied|IsBridgeableDirection|CtxFitsEndpointMint|handle_for|incomplete type")
crucible_neg_compile_effects_test(neg_endpoint_non_exec_ctx
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|IsExecCtx")
crucible_neg_compile_effects_test(neg_endpoint_topology_recommendation_mismatch
    "associated constraints are not satisfied|SubstrateMatchesEndpointRecommendation|recommend_topology_for_workload|ChannelTopology")
crucible_neg_compile_effects_test(neg_substrate_session_unfit_residency
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|fits_in_tier")
crucible_neg_compile_effects_test(neg_substrate_session_non_bridgeable
    "associated constraints are not satisfied|IsBridgeableDirection|CtxFitsSubstrateSessionMint|CtxFitsShardSubstrateSessionMint|handle_for|incomplete type")

# ─── Tier 3 commit 1 (#863, HS14): mint_stage gate fixtures ──────────
#
# Six fixtures pin the CtxFitsStage<FnPtr, Ctx> conjunction at the
# mint_stage<auto FnPtr>(ctx, in, out) boundary:
#   (a) FnPtr that is not a PipelineStage (wrong arity here) →
#       PipelineStage<FnPtr> conjunct fires.
#   (b) Ctx that is not an ExecCtx (bare int) →
#       IsExecCtx<Ctx> conjunct fires.
#   (c-f) input/output payload row mismatch cases, including
#       Capability<E, S> payload propagation, prove the row_subset
#       admission gate fires for both stage boundaries.
crucible_neg_compile_effects_test(neg_mint_stage_non_pipeline_stage
    "associated constraints are not satisfied|CtxFitsStage|PipelineStage")
crucible_neg_compile_effects_test(neg_mint_stage_non_exec_ctx
    "associated constraints are not satisfied|CtxFitsStage|IsExecCtx")
crucible_neg_compile_effects_test(neg_mint_stage_input_row_mismatch
    "EffectRowMismatch|StageInputRowAdmitted|row_subset|Subrow")
crucible_neg_compile_effects_test(neg_mint_stage_output_row_mismatch
    "EffectRowMismatch|StageOutputRowAdmitted|row_subset|Subrow")
crucible_neg_compile_effects_test(neg_mint_stage_both_rows_mismatch
    "EffectRowMismatch|StageInputRowAdmitted|StageOutputRowAdmitted|row_subset|Subrow")
crucible_neg_compile_effects_test(neg_mint_stage_capability_payload_no_admit
    "EffectRowMismatch|StageInputRowAdmitted|row_subset|Subrow")

# Audit round 2 (#864): three additional fixtures hardening Stage's
# soundness gates beyond the 2-fixture HS14 minimum:
#   (c) handle-type mismatch via the non-deduced typename binding
#       — substitution failure, NOT a concept failure
#   (d) copy attempt — = delete("Stage holds linear Permission tokens
#       via the consumer/producer handles") fires
#   (e) .run() on lvalue Stage — && ref-qualifier fires
crucible_neg_compile_effects_test(neg_mint_stage_handle_type_mismatch
    "invalid initialization of reference|no matching function for call|cannot convert|cannot bind")
crucible_neg_compile_effects_test(neg_stage_copy_attempt
    "use of deleted function|deleted with|holds linear Permission tokens")
crucible_neg_compile_effects_test(neg_stage_run_on_lvalue
    "no matching function for call|cannot bind|passing.*as.*'this'|discards qualifiers")

# ─── Tier 3 commit 2 (#865, HS14): mint_pipeline gate fixtures ───────
#
# Six fixtures pin the CtxFitsPipeline<Ctx, Stages...> conjunction
# at the mint_pipeline boundary:
#   (a) pipeline_chain mismatch — adjacent (i, i+1) stages have
#       incompatible payload types (output_i ≠ input_{i+1}).
#   (b) Pack contains a non-Stage element — IsStage<E> fails for
#       some E in Stages...
#   (c) Ctx is not an IsExecCtx — bare int.
#   (d-f) coordinator row admission failures prove
#       pipeline_row_union_t<Stages...> is checked even when the
#       payload chain itself is well-formed.
crucible_neg_compile_effects_test(neg_mint_pipeline_chain_mismatch
    "associated constraints are not satisfied|CtxFitsPipeline|pipeline_chain")
crucible_neg_compile_effects_test(neg_mint_pipeline_non_stage
    "associated constraints are not satisfied|CtxFitsPipeline|pipeline_chain|IsStage")
crucible_neg_compile_effects_test(neg_mint_pipeline_non_exec_ctx
    "associated constraints are not satisfied|CtxFitsPipeline|IsExecCtx")
crucible_neg_compile_effects_test(neg_mint_pipeline_row_union_exceeds_ctx
    "EffectRowMismatch|pipeline_row_union_t|row_subset|Subrow")
crucible_neg_compile_effects_test(neg_mint_pipeline_chain_payload_compat_ok_row_mismatch
    "EffectRowMismatch|pipeline_row_union_t|row_subset|Subrow")
crucible_neg_compile_effects_test(neg_mint_pipeline_capability_propagation
    "EffectRowMismatch|pipeline_row_union_t|row_subset|Subrow")

# Pipeline.h audit round 2 (#866): three additional fixtures hardening
# Pipeline's soundness gates beyond the 3-fixture concept-gate set:
#   (d) copy attempt — = delete("Pipeline holds move-only Stages, ...")
#       fires
#   (e) .run() on lvalue Pipeline — && ref-qualifier fires
#   (f) empty stages pack — pipeline_chain rejects sizeof...(Stages) == 0
crucible_neg_compile_effects_test(neg_pipeline_copy_attempt
    "use of deleted function|deleted with|holds move-only Stages")
crucible_neg_compile_effects_test(neg_pipeline_run_on_lvalue
    "no matching function for call|cannot bind|passing.*as.*'this'|discards qualifiers")
crucible_neg_compile_effects_test(neg_mint_pipeline_empty_pack
    "associated constraints are not satisfied|CtxFitsPipeline|pipeline_chain")
crucible_neg_compile_effects_test(neg_pipeline_inline_opt_in_missing_ws
    "Pipeline inline opt-in requires")

# FIXY-V-009 HS14 companion — Pipeline's previously-public ctor is now
# private, with mint_pipeline friended.  A direct
# `Pipeline<S1, S2>{s1, s2}` call site bypasses CtxFitsPipeline's row
# admission and is rejected at the access-control check.  Diagnostic
# regex is the stable GCC "is private within this context" form.
crucible_neg_compile_effects_test(neg_pipeline_ctor_public_bypass
    "is private within this context")

# FIXY-V-010 HS14 companion — Stage's previously-public ctor is now
# private, with mint_stage friended.  A direct
# `Stage<&body, Ctx>{ctx, in, out}` call site bypasses CtxFitsStage's
# input + output row admission and is rejected at the access-control
# check.
crucible_neg_compile_effects_test(neg_stage_ctor_public_bypass
    "is private within this context")

# FIXY-V-011 HS14 companion — MpmcStage's previously-public ctor is now
# private, with detail::make_mpmc_stage_from_endpoint_tuple (the helper
# mint_mpmc_stage_from_endpoints forwards to) friended.  A direct
# `MpmcStage<&body, Ctx, Inputs, Outputs>{ctx, inputs, outputs}` call
# site bypasses the variadic row admission check and is rejected at the
# access-control check.
crucible_neg_compile_effects_test(neg_mpmc_stage_ctor_public_bypass
    "is private within this context")

# FIXY-V-012 HS14 companion — PipelineDag's previously-public ctor is
# now private, with mint_pipeline_dag friended.  A direct
# `PipelineDag<Graph>{stages...}` call site bypasses
# CtxFitsPipelineDagMint's row admission and is rejected at the
# access-control check.
crucible_neg_compile_effects_test(neg_pipeline_dag_ctor_public_bypass
    "is private within this context")

# ─── #868: Endpoint↔Stage bridge gate fixtures ───────────────────────
#
# Two fixtures pin the CtxFitsStageFromEndpoints<FnPtr, Ctx, ConsumerEp,
# ProducerEp> conjunction at the mint_stage_from_endpoints boundary:
#   (g) handle-type mismatch — endpoint wraps wrong payload-type handle
#       compared to FnPtr's PipelineStage signature.
#   (h) direction-swapped endpoints — Producer endpoint passed where
#       Consumer expected (and vice versa).
crucible_neg_compile_effects_test(neg_mint_stage_from_endpoints_handle_mismatch
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|StageHandlesMatchEndpoints")
crucible_neg_compile_effects_test(neg_mint_stage_from_endpoints_swapped_direction
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|IsConsumerEndpoint|IsProducerEndpoint")

# AUDIT-ROUND-2 (#868): three additional operational fixtures that
# exercise real-world misuse patterns beyond the HS14 minimum two.
# Each axis distinct from the other two and from (g)/(h) above:
#   (i)  fnptr-wrong-arity — FnPtr is a 3-arg function (not 2-arg);
#        proves the CtxFitsStage conjunct of CtxFitsStageFromEndpoints
#        fires independently of StageHandlesMatchEndpoints.
#   (j)  same-direction — both consumer endpoints (no producer);
#        IsProducerEndpoint conjunct fires on the second-position arg.
#   (k)  raw-handle-bypass — raw ConsumerHandle/ProducerHandle (which
#        satisfy IsConsumerHandle/IsProducerHandle) passed where the
#        bridge expects Endpoint specialization; proves the bridge
#        prevents users from accidentally bypassing mint_endpoint's
#        substrate-fit validation by routing raw handles through.
crucible_neg_compile_effects_test(neg_mint_stage_from_endpoints_fnptr_wrong_arity
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|CtxFitsStage|PipelineStage")
crucible_neg_compile_effects_test(neg_mint_stage_from_endpoints_both_consumers
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|IsProducerEndpoint")
crucible_neg_compile_effects_test(neg_mint_stage_from_endpoints_raw_handle
    "associated constraints are not satisfied|CtxFitsStageFromEndpoints|IsConsumerEndpoint|IsProducerEndpoint")

# GAPS-086: variadic stage mint + PipelineDag graph-shape fixtures.
crucible_neg_compile_effects_test(neg_mpmc_stage_arity_mismatch
    "associated constraints are not satisfied|CtxFitsMpmcStageFromEndpoints|StageHandlesMatchEndpointsExtended")
crucible_neg_compile_effects_test(neg_pipeline_dag_cycle
    "template constraint failure|constraints not satisfied|StageGraphWellFormed")
crucible_neg_compile_effects_test(neg_pipeline_dag_unreachable
    "template constraint failure|constraints not satisfied|StageGraphWellFormed")

# GAPS-004a-AUDIT (#1288): SenseHub::load() takes effects::Init by
# value as its sole parameter.  The cap-typing gate is structural —
# Bg/Init/Test are distinct 1-byte structs with no implicit conversion,
# so a hot-path / background frame holding only Bg cannot reach the
# startup-only loader.  These two fixtures pin the gate at
# compile time as the regression-witness for the SenseHub.h docblock
# claim at lines 234-240.
crucible_neg_compile_effects_test(neg_perf_sense_hub_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_sense_hub_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004b-AUDIT (#1289): SchedSwitch::load() cap-tag gate.  Same
# discipline as SenseHub::load — Init-by-value parameter, no implicit
# conversion from Bg/Test contexts, mandatory argument.
crucible_neg_compile_effects_test(neg_perf_sched_switch_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_sched_switch_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004c (#1279): PmuSample::load() cap-tag gate.  Same Init-by-value
# discipline as SenseHub/SchedSwitch.
crucible_neg_compile_effects_test(neg_perf_pmu_sample_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_pmu_sample_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004d-AUDIT (#1280): LockContention::load() cap-tag gate.  Same
# Init-by-value discipline as SenseHub/SchedSwitch/PmuSample.
crucible_neg_compile_effects_test(neg_perf_lock_contention_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_lock_contention_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004e (#1281): SyscallLatency::load() cap-tag gate.  Same
# Init-by-value discipline as SenseHub/SchedSwitch/PmuSample/LockContention.
crucible_neg_compile_effects_test(neg_perf_syscall_latency_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_syscall_latency_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004y (#1287): Senses::load_all() cap-tag gate.  Same
# Init-by-value discipline as the underlying per-program facades.
crucible_neg_compile_effects_test(neg_perf_senses_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_senses_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004f (#1282): SchedTpBtf::load() + SyscallTpBtf::load() cap-tag
# gates.  Same Init-by-value discipline as the raw tracepoint facades
# they parallel — hot-path frames hold no Init token, so structurally
# can't reach the BTF loaders either.
crucible_neg_compile_effects_test(neg_perf_sched_tp_btf_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_sched_tp_btf_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")
crucible_neg_compile_effects_test(neg_perf_syscall_tp_btf_load_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_syscall_tp_btf_load_no_cap
    "too few arguments|no matching function|candidate expects 1 argument")

# GAPS-004g (#1283): warden::DeadlineWatchdog cap-tag gate.  Construction
# takes effects::Init by value as the third parameter — wrong-cap and
# no-cap fixtures pin the gate against silent default-arg regressions.
# Init-by-value discipline matches the Senses-touching facades upstream.
crucible_neg_compile_effects_test(neg_warden_deadline_watchdog_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_warden_deadline_watchdog_no_cap
    "too few arguments|no matching function|candidate expects 3 arguments")

# GAPS-004h (#1284): perf::WorkloadProfiler cap-tag gate.  Construction
# takes effects::Init by value as the second parameter (third is the
# defaulted Config) — wrong-cap and no-cap fixtures pin the gate against
# silent default-arg regressions.  Hot-path frames hold no Init token,
# so structurally can't construct a profiler from a hot call site.
crucible_neg_compile_effects_test(neg_perf_workload_profiler_wrong_cap
    "could not convert|no matching function|cannot convert|expected.*Init")
crucible_neg_compile_effects_test(neg_perf_workload_profiler_no_cap
    "too few arguments|no matching function|candidate expects 2-3 arguments")

# AUDIT-4 closure (S2.4): Stop terminator coverage.  Pins that the
# SessionMint walker handles BSYZ22 crash-stop's Stop the same way
# as End — vacuously admitted, but the surrounding Send's payload
# row still counts.
crucible_neg_compile_effects_test(neg_session_mint_stop_terminator_unfit
    "associated constraints are not satisfied|CtxFitsProtocol")

# ─── Pattern stability classification (#371, SEPLOG-BUG-7) ──────────
#
# Each crucible_neg_compile_test below pairs a deliberately-broken
# .cpp with a regex matched against the build's diagnostic output.
# The regex falls into one of two stability classes:
#
#   FRAMEWORK-CONTROLLED — the matched text is a literal substring
#       of a `static_assert(..., "crucible::session::diagnostic [...]
#       message")` (or a `= delete("reason")`) in the framework's
#       own source.  Stable across GCC versions because we own the
#       string.  These tests survive toolchain bumps.
#
#   GCC-WRAPPER-TEXT — the matched text comes from the compiler's
#       own diagnostic phrasing ("is private within this context",
#       "no matching function for call to ...").  These tests are
#       at risk of breaking when GCC rephrases the diagnostic.  Each
#       such test is annotated with [GCC-text] in its comment header
#       and the rationale for not converting it.
#
# Most patterns below are FRAMEWORK-CONTROLLED.  Three remain in
# GCC-WRAPPER-TEXT class: neg_direct_construct (private-ctor access
# is intrinsic to the protection), neg_schema_register_with_sealed_view
# / neg_ckernel_register_with_sealed_view / neg_cipher_wrong_tag
# (overload-set rejection in production-side code; converting would
# require invasive refactor of schema/ckernel/cipher overloads).
# Each has an annotated comment below.

# Tier 2 reflection audit — all four fire the same static_assert message.
crucible_neg_compile_test(neg_field_storage
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_vector_storage
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_optional_nested
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_unique_ptr_field
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_c_array_field
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_linear_view_field
  "Type contains a safety::ScopedView<> in some field")
crucible_neg_compile_test(neg_appendonly_over_writeonce
  "AppendOnly<WriteOnce<T>> is redundant")

# FIXY-V-008 HS14 companions for test_row_hash_distinctness — three
# distinct mismatch classes covering each consteval gate.  See the
# fixture header doc-blocks for the per-class premise; collectively
# these witness the wire-format-break ceremony fires for federation
# cache key drift.  Production test is test/test_row_hash_distinctness
# .cpp; substrate gates live in include/crucible/safety/diag/RowHashFold.h.
crucible_neg_compile_test(neg_row_hash_anchor_drift
  "ceremony anchor drift")
crucible_neg_compile_test(neg_row_hash_collision
  "row_hash collision")
crucible_neg_compile_test(neg_row_hash_sentinel
  "EMPTY-slot sentinel")

# fixy-A1-019 HS14 fixtures for safety::mint_read_view<Tag> — rename
# of lend_read per §XXI Universal Mint Pattern.  Two distinct
# mismatch classes:
#   * parameter-shape gate    (non-Permission argument)
#   * tag-identity gate       (explicit Tag clashes with deduced)
crucible_neg_compile_test(neg_mint_read_view_non_permission_arg
  "could not deduce template argument|cannot convert|no matching function")
crucible_neg_compile_test(neg_mint_read_view_explicit_tag_mismatch
  "invalid initialization of reference|could not convert|cannot bind|no matching function")
crucible_neg_compile_test(neg_sealed_refined_into_call
  "has no member named")
crucible_neg_compile_test(neg_graded_concept_void_substrate
  "constraints not satisfied")
crucible_neg_compile_test(neg_graded_concept_no_lattice_type
  "constraints not satisfied")
crucible_neg_compile_test(neg_graded_concept_bare_int
  "constraints not satisfied")

# ─── fixy-A3-014 (#1621): is_graded_specialization_v cv-ref symmetry ──
#
# Strict-identity rule must hold under both directions: the substrate
# trait admits ONLY Graded<M, L, T> (after cv-ref strip).  Lookalike
# structs and cv-ref-qualified non-Graded types stay rejected.  Paired
# with the in-header self-test in algebra/GradedTrait.h that pins the
# positive cv-ref symmetry with IsGraded.
crucible_neg_compile_test(neg_is_graded_specialization_lookalike
  "static assertion failed|static_assert|fixy-A3-014")
crucible_neg_compile_test(neg_is_graded_specialization_cvref_nongraded
  "static assertion failed|static_assert|fixy-A3-014")

# ─── A3-017 lattice name-coverage fold (HS14 pair) ─────────────────
#
# Two fixtures pin the load-bearing umbrella fold-static_assert in
# algebra/lattices/AllLattices.h that rejects shipped lattices without
# a `name()` member.  Companion forms:
#  (1) `in_pack` — proves the fold itself rejects when an unnamed
#      lattice is added to the pack.
#  (2) `sentinel_admits` — proves the HasLatticeName concept rejects
#      sentinel-fallback admission, defending the concept against
#      future drift toward "any string return == named".
crucible_neg_compile_test(neg_lattice_missing_name_in_pack
  "static assertion failed|static_assert|Lattice_Missing_Name|fixy-A3-017")
crucible_neg_compile_test(neg_lattice_name_sentinel_admits
  "static assertion failed|static_assert|fixy-A3-017")

# ─── DimensionTraits Tier-S/V concept gates (Phase 0 P0-3, #1094) ──
#
# Two neg-compile fixtures pinning the Tier concept gates exposed
# by safety/DimensionTraits.h.  Five fixtures — one per concept
# family — pin the gate behavior independently:
#
#   SemiringGrade   — rejects TestLattice (lattice without add/mul)
#   VersionedGrade  — rejects bare int (no element_type, no compatible)
#   LatticeGrade    — rejects bare int (no leq/join/meet primitives)
#   TypestateGrade  — rejects TestLattice (lattice has no state_type/
#                     transition_type — typestate ≠ lattice)
#   FoundationalGrade — rejects void (is_object_v<void> is false)
#
# All five diagnostics surface as "constraints not satisfied"
# pointing at the per-concept evaluation site.  Together they
# complete per-concept-family coverage of DimensionTraits's gate
# surface; a maintainer adding a sixth concept family must ship a
# matching neg-compile fixture or the per-concept rejection
# behavior is unwitnessed.
crucible_neg_compile_test(neg_dimension_traits_semiring_rejects_lattice_only
  "constraints not satisfied")
crucible_neg_compile_test(neg_dimension_traits_versioned_rejects_bare_int
  "constraints not satisfied")
crucible_neg_compile_test(neg_dimension_traits_lattice_rejects_int
  "constraints not satisfied")
crucible_neg_compile_test(neg_dimension_traits_typestate_rejects_lattice
  "constraints not satisfied")
crucible_neg_compile_test(neg_dimension_traits_foundational_rejects_void
  "constraints not satisfied")
crucible_neg_compile_test(neg_dimension_traits_quadruple_tier_mismatch
  "QUADRUPLE_TIER_MISMATCH")

# ─── Fn aggregator type-slot discipline (Phase 0 P0-1, #1095) ──────
#
# Five neg-compile fixtures pinning the FIVE type-slot rejection
# gates on Fn<Type, ...>'s class-template body:
#
#   1. void                — caught by std::is_object_v<Type>
#   2. reference types     — caught by std::is_object_v<Type>
#   3. const-qualified     — caught by !std::is_const_v<Type>
#   4. volatile-qualified  — caught by !std::is_volatile_v<Type>
#   5. C array types       — caught by !std::is_array_v<Type>
#
# Each gate is independent — the audit-round-2 commit added gates
# 3-5 to close footguns the original is_object_v gate did not
# catch (`is_object_v<int[N]>` is true; const T silently deletes
# the wrapper's assignment ops; volatile T defeats the optimizer's
# zero-cost claim).  See Fn.h's "Type constraint" comment block.
#
# HS14 (≥2 fixtures per mint factory) is satisfied 2.5× — Fn's
# mint_fn factory has 5 gates, each pinned independently.  When
# CollisionCatalog (P0-2) ships a 6th class of rejection
# (composition), per-rule fixtures join this set.
crucible_neg_compile_test(neg_fn_rejects_void_type
  "complete object type")
crucible_neg_compile_test(neg_fn_rejects_reference_type
  "complete object type")
crucible_neg_compile_test(neg_fn_rejects_const_type
  "Const-qualifying the value type")
crucible_neg_compile_test(neg_fn_rejects_volatile_type
  "volatile is a hardware-memory annotation")
crucible_neg_compile_test(neg_fn_rejects_array_type
  "C arrays decay to pointers")

# ─── Fn collision catalog (GAPS-005..018, P0-2 #1096) ───────────────
#
# Each fixture instantiates an otherwise well-formed Fn<...> whose
# cross-axis composition violates exactly one fixy.md §24.2 rule.
# The catalog uses marker traits for body-level facts until Fixy IR
# analysis exists; these tests pin the Phase-0 annotation path.
crucible_neg_compile_test(neg_collision_I002_classified_fail_payload
  "I002:")
crucible_neg_compile_test(neg_collision_L002_borrow_async
  "L002:")
crucible_neg_compile_test(neg_collision_E044_ct_async
  "E044:")
crucible_neg_compile_test(neg_collision_I003_ct_fail_secret
  "I003:")
crucible_neg_compile_test(neg_collision_M012_monotonic_concurrent_no_atomic
  "M012:")
crucible_neg_compile_test(neg_collision_P002_ghost_runtime
  "P002:")
crucible_neg_compile_test(neg_collision_I004_classified_async_session_no_ct
  "I004:")
crucible_neg_compile_test(neg_collision_N002_decimal_overflow_wrap
  "N002:")
crucible_neg_compile_test(neg_collision_L003_unscoped_spawn_borrow_capture
  "L003:")
crucible_neg_compile_test(neg_collision_L004_linear_region_lifetime
  "L004:")
crucible_neg_compile_test(neg_collision_M011_linear_fail_no_cleanup
  "M011:")
crucible_neg_compile_test(neg_collision_S010_stale_ct
  "S010:")
crucible_neg_compile_test(neg_collision_S011_capability_replay
  "S011:")

# ─── SwmrSession role and token discipline (GAPS-021) ────────────────
crucible_neg_compile_test(neg_swmr_writer_cannot_recv
  "recv")
crucible_neg_compile_test(neg_swmr_reader_cannot_send
  "send")
crucible_neg_compile_test(neg_swmr_double_mint_writer
  "cannot bind|no matching function|use of deleted")
crucible_neg_compile_test(neg_swmr_mint_wrong_reader_token
  "no matching function|could not convert|cannot convert")
crucible_neg_compile_test(neg_swmr_mint_non_swmr_surface
  "SwmrSessionSurface|constraints not satisfied|reader\\(\\)")

# ─── Graded modality discipline (Phase 2a foundation) ───────────────
#
# Three neg-compile tests pinning the modality gates on Graded<>:
#
#   - neg_graded_extract_on_absolute     extract() requires Comonad
#   - neg_graded_inject_on_comonad       inject()  requires RelativeMonad
#   - neg_refined_predicate_not_invocable Refined ctor requires
#                                         PredicateInvocableOn<Pred, T>
#
# Without these tests, a future Graded refactor that loosens the
# `requires` clauses on extract/inject would silently erase the
# modality discipline that Secret<T> (Comonad) and Tagged<T, Source>
# (RelativeMonad) rest on.  The PredicateInvocableOn test pins the
# MIGRATE-2 #462 invariant: predicate-vs-T mismatch surfaces as a
# clean concept-violation rather than a SFINAE cascade through the
# contract pre-clause.
crucible_neg_compile_test(neg_graded_extract_on_absolute
  "constraints not satisfied|ComonadModality")
crucible_neg_compile_test(neg_graded_inject_on_comonad
  "constraints not satisfied|RelativeMonadModality")
crucible_neg_compile_test(neg_refined_predicate_not_invocable
  "constraints not satisfied|PredicateInvocableOn")

# fixy-A3-006 — Quotient + Coeffect modality discipline.
#
# Modality.h ships six ModalityKind values; extract/inject are gated
# on Comonad and RelativeMonad respectively.  Quotient (FIXY-G10,
# equivalence-class membership) and Coeffect (FIXY-G11, resource
# semiring) are functional via the generic peek/grade/weaken/compose
# operations but MUST NOT admit the co/unit operations — Quotient has
# no canonical representative projection, Coeffect has no zero-cost
# unit (zero is the semiring zero, recovered via L::bottom() not via
# a structural unit).  These four fixtures pin the rejection contract;
# future Graded refactors that loosen the requires-clauses would red
# these fixtures and require explicit Modality-table revision.
crucible_neg_compile_test(neg_graded_extract_on_quotient
  "constraints not satisfied|ComonadModality")
crucible_neg_compile_test(neg_graded_extract_on_coeffect
  "constraints not satisfied|ComonadModality")
crucible_neg_compile_test(neg_graded_inject_on_quotient
  "constraints not satisfied|RelativeMonadModality")
crucible_neg_compile_test(neg_graded_inject_on_coeffect
  "constraints not satisfied|RelativeMonadModality")

# WRAP-Types-1 (#1067) — ElementBytes::value_ wraps Refined<bounded_above<16>,
# uint8_t>; constructing ElementBytes{17} in constexpr context drives the
# bounded_above<16>(17) pre-clause through constant evaluation.  The contract
# violation is rejected as a non-constant expression (P1494R5 + P2900R14),
# pinning the structural bound at the storage type rather than relying on
# the source dtype enum being well-formed.
crucible_neg_compile_test(neg_element_bytes_above_max
  "contract|bounded_above|not a constant|non-constant|constexpr")

# WRAP-IterDet-1 (#927) — IterationDetector::match_pos_ wraps Refined<
# bounded_above<MATCH_POS_MAX=K-1=4>, uint8_t>; constructing MatchPos{>4}
# in constexpr context drives the bounded_above<4>(v) pre-clause through
# constant evaluation.  Two fixtures cover distinct forbidden-value
# classes per HS14:
#   * _above_max — boundary edge (5 = K = smallest forbidden), catches
#     off-by-one drift in MATCH_POS_MAX.
#   * _uint8_max — wide miss (255 = uint8_t max), catches "lost narrowing"
#     where a wider intermediate is truncated to uint8_t pre-assignment.
crucible_neg_compile_test(neg_iter_det_match_pos_above_max
  "contract|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_iter_det_match_pos_uint8_max
  "contract|bounded_above|not a constant|non-constant|constexpr")

# WRAP-CallSite-2 (#880) — CallSiteTable::Lineno = NonNegative<int32_t>
# = Refined<non_negative, int32_t>; constructing Lineno{< 0} in
# constexpr context drives the non_negative(v) pre-clause through
# constant evaluation.  Two fixtures per HS14:
#   * _negative — boundary edge (-1 = 0 - 1), catches off-by-one drift
#     ("positive" instead of "non_negative", admits -1 if comparison
#     becomes "x > -1").
#   * _int_min — wide miss (INT32_MIN), catches "lost sign-bit" where
#     unsigned counter narrows to int32_t and predicate is too narrow
#     (e.g., `x != -1` instead of `x >= 0`).
crucible_neg_compile_test(neg_call_site_lineno_negative
  "contract|non_negative|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_call_site_lineno_int_min
  "contract|non_negative|not a constant|non-constant|constexpr")

# WRAP-CallSite-3 (#881) — CallSiteTable::Entry::filename / funcname
# migrated from raw std::string to safety::Tagged<std::string,
# source::Sanitized>.  Two HS14 fixtures cover the soundness gate's
# two directions:
#   * neg_call_site_filename_raw_string — WRITE-side gate (Tagged's
#     `explicit Tagged(T)` ctor rejects aggregate-init from a raw
#     std::string).
#   * neg_call_site_filename_external_passback — READ-side gate
#     (Tagged<T,Sanitized> does not implicitly convert to
#     Tagged<T,External>; the trust-elevation is one-way through
#     insert()).
crucible_neg_compile_test(neg_call_site_filename_raw_string
  "no match for|cannot convert|no viable|could not convert|narrowing|Tagged|explicit")
crucible_neg_compile_test(neg_call_site_filename_external_passback
  "no match for|cannot convert|no viable|could not convert|Tagged|External|Sanitized")

# WRAP-IterDet-2 (#928) — IterationDetector::SignatureLen = safety::
# BoundedMonotonic<uint32_t, K=5>; constructing SignatureLen{> K} in
# constexpr context drives the `pre(!(T(Max) < initial))` clause
# through constant evaluation.  Two fixtures per HS14:
#   * _above_k     — boundary edge (6 = K+1), catches off-by-one drift
#     in the bound (e.g. `bounded_above<K-1>` vs `bounded_above<K>`).
#   * _uint32_max  — wide miss (UINT32_MAX), catches "drop the upper
#     bound" regression where BoundedMonotonic is replaced by plain
#     Monotonic and the Max disappears entirely.
crucible_neg_compile_test(neg_iter_det_signature_len_above_k
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_iter_det_signature_len_uint32_max
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")

# WRAP-IterDet-3 (#929) — IterationDetector::OpsSinceBoundary =
# safety::Monotonic<uint32_t>; advance(smaller) and bump() at
# numeric_limits::max() drive the dual contracts (monotonicity +
# overflow) through constant evaluation.  Two fixtures per HS14:
#   * _regression — boundary edge (advance(5) from 10), catches
#     drift where Monotonic's `>=` predicate weakens to `>`, silently
#     admitting equal-value advances and (post-regression) backward
#     writes.
#   * _overflow   — wide miss (bump() at UINT32_MAX), catches
#     "drop the overflow contract" regression where bump() loses its
#     != max() check and silently wraps to 0.
# The two fixtures together pin BOTH Monotonic mutation contracts at
# the field's call-sites in IterationDetector.
crucible_neg_compile_test(neg_iter_det_ops_since_boundary_regression
  "contract|leq|Monotonic|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_iter_det_ops_since_boundary_overflow
  "contract|max|Monotonic|not a constant|non-constant|constexpr")

# WRAP-IterDet-4 (#930) — IterationDetector typestate codification
# via ScopedView.  iter_det_state::{Building, Steady} tags +
# view_ok overloads encode signature_len.get()'s phase at the type
# level so callers can witness state at compile time.  Two fixtures
# per HS14 — distinct mismatch classes:
#   * _view_steady_on_building — value-level pre at MINT TIME.
#     Fires mint_view's `pre(view_ok(d, Steady))` on a Building-
#     state detector at constexpr.  Catches drift where view_ok's
#     Steady overload weakens to admit non-K signature_len, or where
#     a caller mints a Steady view assuming the detector progressed
#     past Building when in fact it hadn't.
#   * _view_in_field — structural Tier-2 audit at FIELD AUDIT TIME.
#     Fires no_scoped_view_field_check on a struct that embeds a
#     ScopedView<IterationDetector, Steady> as a member.  Catches
#     drift in contains_scoped_view's recursive wrapper-unwrap (a
#     new container shape that escapes the audit), or a refactor
#     that adds a "cached state proof" field somewhere.
# The two fixtures together pin BOTH the value-level mint gate AND
# the structural field-storage gate of WRAP-IterDet-4 — the
# typestate's two distinct soundness witnesses.
crucible_neg_compile_test(neg_iter_det_view_steady_on_building
  "contract|view_ok|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_iter_det_view_in_field
  "ScopedView|contains_scoped_view|no_scoped_view|static assertion")

# WRAP-MerkleDag-6 (#942) — RegionNode::VariantCounter =
# safety::Monotonic<uint32_t>; the field's structurally
# monotonic-forward lifecycle (0 → some non-zero N; subsequent
# set_variant calls must monotonically advance) is now witnessed at
# the type level.  Same companion-pair shape as the IterDet trio
# above (#927/#928/#929) and CKernel-2 (#890):
#   * _regression — boundary edge (current=10, advance(5) → 5 < 10).
#     Catches off-by-one drift in Monotonic's leq predicate (e.g. a
#     refactor that uses `>` instead of `>=`, silently admitting a
#     downgrade from variant 10 to variant 9).
#   * _overflow   — wide miss (bump() at UINT32_MAX), catches
#     "drop the overflow contract" regression where bump() loses its
#     != max() check and silently wraps 4294967295 → 0 (which would
#     also break the monotonic invariant; the bump() contract is the
#     only type-level defense).
# The two fixtures together pin BOTH Monotonic mutation contracts at
# RegionNode::variant_id's call-sites — the WRAP-MerkleDag-6 promise
# that variant selection cannot go backward at the type level.
crucible_neg_compile_test(neg_merkle_dag_variant_id_regression
  "contract|leq|Monotonic|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_merkle_dag_variant_id_overflow
  "contract|max|Monotonic|not a constant|non-constant|constexpr")

# WRAP-CKernel-4 (#892) — ValidCKernelIdRaw =
# Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>; constructing the
# Refined with an out-of-range byte fires the predicate's pre-clause
# in constant evaluation.  This is the type-system gate for every
# uint8_t → CKernelId widening at FFI / deserialize boundaries
# (Cipher.deserialize_region in particular — Serialize.h:385).
# Two fixtures per HS14:
#   * _at_sentinel — boundary edge (= NUM_KERNELS, off-by-one).
#     Catches drift where the bound widens from `NUM_KERNELS - 1` to
#     `NUM_KERNELS`, silently admitting the count sentinel as a real
#     opcode (cascades into classify() returning sentinel and switch
#     fallthrough to UB).
#   * _uint8_max  — wide miss (= UINT8_MAX).  Catches "drop the bound
#     entirely" regression where ValidCKernelIdRaw degenerates into a
#     plain uint8_t typedef and admits any byte from a corrupted
#     Cipher file.
crucible_neg_compile_test(neg_ckernel_id_raw_at_sentinel
  "contract|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_ckernel_id_raw_uint8_max
  "contract|bounded_above|not a constant|non-constant|constexpr")

# WRAP-CKernel-2 (#890) — CKernelTable::SizeCounter =
# BoundedMonotonic<uint32_t, CKERNEL_TABLE_CAP=256>; constructing it
# with an out-of-range initial value fires the BoundedMonotonic ctor's
# pre-clause `!(T(Max) < initial)` in constant evaluation.  Pins the
# size counter as monotonic-and-capped at the type level — register_op's
# explicit `if (size >= CAP) std::abort()` becomes defense-in-depth.
# Two fixtures per HS14:
#   * _above_cap     — boundary edge (= CAP + 1 = 257, off-by-two).
#     Catches drift where the bound widens from
#     `BoundedMonotonic<..., CKERNEL_TABLE_CAP>` to
#     `BoundedMonotonic<..., CKERNEL_TABLE_CAP + N>` for any N >= 1.
#   * _uint32_max    — wide miss (= UINT32_MAX).  Catches "drop the
#     bound entirely" regression where SizeCounter degenerates from
#     BoundedMonotonic into a plain Monotonic<uint32_t>; under that
#     drift register_op's runtime check is the only defense and the
#     compile-time witness disappears.
crucible_neg_compile_test(neg_ckernel_table_size_above_cap
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_ckernel_table_size_uint32_max
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")

# WRAP-CKernel-5 (#893) — global CKernelTable access carries
# source::Singleton provenance. Raw pointers and differently tagged
# pointers cannot satisfy singleton-only consumers.
crucible_neg_compile_test(neg_ckernel_singleton_raw_pointer
  "could not convert|cannot convert|no matching function")
crucible_neg_compile_test(neg_ckernel_singleton_cross_tag
  "could not convert|cannot convert|no matching function")

# WRAP-SchemaTab-1 (#1003) — SchemaTable::SizeCounter =
# BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP=512>; constructing it
# with an out-of-range initial value fires the BoundedMonotonic ctor's
# pre-clause `!(T(Max) < initial)` in constant evaluation.  Pins the
# size counter as monotonic-and-capped at the type level — register_name's
# silent-truncation runtime check `if (size >= CAP) return;` becomes
# defense-in-depth (and #1009 WRAP-SchemaTab-7 will upgrade it to abort
# parity with CKernelTable).
# Two fixtures per HS14:
#   * _above_cap     — boundary edge (= CAP + 1 = 513, off-by-two).
#     Catches drift where the bound widens from
#     `BoundedMonotonic<..., SCHEMA_TABLE_CAP>` to
#     `BoundedMonotonic<..., SCHEMA_TABLE_CAP + N>` for any N >= 1.
#   * _uint32_max    — wide miss (= UINT32_MAX).  Catches "drop the
#     bound entirely" regression where SizeCounter degenerates from
#     BoundedMonotonic into plain Monotonic<uint32_t>; under that drift
#     register_name's runtime silent-truncate is the only defense and
#     the compile-time witness disappears.
crucible_neg_compile_test(neg_schema_table_size_above_cap
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_schema_table_size_uint32_max
  "contract|BoundedMonotonic|not a constant|non-constant|constexpr")

# WRAP-SchemaTab-5 (#1007) — SchemaTable::lookup() returns
# LookupName = Tagged<Borrowed<const char, SchemaTable>, source::Sanitized>.
# Two HS14 fixtures cover the lookup boundary's two soundness directions:
#   * neg_schema_table_lookup_raw_const_char — typed borrow cannot
#     decay to raw const char* without explicit .value().data().
#   * neg_schema_table_lookup_cross_tag — sanitized provenance cannot
#     pass as source::External.
crucible_neg_compile_test(neg_schema_table_lookup_raw_const_char
  "no match for|cannot convert|no viable|could not convert|Tagged|const char")
crucible_neg_compile_test(neg_schema_table_lookup_cross_tag
  "no match for|cannot convert|no viable|could not convert|Tagged|External|Sanitized")

# WRAP-SymTab-4 (#1032) — SymbolTable::add() returns
# InternalSymbolId = Tagged<SymbolId, source::FromInternal>.  Two HS14
# fixtures cover the mint boundary's two soundness directions:
#   * neg_symbol_table_add_raw_symbol_id — internal Tagged value cannot
#     decay to raw SymbolId without explicit .value().
#   * neg_symbol_table_add_cross_tag — internal provenance cannot pass
#     as source::External.
crucible_neg_compile_test(neg_symbol_table_add_raw_symbol_id
  "no match for|cannot convert|no viable|could not convert|Tagged|SymbolId|FromInternal")
crucible_neg_compile_test(neg_symbol_table_add_cross_tag
  "no match for|cannot convert|no viable|could not convert|Tagged|External|FromInternal")

# WRAP-Transaction-4 (#1063) — TransactionLog<N>'s ring-storage triple
# (entries_ + head_ + count_) is now safety::CyclicBuffer<Transaction,
# N>.  The migration's positive-power-of-2 capacity invariant is
# enforced at the TransactionLog<N> instantiation by TWO gates; the
# two fixtures (per HS14) exercise one distinct mismatch class each:
#   * _non_power_of_two (N = 7) — caught by BOTH the class-body
#     static_assert("N must be a power of 2") AND CyclicBuffer's
#     requires-clause.  A masked cursor over a non-power-of-2 ring
#     would alias logical positions onto one physical slot.
#   * _zero_capacity (N = 0) — SLIPS PAST the static_assert
#     (`0 & (0u-1u) == 0` is vacuously a power of two) and is caught
#     ONLY by CyclicBuffer's `requires (N > 0 ...)`.  Witnesses that
#     the migration STRENGTHENED the contract — the old hand-rolled
#     `entries_[0]` carried no N > 0 gate.
# (The retired _count_above_cap / _count_uint32_max fixtures targeted
#  the removed CountCounter alias; the BoundedMonotonic fill-counter
#  bound they pinned is now structurally guaranteed inside CyclicBuffer
#  and covered by neg_pre_macro_bounded_monotonic_bump_at_max.)
crucible_neg_compile_test(neg_transaction_log_non_power_of_two
  "power of 2|constraints not satisfied|static assertion")
crucible_neg_compile_test(neg_transaction_log_zero_capacity
  "constraints not satisfied|required from")

# WRAP-Transaction-2 (#1061) — Transaction::region is committed via
# Tagged<RegionNode*, source::Arena>.  Raw RegionNode* and loaded
# serialized-region provenance must fail at the commit boundary.
crucible_neg_compile_test(neg_transaction_region_raw_pointer
  "no match|no known conversion|cannot convert|could not convert")
crucible_neg_compile_test(neg_transaction_region_loaded_cross_tag
  "no match|no known conversion|cannot convert|could not convert")

# WRAP-Lower-4 (#934) — lower_trace_to_graph requires Recorded/Replayed
# TraceGraph provenance and propagates that source to the returned Graph.
crucible_neg_compile_test(neg_lower_trace_graph_raw_input
  "no match|no known conversion|cannot convert|could not convert")
crucible_neg_compile_test(neg_lower_trace_graph_cross_source
  "conversion from|no match|no known conversion|cannot convert|could not convert")

# WRAP-Lower-6 (#936) — lower_trace_to_graph runs in the background
# compilation path and allocates Graph scratch.  Required row is
# Row<Bg, Alloc>; pure, Alloc-only, and Bg-only callers are rejected.
crucible_neg_compile_test(neg_lower_trace_graph_pure_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_lower_trace_graph_alloc_only_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_lower_trace_graph_bg_only_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")

# WRAP-Transaction-1 (#1060) — Transaction::step_id raw uint64_t →
# safety::Monotonic<uint64_t>.  HS14 mandate: ≥2 negative-compile
# fixtures per soundness gate, each demonstrating a distinct mismatch
# class:
#   * neg_transaction_step_id_assign_from_raw — WRITE-side gate (no
#     operator=(uint64_t) on Monotonic; raw assignment rejected).
#   * neg_transaction_step_id_advance_retrograde — ADVANCE-side gate
#     (advance(smaller) violates lattice_type::leq pre-clause at
#     consteval, poisoning the surrounding constant evaluation).
crucible_neg_compile_test(neg_transaction_step_id_assign_from_raw
  "no match for|cannot convert|no viable|deleted|operator=")
crucible_neg_compile_test(neg_transaction_step_id_advance_retrograde
  "contract|Monotonic|not a constant|non-constant|constexpr|leq")

# WRAP-Serialize-6 (#1015) — ValidTraceNodeKindRaw =
#   safety::Refined<safety::bounded_above<TraceNodeKind::TERMINAL>, uint8_t>
# pinning the typed gate at every uint8_t → TraceNodeKind widening
# site (Cipher deserialize via Serialize::read_header is the canonical
# example).  HS14 mandate: ≥2 negative-compile fixtures per soundness
# gate, each demonstrating a distinct mismatch class.
#
#   * neg_trace_node_kind_above_terminal.cpp — boundary edge
#     (= TERMINAL + 1, off-by-one).  Catches drift where the bound
#     widens from `bounded_above<TERMINAL>` to `bounded_above<TERMINAL+K>`
#     for some K >= 1, silently admitting a byte that names no valid
#     TraceNodeKind enumerator.
#   * neg_trace_node_kind_uint8_max.cpp — wide miss (= UINT8_MAX).
#     Catches "drop the bound entirely" regression where
#     ValidTraceNodeKindRaw degenerates from
#     `Refined<bounded_above<TERMINAL>, uint8_t>` into a plain
#     `uint8_t` typedef; under that drift any byte is silently accepted
#     at construction and the type-system gate that fires before any
#     DAG traversal call ever lands disappears.
crucible_neg_compile_test(neg_trace_node_kind_above_terminal
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_node_kind_uint8_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# WRAP-Serialize-4 (#1013) — CDAG_VERSION =
#   safety::Tagged<uint32_t, source::FormatVersion>.  Disk-read
# header versions enter as source::External and must be explicitly
# checked against the in-process format constant before the wire body
# is trusted.  HS14 fixtures pin both escape directions.
#
#   * neg_cdag_version_raw_uint32.cpp — read-side raw extraction
#     rejected; byte writers must spell `.value()` at the boundary.
#   * neg_cdag_version_external_cross_tag.cpp — cross-tag assignment
#     rejected; source::FormatVersion cannot masquerade as
#     source::External.
crucible_neg_compile_test(neg_cdag_version_raw_uint32
  "conversion from|cannot convert|could not convert|no match for|Tagged")
crucible_neg_compile_test(neg_cdag_version_external_cross_tag
  "conversion from|cannot convert|could not convert|no match for|Tagged|External|FormatVersion")

# WRAP-Serialize-8 (#1017) — deserialize_region() returns
#   safety::Tagged<RegionNode*, source::Loaded>.  The raw RegionNode*
# only appears after explicit `.value()` at call sites that still need
# pointer interop.  HS14 fixtures pin the two escape directions.
#
#   * neg_deserialize_region_raw_region_ptr.cpp — implicit raw pointer
#     extraction rejected.
#   * neg_deserialize_region_cross_tag.cpp — Loaded provenance cannot
#     pass as source::External.
crucible_neg_compile_test(neg_deserialize_region_raw_region_ptr
  "conversion from|cannot convert|could not convert|no match for|Tagged|RegionNode")
crucible_neg_compile_test(neg_deserialize_region_cross_tag
  "conversion from|cannot convert|could not convert|no match for|Tagged|External|Loaded")

# WRAP-TraceLoader-3 (#1051) — ValidSchemaNameLen =
#   safety::Refined<safety::in_range<SCHEMA_NAME_LEN_MIN,
#                                    SCHEMA_NAME_LEN_MAX>, uint16_t>
# pinning the typed gate at every uint16_t → schema-name-length
# widening site (TraceLoader::load_trace is the canonical example;
# the .crtrace optional schema-name table feeds a 257-byte stack
# buffer, so admitting any value outside [1, 256] is either an empty-
# name SchemaTable poison entry (lower) or a stack-buffer overflow
# (upper)).  HS14 mandate: ≥2 negative-compile fixtures per soundness
# gate, each demonstrating a distinct mismatch class.
#
#   * neg_schema_name_len_below_min.cpp — lower-bound edge (= 0).
#     Catches drift where `in_range<1, 256>` widens to
#     `in_range<0, 256>` and silently admits a zero-length name.
#     The trailing `name_buf[name_len] = '\0'` would associate the
#     SchemaHash with the empty string in the global SchemaTable —
#     every subsequent lookup() would return the empty entry instead
#     of the real one.
#   * neg_schema_name_len_above_max.cpp — wide miss (= UINT16_MAX).
#     Catches "drop the bound entirely" regression where
#     ValidSchemaNameLen degenerates from
#     `Refined<in_range<1, 256>, uint16_t>` into a plain `uint16_t`
#     typedef; under that drift any 16-bit value is silently accepted
#     at construction and the only remaining defense is the
#     per-call-site `raw_name_len > SCHEMA_NAME_LEN_MAX` comparison
#     inside load_trace — losing the type-system gate that fires
#     BEFORE any fread call ever lands.
crucible_neg_compile_test(neg_schema_name_len_below_min
  "contract|Refined|in_range|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_schema_name_len_above_max
  "contract|Refined|in_range|not a constant|non-constant|constexpr")

# ─── WRAP-TraceLoader-2 (#1050) ValidTraceNum* neg-compile fixtures ──
#
# Three uint32_t header-count widening sites in load_trace gain typed
# Refined gates: ValidTraceNumOps (cap MAX_OPS = 1<<22 = 4 M ops),
# ValidTraceNumMetas (cap MAX_METAS = 1<<24 = 16 M metas), and
# ValidTraceNumNames (cap SCHEMA_TABLE_CAP = 512 schema-name entries).
# Each gate gets two fixtures per HS14 mandate: a boundary-edge one
# (cap + 1) catching "bound widens to admit one more" drift, and a
# wide-miss one (UINT32_MAX) catching "drop the bound entirely"
# regression that would degenerate the alias into a plain uint32_t
# typedef.  Six fixtures, three gates, one PR.
#
# Without these gates a corrupted .crtrace header could request multi-
# GB allocations (320 MB for num_ops × 80B; 2.7 GB for num_metas ×
# 168B; or — in the silent-truncation case — drive the schema-name
# loop past the SchemaTable's fixed-size entries[] array).  The
# runtime if-checks in load_trace catch the first wave; the typed
# gates catch the rest, and the fixtures wedge each bound at the type
# system so future readers / aggregate-init paths inherit the gate.
crucible_neg_compile_test(neg_trace_num_ops_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_num_ops_uint32_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_num_metas_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_num_metas_uint32_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_num_names_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_num_names_uint32_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# WRAP-TraceGraph-3 (#1044) — TraceGraph build counters are
# WriteOnce<uint32_t>.  The fixtures pin both soundness directions:
# producers cannot raw-assign a counter, and consumers cannot read one
# through an implicit uint32_t conversion.
crucible_neg_compile_test(neg_trace_graph_num_ops_assign_raw
  "no match for|no known conversion")
crucible_neg_compile_test(neg_trace_graph_num_edges_implicit_raw
  "cannot convert|invalid conversion|no viable conversion")

# ─── WRAP-MetaLog-2 (#945) ValidMetaAppendCount neg-compile fixtures ──
#
# MetaLog::try_append's `n` count parameter gains a typed Refined gate
# capped at MetaLog::CAPACITY (1<<20 = 1'048'576 entries, 168 MB).
# `n > CAPACITY` is structurally non-sensical (a single append larger
# than the entire ring can never succeed); the typed witness pins
# the bound at the type system so any caller that constructs
# ValidMetaAppendCount with an out-of-range count is rejected at the
# constructor's pre-clause.
#
# Two fixtures per HS14 mandate:
#   * neg_metalog_append_count_above_max — boundary edge (CAPACITY+1)
#     catches "bound widens to admit one more" drift.
#   * neg_metalog_append_count_uint32_max — wide miss (UINT32_MAX)
#     catches "drop the bound entirely" regression that would
#     degenerate the alias into a plain uint32_t typedef.
#
# Without the gate, a caller passing n > CAPACITY would either spin
# forever in try_append's overflow check (graceful failure but mute)
# or — on any path that bypassed the runtime check — drive a memcpy
# of n*144B beyond the ring buffer.  Defense-in-depth: the existing
# `if (h - cached_tail + n > CAPACITY)` runtime guard plus the new
# `pre (n <= CAPACITY)` clause plus the typed witness all fire
# BEFORE any memcpy or head publish.
crucible_neg_compile_test(neg_metalog_append_count_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_metalog_append_count_uint32_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# ─── WRAP-TraceRing-3 (#1055) ValidDrainCount neg-compile fixtures ──
#
# TraceRing::drain's `max_count` parameter (also drain_pinned,
# drain_pure, try_pop_batch) gains a typed Refined gate capped at
# TraceRing::CAPACITY (1<<16 = 65 536 entries, ~5.25 MB ring).
# `max_count > CAPACITY` is meaningless because the inner
# std::min(available, max_count) clamps to available <= CAPACITY,
# but the silent clamp masks call-site arithmetic bugs (UINT32_MAX
# leaked through from disk/env/FFI parsing, oversized buffer-size
# math).  The typed witness pins the bound at the type system so
# any caller that constructs ValidDrainCount with an out-of-range
# count is rejected at the constructor's pre-clause.
#
# Two fixtures per HS14 mandate:
#   * neg_trace_ring_drain_count_above_max — boundary edge
#     (CAPACITY+1) catches "bound widens to admit one more" drift.
#   * neg_trace_ring_drain_count_uint32_max — wide miss (UINT32_MAX)
#     catches "drop the bound entirely" regression that would
#     degenerate the alias into a plain uint32_t typedef.
#
# Defense-in-depth: existing `count = std::min(available, max_count)`
# runtime clamp plus the new `pre (max_count <= CAPACITY)` clause
# plus the typed witness all fire at the boundary, exposing the
# silent clamp at enforce semantics.
crucible_neg_compile_test(neg_trace_ring_drain_count_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_trace_ring_drain_count_uint32_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# ─── PROD-WRAP-5 (#534) ndim Refined<bounded_above<8>> typed gate ────
#
# TensorMeta::ndim is structurally bounded by the inline sizes[8] /
# strides[8] arrays — values past 8 cannot be expressed without
# overrunning the inline storage.  The deserialize boundary in
# Serialize.h::read_meta() previously read a raw uint8_t and assigned
# it directly to m.ndim, with no bounds check; a corrupt or
# adversarial Cipher payload could deliver a byte in [9, 255] that
# slipped through to compute_storage_nbytes (where the
# `pre(meta.ndim <= 8)` contract would terminate the program under
# enforce semantics, OR invoke `[[assume]]`-driven UB under ignore on
# hot-path TUs).
#
# ValidNDim is Refined<bounded_above<kMaxTensorNDim>, uint8_t>; its
# ctor pre-clause `bounded_above<8>(v)` rejects any byte past 8 via
# P1494R5 partial-program correctness — the deserialize path then
# routes through handle_contract_violation (logged + std::abort).
#
# Two fixtures per HS14 mandate:
#   * neg_tensor_meta_ndim_above_max — boundary edge (kMaxTensorNDim+1
#     = 9) catches drift where the bound widens by any K ≥ 1, silently
#     admitting an ndim that overruns sizes[8] / strides[8].
#   * neg_tensor_meta_ndim_uint8_max — wide miss (UINT8_MAX = 0xFF)
#     catches "drop the bound entirely" regression where ValidNDim
#     degenerates from Refined<bounded_above<8>> into a plain uint8_t
#     typedef.
#
# Defense-in-depth: TraceLoader.h:321 `if (metas[i].ndim > 8)
# [[unlikely]] return nullptr` runtime guard is retained.  ValidNDim
# catches the byte at deserialize entry; the loader guard catches it
# at iteration entry.  Both layers must agree.
crucible_neg_compile_test(neg_tensor_meta_ndim_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_tensor_meta_ndim_uint8_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# WRAP-TensorMeta-1 (#1034) — TensorMeta sizes/strides lanes now
# write through TensorDim = Refined<bounded_above<INT64_MAX / 16>,
# int64_t> while keeping the raw 64-byte lane blocks for SIMD loads.
crucible_neg_compile_test(neg_tensor_meta_dim_raw_assignment
  "no match for|cannot convert|no viable|could not convert|operator=|TensorDim")
crucible_neg_compile_test(neg_tensor_meta_dim_above_max
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# WRAP-TensorMeta-2 (#1035) — TensorMeta::data_ptr now stores
# ExternalDataPtr = Tagged<void*, source::External>.  Two HS14
# fixtures cover the provenance gate's two field-write directions:
#   * neg_tensor_meta_data_ptr_raw_pointer — raw void* cannot assign
#     directly to the explicit Tagged field.
#   * neg_tensor_meta_data_ptr_cross_tag — Tagged<void*, Sanitized>
#     cannot pass as source::External.
crucible_neg_compile_test(neg_tensor_meta_data_ptr_raw_pointer
  "no match for|cannot convert|no viable|could not convert|operator=|Tagged|ExternalDataPtr")
crucible_neg_compile_test(neg_tensor_meta_data_ptr_cross_tag
  "no match for|cannot convert|no viable|could not convert|operator=|Tagged|External|Sanitized")

# WRAP-TensorMeta-7 (#1040) — TensorMeta::grad_fn_hash now stores
# GradFnHash = Tagged<uint64_t, hash_family::FamilyB>.  Two HS14
# fixtures cover the Family-B gate's two field-write directions:
#   * neg_tensor_meta_grad_fn_hash_raw_uint64 — raw uint64_t cannot
#     assign directly to the explicit Tagged field.
#   * neg_tensor_meta_grad_fn_hash_cross_family — Family-A persistent
#     hashes cannot pass as process-local Family-B hashes.
crucible_neg_compile_test(neg_tensor_meta_grad_fn_hash_raw_uint64
  "no match for|cannot convert|no viable|could not convert|operator=|Tagged|GradFnHash")
crucible_neg_compile_test(neg_tensor_meta_grad_fn_hash_cross_family
  "no match for|cannot convert|no viable|could not convert|operator=|Tagged|FamilyA|FamilyB")

# WRAP-DimHash-1 (#910) — dim_hash_*_det returns
# DetSafe<Pure, Tagged<uint64_t, hash_family::FamilyB>>.  Two HS14
# fixtures cover both fences:
#   * neg_dim_hash_raw_uint64 — raw uint64_t cannot enter the typed
#     deterministic Family-B carrier.
#   * neg_dim_hash_cross_family — Family-A values cannot substitute
#     for process-local Family-B dim hashes.
crucible_neg_compile_test(neg_dim_hash_raw_uint64
  "cannot convert|no matching function|could not convert|DetSafe|DimHash|Tagged")
crucible_neg_compile_test(neg_dim_hash_cross_family
  "cannot convert|no matching function|could not convert|DetSafe|Tagged|FamilyA|FamilyB")

# GAPS-135 — observe/HdrHistogram.h keeps the recording boundary typed
# and the merge geometry exact:
#   * neg_hdr_histogram_record_raw_uint64 — raw external samples cannot
#     bypass Refined<in_range<1, MaxValue>, uint64_t>.
#   * neg_hdr_histogram_merge_cross_config — histograms with different
#     significant-digit layouts cannot be merged.
crucible_neg_compile_test(neg_hdr_histogram_record_raw_uint64
  "cannot convert|no matching function|could not convert|Refined|in_range|value_type")
crucible_neg_compile_test(neg_hdr_histogram_merge_cross_config
  "cannot convert|no matching function|could not convert|HdrHistogram")

# GAPS-141 — observe/SyntheticProbe.h keeps synthetic-probe publication
# on the background row and transport registration typed:
#   * neg_synthetic_probe_record_fg_ctx — HotFgCtx cannot record probe
#     outcomes because it does not admit Row<Bg>.
#   * neg_synthetic_probe_raw_kind_mask — raw integer masks cannot
#     substitute for Bits<TransportProbeKind>.
crucible_neg_compile_test(neg_synthetic_probe_record_fg_ctx
  "constraint|constraints not satisfied|CtxFitsSyntheticProbeRecord|record_gate|GAPS-141")
crucible_neg_compile_test(neg_synthetic_probe_raw_kind_mask
  "cannot convert|no matching function|could not convert|Bits|TransportProbeKind|GAPS-141")

# GAPS-180 — observe/SdcDetect.h keeps redundant-compute evidence typed:
#   * neg_sdc_detect_mint_bg_ctx — only Init-row contexts mint detectors.
#   * neg_sdc_detect_run_hot_ctx — hot foreground contexts cannot mutate
#     bounded SDC event state.
#   * neg_sdc_detect_raw_result_as_verified — raw results cannot stand in
#     for source::SdcVerified values.
#   * neg_sdc_detect_cross_tag_result — external provenance cannot stand
#     in for source::SdcVerified values.
#   * neg_sdc_detect_sampling_rate_above_max — ppm rates above 100% cannot
#     cross the SdcConfig boundary.
crucible_neg_compile_effects_test(neg_sdc_detect_mint_bg_ctx
  "constraint|constraints not satisfied|CtxFitsSdcMint|Init|Bg")
crucible_neg_compile_effects_test(neg_sdc_detect_run_hot_ctx
  "constraint|constraints not satisfied|CtxFitsSdcRun|HotFgCtx|Bg")
crucible_neg_compile_test(neg_sdc_detect_raw_result_as_verified
  "cannot convert|no matching function|could not convert|Tagged|SdcVerified")
crucible_neg_compile_test(neg_sdc_detect_cross_tag_result
  "cannot convert|no matching function|could not convert|Tagged|External|SdcVerified")
crucible_neg_compile_test(neg_sdc_detect_sampling_rate_above_max
  "contract|violation|in_range|Refined|SdcSamplingRatePpm")

# GAPS-138 — canopy/Hlc.h Hybrid Logical Clock primitive.
#   * neg_hlc_raw_timestamp_to_tagged: raw HlcTimestamp cannot enter
#     APIs requiring source::Hlc provenance.
#   * neg_hlc_cross_source_timestamp: source::External timestamps do
#     not implicitly convert to source::Hlc.
#   * neg_hlc_mint_test_context: Init-only mint factory rejects Test.
crucible_neg_compile_test(neg_hlc_raw_timestamp_to_tagged
  "cannot convert|no matching function|could not convert|Tagged|HlcTimestamp")
crucible_neg_compile_test(neg_hlc_cross_source_timestamp
  "cannot convert|no matching function|could not convert|Tagged|External|Hlc")
crucible_neg_compile_effects_test(neg_hlc_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-215 — canopy/VectorClock.h operational vector clock.
#   * neg_vector_clock_zero_nodes: MaxNodes=0 is rejected at the
#     mint concept gate.
#   * neg_vector_clock_mint_test_context: Init-only mint factory
#     rejects Test.
#   * neg_vector_clock_cross_n_snapshot: snapshots with different N
#     cannot cross a fixed-size API boundary.
crucible_neg_compile_test(neg_vector_clock_zero_nodes
  "constraints not satisfied|VectorClockNodeBound|no matching function|MaxNodes")
crucible_neg_compile_effects_test(neg_vector_clock_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")
crucible_neg_compile_test(neg_vector_clock_cross_n_snapshot
  "cannot convert|no matching function|could not convert|VectorClockSnapshot")

# GAPS-214 — canopy/Crdt.h state-based CRDT substrate.
#   * neg_crdt_gset_raw_add: local updates require source::Local.
#   * neg_crdt_merge_raw_state: received state requires source::Gossiped.
#   * neg_crdt_merge_local_state: source::Local state cannot substitute
#     for source::Gossiped state at merge.
#   * neg_crdt_lww_partial_clock: LWW clocks must be total-order clocks,
#     not vector clocks with unordered concurrency.
#   * neg_crdt_mv_register_unordered_value: MV-register values must be
#     totally ordered so concurrent versions have byte-stable order.
crucible_neg_compile_test(neg_crdt_gset_raw_add
  "cannot convert|no matching function|could not convert|Tagged|Local")
crucible_neg_compile_test(neg_crdt_merge_raw_state
  "cannot convert|no matching function|could not convert|Gossiped|state_type")
crucible_neg_compile_test(neg_crdt_merge_local_state
  "cannot convert|no matching function|could not convert|Local|Gossiped")
crucible_neg_compile_test(neg_crdt_lww_partial_clock
  "constraints not satisfied|three_way_comparable|strong_ordering|partial_ordering")
crucible_neg_compile_test(neg_crdt_mv_register_unordered_value
  "constraints not satisfied|totally_ordered|three_way_comparable|operator<")

# GAPS-114 — canopy/Swim.h bounded SWIM membership substrate.
#   * neg_swim_zero_max_peers: MaxPeers=0 is rejected structurally.
#   * neg_swim_raw_peer_add: add_peer requires source::SwimMember.
#   * neg_swim_external_peer_add: source::External cannot substitute
#     for source::SwimMember.
#   * neg_swim_mint_test_context: Init-only mint factory rejects Test.
crucible_neg_compile_test(neg_swim_zero_max_peers
  "constraints not satisfied|SwimCapacity|MaxPeers")
crucible_neg_compile_test(neg_swim_raw_peer_add
  "cannot convert|no matching function|could not convert|SwimPeer|Tagged")
crucible_neg_compile_test(neg_swim_external_peer_add
  "cannot convert|no matching function|could not convert|External|SwimMember")
crucible_neg_compile_effects_test(neg_swim_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-211 — canopy/HyParView.h bounded active/passive overlay substrate.
#   * neg_hyparview_zero_active: MaxActive=0 is rejected structurally.
#   * neg_hyparview_raw_peer_join: join requires source::HyParView.
#   * neg_hyparview_cross_tag_peer: source::External cannot substitute.
#   * neg_hyparview_raw_shuffle: shuffle exchange must be gossiped.
#   * neg_hyparview_mint_test_context: Init-only mint rejects Test.
crucible_neg_compile_test(neg_hyparview_zero_active
  "constraints not satisfied|HyParViewShape|MaxActive")
crucible_neg_compile_test(neg_hyparview_raw_peer_join
  "cannot convert|no matching function|could not convert|HyParViewPeer|Tagged")
crucible_neg_compile_test(neg_hyparview_cross_tag_peer
  "cannot convert|no matching function|could not convert|External|HyParView")
crucible_neg_compile_test(neg_hyparview_raw_shuffle
  "cannot convert|no matching function|could not convert|GossipedHyParViewShuffle|Tagged")
crucible_neg_compile_effects_test(neg_hyparview_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-212 — canopy/Plumtree.h eager/lazy broadcast substrate.
#   * neg_plumtree_zero_peers: MaxPeers=0 is rejected structurally.
#   * neg_plumtree_raw_peer: link mutation requires source::HyParView.
#   * neg_plumtree_raw_message: received messages must be gossiped.
#   * neg_plumtree_raw_ihave: repair summaries must be gossiped.
#   * neg_plumtree_mint_test_context: Init-only mint rejects Test.
crucible_neg_compile_test(neg_plumtree_zero_peers
  "constraints not satisfied|PlumtreeShape|MaxPeers")
crucible_neg_compile_test(neg_plumtree_raw_peer
  "cannot convert|no matching function|could not convert|HyParViewPeer|Tagged")
crucible_neg_compile_test(neg_plumtree_raw_message
  "cannot convert|no matching function|could not convert|GossipedPlumtreeMessage|Tagged")
crucible_neg_compile_test(neg_plumtree_raw_ihave
  "cannot convert|no matching function|could not convert|GossipedPlumtreeIHave|Tagged")
crucible_neg_compile_effects_test(neg_plumtree_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-213 — canopy/Lifeguard.h SWIM false-positive reduction substrate.
#   * neg_lifeguard_zero_peers: MaxPeers=0 is rejected structurally.
#   * neg_lifeguard_raw_local_peer: local identity requires SwimPeer.
#   * neg_lifeguard_mint_test_context: Init-only mint rejects Test.
crucible_neg_compile_test(neg_lifeguard_zero_peers
  "constraints not satisfied|LifeguardShape|SwimCapacity|MaxPeers")
crucible_neg_compile_test(neg_lifeguard_raw_local_peer
  "cannot convert|no matching function|could not convert|SwimPeer|Tagged")
crucible_neg_compile_effects_test(neg_lifeguard_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-115 — canopy/Scuttlebutt.h bounded anti-entropy substrate.
#   * neg_scuttlebutt_zero_max_peers: MaxPeers=0 is rejected structurally.
#   * neg_scuttlebutt_raw_key_register: raw ScuttlebuttKey cannot
#     register a CRDT; LocalScuttlebuttKey is required.
#   * neg_scuttlebutt_raw_delta_apply: raw deltas cannot cross the
#     gossiped merge boundary.
#   * neg_scuttlebutt_non_crdt_publish: publish requires the CRDT concept.
#   * neg_scuttlebutt_mint_test_context: Init-only mint rejects Test.
crucible_neg_compile_test(neg_scuttlebutt_zero_max_peers
  "constraints not satisfied|ScuttlebuttShape|MaxPeers")
crucible_neg_compile_test(neg_scuttlebutt_raw_key_register
  "cannot convert|no matching function|could not convert|LocalScuttlebuttKey|Tagged")
crucible_neg_compile_test(neg_scuttlebutt_raw_delta_apply
  "cannot convert|no matching function|could not convert|GossipedScuttlebuttDelta|Tagged")
crucible_neg_compile_test(neg_scuttlebutt_non_crdt_publish
  "constraints not satisfied|ScuttlebuttCrdt|state_type|merge")
crucible_neg_compile_effects_test(neg_scuttlebutt_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")

# GAPS-120 — cntp/CongestionControl.h per-socket TCP CC selection.
#   * neg_congestion_control_raw_algorithm_apply: raw enum values cannot
#     drive TCP_CONGESTION; callers must use a tagged DeclaredCcChoice.
#   * neg_congestion_control_dctcp_cross_dc: DCTCP is lossless-fabric only.
#   * neg_congestion_control_custom_missing_name: custom CC modules must
#     provide a constexpr kernel congestion-control name.
crucible_neg_compile_test(neg_congestion_control_raw_algorithm_apply
  "could not convert|no matching function|DeclaredCcChoice|Tagged")
crucible_neg_compile_test(neg_congestion_control_dctcp_cross_dc
  "constraints not satisfied|CcCompatible|LosslessDatacenterFabric")
crucible_neg_compile_test(neg_congestion_control_custom_missing_name
  "constraints not satisfied|CustomCcModule|congestion_control_name")

# GAPS-196 — cog/Calibrate.h per-Cog calibration substrate.
#   * neg_calibrate_psu_rail: only schedulable substrates calibrate.
#   * neg_calibrate_zero_latency_quantile: measured latency must be positive.
#   * neg_calibrate_hot_fg_ctx: calibration is Init/Bg work, never hot Fg.
crucible_neg_compile_cog_test(neg_calibrate_psu_rail
  "constraints not satisfied|CalibratableCogKind|HasOpcodeTable|HasCaps")
crucible_neg_compile_cog_test(neg_calibrate_zero_latency_quantile
  "contract|precondition|calibration_quantiles_valid|Refined|CalibrationLatencyQuantiles")
crucible_neg_compile_effects_test(neg_calibrate_hot_fg_ctx
  "constraints not satisfied|CtxFitsCalibration|CtxAdmits|Init|Bg")

# GAPS-192 — cog/NicConfig.h ethtool/sysctl/tc-qdisc config substrate.
#   * neg_nic_config_raw_apply: privileged apply requires tagged config.
#   * neg_nic_config_ring_not_power_of_two: ring sizes are power-of-two
#     values in [256, 8192].
#   * neg_nic_config_mint_bg_ctx: config minting is Init-row work.
crucible_neg_compile_cog_test(neg_nic_config_raw_apply
  "could not convert|no matching function|DeclaredNicConfig|Tagged")
crucible_neg_compile_cog_test(neg_nic_config_ring_not_power_of_two
  "contract|precondition|power_of_two|Refined|NicRingSize")
crucible_neg_compile_effects_test(neg_nic_config_mint_bg_ctx
  "constraints not satisfied|CtxFitsNicConfigMint|Init")

# GAPS-147 — cog/SrIov.h SR-IOV / VF management substrate.
#   * neg_sriov_raw_plan_enable: privileged enable requires tagged intent.
#   * neg_sriov_zero_vfs: zero VFs are the disable state, not an enable plan.
#   * neg_sriov_multicast_mac: VF MACs must be non-zero unicast addresses.
#   * neg_sriov_mint_bg_ctx: SR-IOV plan minting is Init-row work.
crucible_neg_compile_cog_test(neg_sriov_raw_plan_enable
  "could not convert|no matching function|DeclaredSrIovPlan|Tagged")
crucible_neg_compile_cog_test(neg_sriov_zero_vfs
  "contract|precondition|bounded|Bounded|VfCount|Refined")
crucible_neg_compile_cog_test(neg_sriov_multicast_mac
  "contract|precondition|vf_mac_valid|Refined|VfMacAddress")
crucible_neg_compile_effects_test(neg_sriov_mint_bg_ctx
  "constraints not satisfied|CtxFitsSrIovMint|Init")

# GAPS-121 — cntp/Pacing.h fq/qdisc verification + SO_MAX_PACING_RATE.
#   * neg_pacing_bbr_pfifo_config: BBR pacing config admits only fq/fq_codel.
#   * neg_pacing_raw_rate: socket pacing requires admitted positive rate.
#   * neg_pacing_raw_config_ensure: fq verification requires tagged qdisc config.
crucible_neg_compile_test(neg_pacing_bbr_pfifo_config
  "constraints not satisfied|BbrCompatibleQdisc|Qdisc::Pfifo")
crucible_neg_compile_test(neg_pacing_raw_rate
  "could not convert|no matching function|PositivePacingRate|Refined")
crucible_neg_compile_test(neg_pacing_raw_config_ensure
  "could not convert|no matching function|DeclaredQdiscConfig|Tagged")

# GAPS-122 — cntp/PathSwap.h application-level resource transition.
#   * neg_path_swap_raw_plan_begin: state-machine entry requires a tagged plan.
#   * neg_path_swap_commit_fg_ctx: commit is a Bg-row runtime transition.
#   * neg_path_swap_commit_non_session_resource: new path resource must satisfy
#     SessionResource pin discipline.
crucible_neg_compile_test(neg_path_swap_raw_plan_begin
  "could not convert|no matching function|DeclaredPathSwapPlan|Tagged")
crucible_neg_compile_effects_test(neg_path_swap_commit_fg_ctx
  "constraints not satisfied|CtxFitsPathSwapTransition|Bg")
crucible_neg_compile_test(neg_path_swap_commit_non_session_resource
  "constraints not satisfied|SessionResource|PathSwapSessionResource")

# GAPS-123 — topology/CongestionTelemetry.h TCP_INFO feedback loop.
#   * neg_congestion_telemetry_raw_fd_harvest: live harvest requires
#     an admitted SocketFd, not a raw int.
#   * neg_congestion_telemetry_raw_state_aggregate: per-link aggregation
#     accepts tagged TcpInfoSnapshot values, not raw CongestionState.
#   * neg_congestion_telemetry_worker_fg_record: rt-owned telemetry updates
#     are Bg-row work and cannot run in the foreground hot path.
crucible_neg_compile_test(neg_congestion_telemetry_raw_fd_harvest
  "could not convert|no matching function|SocketFd|Refined")
crucible_neg_compile_test(neg_congestion_telemetry_raw_state_aggregate
  "could not convert|no matching function|TcpInfoSnapshot|Tagged")
crucible_neg_compile_effects_test(neg_congestion_telemetry_worker_fg_record
  "constraints not satisfied|CtxFitsCongestionTelemetryHarvest|Bg")

# GAPS-124 — cntp/IncastControl.h incast mitigation substrate.
#   * neg_incast_raw_fd_configure: socket tuning requires admitted SocketFd.
#   * neg_incast_raw_config_configure: socket tuning requires tagged config.
#   * neg_incast_credit_fg_ctx: credit mutation is Bg-row work.
crucible_neg_compile_test(neg_incast_raw_fd_configure
  "could not convert|no matching function|SocketFd|Refined")
crucible_neg_compile_test(neg_incast_raw_config_configure
  "could not convert|no matching function|DeclaredIncastConfig|Tagged")
crucible_neg_compile_effects_test(neg_incast_credit_fg_ctx
  "constraints not satisfied|CtxFitsIncastCredit|Bg")

# GAPS-125 — cntp/RoceConfig.h RoCEv2 PFC/ECN/DCQCN config substrate.
#   * neg_roce_zero_pfc_mask: PFC priority mask cannot be empty.
#   * neg_roce_dscp_above_max: DSCP is a 6-bit value.
#   * neg_roce_raw_config_apply: privileged apply requires tagged config.
crucible_neg_compile_test(neg_roce_zero_pfc_mask
  "constraints not satisfied|ValidPfcPriorityMask|PfcPriorities")
crucible_neg_compile_test(neg_roce_dscp_above_max
  "constraints not satisfied|ValidRoceDscp|Dscp")
crucible_neg_compile_test(neg_roce_raw_config_apply
  "could not convert|no matching function|DeclaredRoceConfig|Tagged")

# GAPS-132 — cntp/_wip/GpuDirect.h GPUDirect RDMA / Storage substrate.
#   * neg_gpu_direct_null_address: GPU virtual address must be non-null.
#   * neg_gpu_direct_zero_bytes: registered/transfer size must be positive.
#   * neg_gpu_direct_raw_plan_register: MR registration requires tagged intent.
#   * neg_gpu_direct_mint_bg_ctx: GPUDirect plan minting is Init-row work.
crucible_neg_compile_test(neg_gpu_direct_null_address
  "contract|precondition|non_zero|predicate|static assertion")
crucible_neg_compile_test(neg_gpu_direct_zero_bytes
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_gpu_direct_raw_plan_register
  "could not convert|no matching function|DeclaredGpuDirectMrPlan|Tagged")
crucible_neg_compile_effects_test(neg_gpu_direct_mint_bg_ctx
  "constraints not satisfied|CtxFitsGpuDirectMint|Init")

# GAPS-133 — cntp/_wip/Sharp.h in-network reduction substrate.
#   * neg_sharp_zero_participants: SHARP fabric participants are positive.
#   * neg_sharp_raw_plan_dispatch: dispatch requires tagged Sharp intent.
#   * neg_sharp_strict_recipe_concept: BITEXACT_STRICT cannot enter SHARP.
#   * neg_sharp_mint_bg_ctx: fabric context minting is Init-row work.
#   * neg_sharp_dispatch_init_ctx: all-reduce dispatch is Bg-row work.
crucible_neg_compile_test(neg_sharp_zero_participants
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_sharp_raw_plan_dispatch
  "could not convert|no matching function|DeclaredSharpFabricPlan|Tagged")
crucible_neg_compile_test(neg_sharp_strict_recipe_concept
  "constraints not satisfied|SharpEligibleRecipe|BITEXACT_STRICT|determinism")
crucible_neg_compile_effects_test(neg_sharp_mint_bg_ctx
  "constraints not satisfied|CtxFitsSharpMint|Init")
crucible_neg_compile_effects_test(neg_sharp_dispatch_init_ctx
  "constraints not satisfied|CtxFitsSharpDispatch|Bg")

# GAPS-144 — cntp/_wip/Doca.h DPU / SmartNIC offload substrate.
#   * neg_doca_zero_program_id: DOCA program IDs reserve zero.
#   * neg_doca_zero_image_bytes: offload images must be non-empty.
#   * neg_doca_zero_queue_depth: DPU queues must have positive depth.
#   * neg_doca_raw_plan_deploy: deploy requires tagged DOCA intent.
#   * neg_doca_mint_bg_ctx: deployment-plan minting is Init-row work.
#   * neg_doca_send_init_ctx: DPU communication is Bg-row work.
crucible_neg_compile_test(neg_doca_zero_program_id
  "contract|precondition|non_zero|predicate|static assertion")
crucible_neg_compile_test(neg_doca_zero_image_bytes
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_doca_zero_queue_depth
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_doca_raw_plan_deploy
  "could not convert|no matching function|DeclaredDocaDeployPlan|Tagged")
crucible_neg_compile_effects_test(neg_doca_mint_bg_ctx
  "constraints not satisfied|CtxFitsDocaMint|Init")
crucible_neg_compile_effects_test(neg_doca_send_init_ctx
  "constraints not satisfied|CtxFitsDocaComm|Bg")

# GAPS-145 — cntp/_wip/P4.h programmable switch dataplane substrate.
#   * neg_p4_zero_program_id: P4 program IDs reserve zero.
#   * neg_p4_zero_source_bytes: admitted P4 source/program artifacts are non-empty.
#   * neg_p4_zero_tcam_entries: switch TCAM budget requests are positive.
#   * neg_p4_raw_program_deploy: deployment requires tagged P4 intent.
#   * neg_p4_mint_bg_ctx: program minting is Init-row work.
crucible_neg_compile_test(neg_p4_zero_program_id
  "contract|precondition|non_zero|predicate|static assertion")
crucible_neg_compile_test(neg_p4_zero_source_bytes
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_p4_zero_tcam_entries
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_p4_raw_program_deploy
  "could not convert|no matching function|DeclaredP4Program|Tagged")
crucible_neg_compile_effects_test(neg_p4_mint_bg_ctx
  "constraints not satisfied|CtxFitsP4Mint|Init")

# GAPS-148 — cntp/Tcam.h hardware ACL / flow-rule substrate.
#   * neg_tcam_zero_entry_count: TCAM table capacities are positive.
#   * neg_tcam_static_table_zero: in-process table image has nonzero capacity.
#   * neg_tcam_raw_rule_add: add_rule requires source::TcamFlowRule provenance.
#   * neg_tcam_rule_handle_copy: installed rule handles are linear.
#   * neg_tcam_mint_bg_ctx: table minting is Init-row work.
crucible_neg_compile_test(neg_tcam_zero_entry_count
  "contract|precondition|positive|predicate|static assertion")
crucible_neg_compile_test(neg_tcam_static_table_zero
  "constraints not satisfied|TcamTableShape")
crucible_neg_compile_test(neg_tcam_raw_rule_add
  "could not convert|no matching function|DeclaredTcamFlowRule|Tagged")
crucible_neg_compile_test(neg_tcam_rule_handle_copy
  "Linear<T> is move-only|use of deleted|deleted function")
crucible_neg_compile_effects_test(neg_tcam_mint_bg_ctx
  "constraints not satisfied|CtxFitsTcamMint|Init")

# GAPS-160 — cntp/_wip/Wireguard.h site-to-site VPN substrate.
#   * neg_wireguard_zero_allowed_ips: peers must own at least one allowed CIDR.
#   * neg_wireguard_raw_public_key_peer: peer admission requires tagged pubkeys.
#   * neg_wireguard_private_key_copy: private keys are Secret and move-only.
#   * neg_wireguard_mint_bg_ctx: tunnel minting is Init-row work.
crucible_neg_compile_test(neg_wireguard_zero_allowed_ips
  "constraints not satisfied|WireguardAllowedIpShape")
crucible_neg_compile_test(neg_wireguard_raw_public_key_peer
  "could not convert|no matching function|DeclaredWireguardPublicKey|Tagged")
crucible_neg_compile_test(neg_wireguard_private_key_copy
  "Secret<T> cannot be silently duplicated|use of deleted|deleted function")
crucible_neg_compile_effects_test(neg_wireguard_mint_bg_ctx
  "constraints not satisfied|CtxFitsWireguardMint|Init")

# GAPS-126 — cntp/MtlsTransport.h mutual-TLS federation policy substrate.
#   * neg_mtls_tls12_config: TLS 1.2 cannot mint a declared mTLS config.
#   * neg_mtls_legacy_cipher_config: RSA/3DES cannot enter the cipher set.
#   * neg_mtls_raw_config_connect: runtime connect requires tagged policy.
crucible_neg_compile_test(neg_mtls_tls12_config
  "constraints not satisfied|SupportedMtlsVersion|TlsVersion::V12")
crucible_neg_compile_test(neg_mtls_legacy_cipher_config
  "constraints not satisfied|ApprovedMtlsCipherSuite|LegacyRsa3desSha")
crucible_neg_compile_test(neg_mtls_raw_config_connect
  "invalid initialization of reference|no matching function|DeclaredMtlsConfig|Tagged")

# GAPS-128 — cntp/_wip/QuicTransport.h QUIC + TLS 1.3 substrate.
#   * neg_quic_raw_config_connect: runtime connect requires tagged QUIC policy.
#   * neg_quic_raw_cc_config: QUIC config minting requires tagged CC policy.
#   * neg_quic_open_stream_fg_ctx: foreground contexts cannot mutate streams.
#   * neg_quic_raw_migration_plan: migration requires a tagged path-swap plan.
crucible_neg_compile_test(neg_quic_raw_config_connect
  "could not convert|no matching function|DeclaredQuicConfig|Tagged")
crucible_neg_compile_test(neg_quic_raw_cc_config
  "could not convert|no matching function|DeclaredCcChoice|Tagged")
crucible_neg_compile_effects_test(neg_quic_open_stream_fg_ctx
  "constraints not satisfied|CtxFitsQuicRuntime|Bg")
crucible_neg_compile_test(neg_quic_raw_migration_plan
  "could not convert|no matching function|DeclaredPathSwapPlan|Tagged")

# GAPS-146 — cntp/_wip/KtlsOffload.h kernel-TLS offload substrate.
#   * neg_ktls_tls12_crypto: TLS 1.2 record secrets cannot be admitted.
#   * neg_ktls_chacha_crypto: software ChaCha20 TLS suites are not kTLS AES-GCM.
#   * neg_ktls_raw_crypto_offload: offload requests require tagged crypto info.
crucible_neg_compile_test(neg_ktls_tls12_crypto
  "constraints not satisfied|SupportedKtlsVersion|TlsVersion::V12")
crucible_neg_compile_test(neg_ktls_chacha_crypto
  "constraints not satisfied|KtlsAesGcmCipherSuite|TlsChacha20Poly1305Sha256")
crucible_neg_compile_test(neg_ktls_raw_crypto_offload
  "could not convert|no matching function|DeclaredTlsCryptoInfo|Tagged")

# GAPS-137 — cntp/Backpressure.h + cntp/BackpressureRuntime.h substrate.
#   * neg_backpressure_raw_admission_decision: admission outcomes must be tagged.
#   * neg_backpressure_invalid_resource_kind: pressure samples require ResourceKind.
#   * neg_backpressure_hot_ctx_credit: hot replay contexts cannot mutate credits.
crucible_neg_compile_test(neg_backpressure_raw_admission_decision
  "conversion from|could not convert|DeclaredAdmissionDecision|Tagged")
crucible_neg_compile_test(neg_backpressure_invalid_resource_kind
  "constraints not satisfied|IsResourceKind|ResourceKind")
crucible_neg_compile_test(neg_backpressure_hot_ctx_credit
  "constraints not satisfied|CtxFitsBackpressureRuntime|HotFgCtx")

# GAPS-131 — cntp/AfXdp.h typed AF_XDP substrate.
#   * neg_af_xdp_raw_config: socket minting requires source::AfXdp config.
#   * neg_af_xdp_non_power_of_two_frame: static shape rejects invalid frame size.
#   * neg_af_xdp_cross_source_packet: TX enqueue accepts socket-borrowed packets.
crucible_neg_compile_test(neg_af_xdp_raw_config
  "could not convert|no matching function|DeclaredAfXdpConfig|Tagged")
crucible_neg_compile_test(neg_af_xdp_non_power_of_two_frame
  "constraints not satisfied|AfXdpStaticShape|1500")
crucible_neg_compile_test(neg_af_xdp_cross_source_packet
  "cannot convert|no matching function|Borrowed|OtherOwner|AfXdpSocket")

# GAPS-136 — cntp/ConnectionPool.h + cntp/ConnectionPoolRuntime.h substrate.
#   * neg_connection_pool_raw_event: pool events must carry source::ConnectionPool.
#   * neg_connection_pool_invalid_transport: only enumerated transport classes
#     may mint connections/pools.
#   * neg_connection_pool_hot_ctx_lease: hot replay contexts cannot mutate pool state.
crucible_neg_compile_test(neg_connection_pool_raw_event
  "conversion from|could not convert|DeclaredPoolEvent|Tagged")
crucible_neg_compile_test(neg_connection_pool_invalid_transport
  "constraints not satisfied|PoolTransportClass|TransportClass")
crucible_neg_compile_test(neg_connection_pool_hot_ctx_lease
  "constraints not satisfied|CtxFitsConnectionPoolRuntime|HotFgCtx")

# GAPS-116 — cntp/Integrity.h end-to-end xxHash64 verification.
#   * neg_integrity_raw_payload_as_verified: raw bytes cannot satisfy the
#     IntegrityVerified provenance lane.
#   * neg_integrity_cross_tag_verified: External is not IntegrityVerified.
#   * neg_integrity_raw_hash_field: hash field requires refined non-zero
#     IntegrityHash admission.
#   * neg_integrity_non_contiguous_wrap: payloads must be byte-contiguous.
crucible_neg_compile_test(neg_integrity_raw_payload_as_verified
  "cannot convert|no matching function|IntegrityVerified|Tagged")
crucible_neg_compile_test(neg_integrity_cross_tag_verified
  "cannot convert|no matching function|IntegrityVerified|External|Tagged")
crucible_neg_compile_test(neg_integrity_raw_hash_field
  "cannot convert|could not convert|IntegrityHash|Refined")
crucible_neg_compile_test(neg_integrity_non_contiguous_wrap
  "constraints not satisfied|ByteContiguousPayload|HasContiguousPayloadData|no matching function")

# GAPS-172 — cntp/GossipMulticast.h XDP_TX multicast plan substrate.
#   * neg_gossip_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_gossip_multicast_raw_plan: XDP program extraction requires
#     source::GossipMulticast provenance.
#   * neg_gossip_multicast_raw_topic: raw topic keys cannot register
#     neighbors; callers must pass DeclaredGossipTopic.
#   * neg_gossip_multicast_zero_neighbors: zero-neighbor plans are rejected
#     at the shape concept.
crucible_neg_compile_test(neg_gossip_multicast_mint_bg_ctx
  "constraints not satisfied|CtxFitsGossipMulticastMint|Init|Bg")
crucible_neg_compile_test(neg_gossip_multicast_raw_plan
  "cannot convert|could not convert|DeclaredGossipMulticastPlan|Tagged")
crucible_neg_compile_test(neg_gossip_multicast_raw_topic
  "cannot convert|could not convert|DeclaredGossipTopic|Tagged")
crucible_neg_compile_test(neg_gossip_multicast_zero_neighbors
  "constraints not satisfied|GossipMulticastShape|GossipNeighborShape|MaxNeighbors")

# GAPS-139 — cntp/OverlayMulticast.h application-layer route substrate.
#   * neg_overlay_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_overlay_multicast_raw_peer: raw peer refs cannot seed a plan.
#   * neg_overlay_multicast_zero_stripes: zero-stripe route tables rejected.
#   * neg_overlay_multicast_cross_tag_peer: externally tagged peer refs cannot
#     seed an overlay route plan.
crucible_neg_compile_test(neg_overlay_multicast_mint_bg_ctx
  "constraints not satisfied|CtxFitsOverlayMulticastMint|Init|Bg")
crucible_neg_compile_test(neg_overlay_multicast_raw_peer
  "cannot convert|could not convert|DeclaredOverlayPeer|Tagged")
crucible_neg_compile_test(neg_overlay_multicast_zero_stripes
  "constraints not satisfied|OverlayMulticastShape|MaxStripes")
crucible_neg_compile_test(neg_overlay_multicast_cross_tag_peer
  "no matching function|cannot convert|could not convert|Tagged")

# GAPS-117 — cntp/Fec.h GF(256) systematic Reed-Solomon primitive.
#   * neg_fec_zero_k / neg_fec_zero_m: shard counts must be positive.
#   * neg_fec_shape_over_256: GF(2^8) limits K+M to 256 coordinates.
#   * neg_fec_mint_test_context: the mint is Init-only.
#   * neg_fec_encode_non_contiguous_owned: owned FEC input must be a
#     Linear byte-contiguous buffer.
crucible_neg_compile_test(neg_fec_zero_k
  "constraints not satisfied|ReedSolomonShape|K")
crucible_neg_compile_test(neg_fec_zero_m
  "constraints not satisfied|ReedSolomonShape|M")
crucible_neg_compile_test(neg_fec_shape_over_256
  "constraints not satisfied|ReedSolomonShape|256")
crucible_neg_compile_effects_test(neg_fec_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")
crucible_neg_compile_test(neg_fec_encode_non_contiguous_owned
  "constraints not satisfied|ByteContiguousBuffer|no matching function")

# GAPS-119 — cntp/Fountain.h deterministic LT fountain primitive.
#   * neg_fountain_zero_symbols / neg_fountain_zero_symbol_bytes:
#     fountain shape dimensions must be positive.
#   * neg_fountain_mint_test_context: the mint is Init-only.
#   * neg_fountain_raw_seed: repair choices require a DetSafe Philox key.
#   * neg_fountain_encode_non_contiguous_owned: owned payloads must be
#     Linear byte-contiguous buffers.
crucible_neg_compile_test(neg_fountain_zero_symbols
  "constraints not satisfied|FountainShape|SourceSymbols")
crucible_neg_compile_test(neg_fountain_zero_symbol_bytes
  "constraints not satisfied|FountainShape|SymbolBytes")
crucible_neg_compile_effects_test(neg_fountain_mint_test_context
  "could not convert|no matching function|cannot convert|effects::Test|effects::Init")
crucible_neg_compile_test(neg_fountain_raw_seed
  "cannot convert|no matching function|DetSafe|FountainSeed")
crucible_neg_compile_test(neg_fountain_encode_non_contiguous_owned
  "constraints not satisfied|ByteContiguousBuffer|no matching function")

# WRAP-StorageNbytes-5 (#1022) — compute_storage_nbytes* now accepts
# ExternalTensorMeta = Tagged<const TensorMeta&, source::External>.
# Two HS14 fixtures cover the provenance gate's two directions:
#   * neg_storage_nbytes_raw_tensor_meta — raw TensorMeta cannot bind
#     implicitly to the explicit Tagged boundary.
#   * neg_storage_nbytes_cross_tag — Tagged<const TensorMeta&,
#     source::Sanitized> cannot pass as source::External.
crucible_neg_compile_test(neg_storage_nbytes_raw_tensor_meta
  "no match for|cannot convert|no viable|could not convert|Tagged|ExternalTensorMeta|explicit")
crucible_neg_compile_test(neg_storage_nbytes_cross_tag
  "no match for|cannot convert|no viable|could not convert|Tagged|External|Sanitized")

# WRAP-StorageNbytes-3 (#1020) — deterministic storage-byte
# projections return DetSafe<Pure, Saturated<uint64_t>>.  Raw
# Saturated slots and differently pinned DetSafe slots cannot consume
# the result without an explicit unwrap / retag decision.
crucible_neg_compile_test(neg_storage_nbytes_det_raw_saturated
  "conversion from|could not convert|cannot convert|no viable conversion")
crucible_neg_compile_test(neg_storage_nbytes_det_cross_tier
  "conversion from|could not convert|cannot convert|no viable conversion")

# ─── #884 WRAP-Cipher-1 — non-zero head invariant on advance_head ───
#
# Per #884, ValidCipherHead is safety::Refined<safety::non_zero,
# ContentHash> and gates the parameter to Cipher::advance_head.
# ContentHash{0} is the structural sentinel for "no commit yet" —
# advancing HEAD to it would corrupt the log invariant (a step with
# hash=0 is indistinguishable from "before first commit"), break
# hash_at_step()'s binary search, and lose the federation round-trip
# key for that step.
#
# Two fixtures per HS14 mandate, demonstrating distinct mismatch
# classes:
#   * neg_cipher_head_zero_literal — explicit-literal mismatch
#     (= ContentHash{uint64_t{0}}).  Catches drift where a caller
#     silently passes a zero-init value from a corrupt-source path.
#   * neg_cipher_head_default_construct — NSDMI-default wide miss
#     (= ContentHash{}).  Catches drift where a caller forgets to
#     initialize a local ContentHash variable and forwards it
#     directly to advance_head (whose old signature took bare
#     ContentHash and silently accepted the default).
#
# The two failure modes look identical at the byte level (raw == 0)
# but originate from different source-level mistakes; rejecting both
# makes the gate watertight.
#
# Defense-in-depth: the existing runtime check inside advance_head's
# body is retained.  ValidCipherHead catches the value at the call
# site at compile-time via the typed alias path; the existing body
# guard catches it at runtime if a caller bypasses the typed path.
crucible_neg_compile_test(neg_cipher_head_zero_literal
  "contract|Refined|non_zero|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_cipher_head_default_construct
  "contract|Refined|non_zero|not a constant|non-constant|constexpr")

# ─── #937 WRAP-MerkleDag-1 neg-compile tests ────────────────────────
#
# Per #937, ValidMerkleRoot is safety::Refined<safety::non_zero,
# MerkleHash> and gates the parameter to Tx::commit (defense-in-depth
# alongside computed_merkle_hash() accessor).  MerkleHash{0} is the
# structural sentinel for "subtree never built" — committing it
# would (1) corrupt tx_log's step-by-merkle invariant (a fresh
# Transaction's merkle_root field is also MerkleHash{}), (2) collide
# with TERMINAL leaves in dag_diff equality short-circuits, and (3)
# propagate as a no-op replay key into federation round-trip flows.
#
# Two fixtures per HS14 mandate, demonstrating distinct mismatch
# classes (mirroring the WRAP-Cipher-1 pattern above):
#   * neg_merkle_root_zero_literal — explicit-literal mismatch
#     (= MerkleHash{uint64_t{0}}).  Catches drift where a caller
#     hand-spells a zero MerkleHash literal.
#   * neg_merkle_root_default_construct — NSDMI-default wide miss
#     (= MerkleHash{}).  Catches the realistic mode: a freshly
#     allocated RegionNode whose merkle_hash is default-zero and
#     hasn't yet been populated by recompute_merkle.
#
# Defense-in-depth: the runtime gate `pre(merkle_root.raw() != 0)`
# inside Tx::commit is retained — ValidMerkleRoot catches the value
# at the call site at compile-time via the typed alias path; the
# existing pre clause catches it at runtime if a caller bypasses the
# typed path.
crucible_neg_compile_test(neg_merkle_root_zero_literal
  "contract|Refined|non_zero|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_merkle_root_default_construct
  "contract|Refined|non_zero|not a constant|non-constant|constexpr")

# ─── #897 WRAP-CostModel-3 neg-compile tests ────────────────────────
#
# Per #897, ValidRegsPerThread is
# safety::Refined<safety::bounded_above<uint16_t{255}>, uint16_t> and
# pins KernelConfig::regs_per_thread at the type level.  255 is the
# hardware ceiling on every shipped backend (NVIDIA Hopper / Blackwell
# SM_VERSION ≥ 80, AMD CDNA3+/RDNA3+); the corresponding
# HardwareProfile::max_regs_per_thread defaults to 255 in every preset.
#
# Two fixtures per HS14 mandate, demonstrating distinct mismatch
# classes:
#   * neg_costmodel_regs_per_thread_overflow — boundary edge (256,
#     exactly one past the ceiling).  Catches off-by-one drift in
#     callers that compute regs_per_thread = something + 1.
#   * neg_costmodel_regs_per_thread_max_uint16 — wide miss (UINT16_MAX
#     = 65535).  Catches drift where a caller passes
#     std::numeric_limits<uint16_t>::max(), uninitialised stack
#     garbage that happens to read as max, or a corrupt cost-model
#     snapshot deserialiser.
#
# Defense-in-depth: the existing runtime check inside validate_config
# (constraint C2: cfg.regs_per_thread > hw.max_regs_per_thread) is
# retained.  ValidRegsPerThread catches the value at construction
# time via the typed alias path; the C2 body check catches per-
# hardware ceilings that may be < 255 on future silicon.
crucible_neg_compile_test(neg_costmodel_regs_per_thread_overflow
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_costmodel_regs_per_thread_max_uint16
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# ─── #896 WRAP-CostModel-2 neg-compile tests ────────────────────────
#
# Per #896, ValidWarpSize is
# safety::Refined<safety::all_of<safety::power_of_two,
#                                 safety::bounded_above<uint16_t{128}>>,
#                  uint16_t> and pins HardwareProfile::warp_size at the
# type level.  Every shipped GPU backend (NVIDIA Volta-and-later: 32
# threads, AMD GCN/CDNA/RDNA: 64 threads) emits warps in power-of-two
# widths ≤ 128.
#
# Two fixtures per HS14 mandate, demonstrating distinct mismatch
# classes for the COMPOSITE predicate:
#   * neg_costmodel_warp_size_not_power_of_two — fails the
#     power_of_two predicate (33).  Catches off-by-one drift in
#     callers that compute warp_size = nominal + 1, or stub fence-
#     padding schemes that treat warp_size as an opaque count.
#   * neg_costmodel_warp_size_too_large — fails the bounded_above<128>
#     predicate (256, power-of-two but past the ceiling).  Catches
#     drift where a future architecture is rumoured to widen
#     wavefronts and a deserialiser accepts the rumour, or where a
#     hostile cost-model snapshot inflates warp_size to push every
#     real kernel into a single MAP-Elites cell.
#
# Defense-in-depth: there is no runtime warp_size cap inside
# validate_config — the structural ceiling is a property of the
# silicon, not a per-config knob.  ValidWarpSize catches the value at
# construction time, which is the only meaningful enforcement point.
crucible_neg_compile_test(neg_costmodel_warp_size_not_power_of_two
  "contract|Refined|power_of_two|all_of|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_costmodel_warp_size_too_large
  "contract|Refined|bounded_above|all_of|not a constant|non-constant|constexpr")

# ─── #898 WRAP-CostModel-4 neg-compile tests ────────────────────────
#
# Per #898, ValidUtilization is
# safety::Refined<safety::in_range<0.0f, 1.0f>, float> and pins the
# return types of wave_efficiency() / sm_occupancy() and the symmetric
# receiver fields CostBreakdown::wave_efficiency / occupancy at the
# type level.  Both functions structurally produce values in [0, 1]
# under IEEE 754 round-to-nearest (waves × tpw ≥ elements; min(...)
# ≤ max_threads), so the pre clause never fires under documented
# inputs — it is the soundness witness that catches future caller-
# introduced inversions or sign flips.
#
# Two fixtures per HS14 mandate, demonstrating distinct mismatch
# classes for the InRange<0.0f, 1.0f> predicate:
#   * neg_costmodel_utilization_above_one — fails the upper bound
#     (1.5f).  Catches numerator/denominator inversion in a future
#     wave_efficiency rewrite and off-by-one drift where the formula
#     divides by a rounded-down denominator.
#   * neg_costmodel_utilization_negative — fails the lower bound
#     (-0.5f).  Catches sign flips, signed loop counters wrapping
#     past zero before float conversion, and signed-difference
#     formulations that produce negative ratios.
#
# Defense-in-depth: there is no runtime [0, 1] clamp anywhere in
# evaluate_cost — the structural bound is a property of the formulas
# themselves, not a per-call knob.  ValidUtilization catches the
# value at construction time, which is the only meaningful
# enforcement point.
crucible_neg_compile_test(neg_costmodel_utilization_above_one
  "contract|Refined|in_range|InRange|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_costmodel_utilization_negative
  "contract|Refined|in_range|InRange|not a constant|non-constant|constexpr")

# ─── #902 WRAP-CostModel-8 pure-row neg-compile tests ───────────────
#
# CostModel's evaluator family is pure arithmetic over already-owned
# inputs.  The row-typed overloads require Subrow<CallerRow, Row<>>,
# so any caller that declares IO / Alloc / Bg / Block authority is
# rejected at substitution time instead of silently weakening the pure
# projection into an effect-bearing context.
crucible_neg_compile_test(neg_costmodel_wave_efficiency_io_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_costmodel_sm_occupancy_alloc_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_costmodel_evaluate_cost_bg_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_costmodel_fusion_benefit_block_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")

# ─── ALGEBRA-13/14 lattice neg-compile tests (this session) ─────────
#
# Six tests pinning the structural invariants of the four new lattices
# (HappensBefore + Lifetime + Consistency + Tolerance) and the refined
# Graded::peek_mut gate from the prior MIGRATE-3/MIGRATE-4 work.
#
#   neg_happens_before_zero_n
#       N=0 must trip the primary template's static_assert.
#       [FRAMEWORK-CONTROLLED] — matches the static_assert string.
#
#   neg_happens_before_strong_ordering
#       element_type's operator<=> returns std::partial_ordering, NOT
#       std::strong_ordering — concurrent events MUST be reachable as
#       `unordered`.  Pins the partial-order semantic against a future
#       refactor that defaulted operator<=> on the std::array carrier.
#       [FRAMEWORK-CONTROLLED] — matches the static_assert tag.
#
#   neg_lifetime_at_cross_tier_mixing
#       LifetimeLattice::At<L>::element_type is a NESTED struct whose
#       identity depends on L; cross-tier mixing must be a type-mismatch
#       compile error.  Same structural property holds for the At<>
#       sub-lattices of every chain lattice — pinning one is sufficient.
#       [GCC-WRAPPER-TEXT] — overload-resolution rejection.
#
#   neg_consistency_cross_lattice_mixing,
#   neg_tolerance_cross_lattice_mixing
#       The three new chain-lattice enums (Lifetime / Consistency /
#       Tolerance) MUST remain structurally distinct.  Pinning the
#       Consistency × Lifetime AND Consistency × Tolerance pairs covers
#       both adjacent edges of the triangle (the third edge is implied
#       by enum-class type identity but pinning two pairs catches a
#       partial-collapse refactor).
#       [GCC-WRAPPER-TEXT] — strong-enum type-mismatch rejection.
#
#   neg_graded_peek_mut_on_comonad_nonempty_grade
#       Graded::peek_mut's refined gate `(AbsoluteModality<M> ||
#       std::is_empty_v<grade_type>)` MUST reject the
#       Comonad-over-non-empty-grade case (the dangerous pattern that
#       would let Secret<T> with a runtime classification be silently
#       mutated, defeating the comonad's information-flow contract).
#       Empty-grade arm validated independently by MIGRATE-3 positive
#       tests.
#       [GCC-text + framework] — concept-failure diagnostic + the
#       requires clause is FRAMEWORK-CONTROLLED.
crucible_neg_compile_test(neg_happens_before_zero_n
  "HappensBeforeLattice<0> is forbidden")
crucible_neg_compile_test(neg_happens_before_strong_ordering
  "VectorClockMustBePartialOrder|three_way_comparable")
crucible_neg_compile_test(neg_lifetime_at_cross_tier_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_consistency_cross_lattice_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_tolerance_cross_lattice_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_graded_peek_mut_on_comonad_nonempty_grade
  "constraints not satisfied|AbsoluteModality")

# ─── ALGEBRA-13/14 cross-instantiation neg-compiles (extension) ─────
#
# Six additional neg-compiles broadening coverage of the same lattice
# substrate the prior batch pinned, targeting class-of-bug holes that
# would slip through the originals:
#
#   neg_happens_before_cross_n_mixing
#       HappensBefore<3>::leq with HB<4>::element_type — pins per-N
#       type identity at the static-leq surface.  [GCC-WRAPPER-TEXT]
#
#   neg_happens_before_cross_tag_mixing
#       HappensBefore<4, TagA>::leq with HB<4, TagB>::element_type —
#       pins per-Tag type identity (the WHOLE POINT of the optional
#       Tag parameter).  Without this, a future refactor that elided
#       Tag would silently allow ReplayClock to flow into
#       KernelOrderClock positions.  [GCC-WRAPPER-TEXT]
#
#   neg_happens_before_spaceship_cross_n
#       Cross-N invocation of operator<=> rejected at overload
#       resolution.  Catches a refactor that left static `leq` strict
#       but added a templated converting constructor on element_type
#       that bridged the spaceship across N.  Both surfaces (member
#       leq AND member <=>) must remain N-strict.  [GCC-WRAPPER-TEXT]
#
#   neg_consistency_at_cross_tier_mixing,
#   neg_tolerance_at_cross_tier_mixing
#       Mirrors of neg_lifetime_at_cross_tier_mixing for the OTHER two
#       At<>-based chain lattices.  Together with the original, all
#       three families are independently pinned — a refactor that
#       drifted just ONE family's element_type identity would not
#       slip through.  [GCC-WRAPPER-TEXT]
#
#   neg_graded_peek_mut_on_relative_monad_nonempty_grade
#       Symmetric to the Comonad case: RelativeMonad over non-empty
#       grade also rejected by the refined gate.  Together with the
#       Comonad neg, proves the gate UNIFORMLY rejects any non-Absolute
#       modality over a non-empty grade — the SOUNDNESS of the gate's
#       principle ("mutation is allowed when it can't violate the
#       grade").  [GCC-text + framework]
crucible_neg_compile_test(neg_happens_before_cross_n_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_happens_before_cross_tag_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_happens_before_spaceship_cross_n
  "no match for|invalid operands|no matching function")
crucible_neg_compile_test(neg_consistency_at_cross_tier_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_tolerance_at_cross_tier_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_graded_peek_mut_on_relative_monad_nonempty_grade
  "constraints not satisfied|AbsoluteModality")

# ─── Modality × method matrix completion + cross-mixing closure ─────
#
# Four neg-compiles closing the remaining coverage holes from the prior
# two batches.  Each fills a specific cell that, if the discipline
# slipped, would not be caught by any earlier neg-compile:
#
#   neg_happens_before_spaceship_cross_tag
#       Mirrors neg_happens_before_spaceship_cross_n for the Tag
#       parameter — pins that the spaceship surface respects per-Tag
#       distinction (mirrors the static-leq cross-Tag test for the
#       spaceship surface).  [GCC-WRAPPER-TEXT]
#
#   neg_lifetime_tolerance_cross_lattice_mixing
#       The third (and final) edge of the chain-lattice cross-mixing
#       triangle {Lifetime, Consistency, Tolerance}.  Together with
#       the earlier Consistency×Lifetime and Consistency×Tolerance
#       tests, all three pairs are independently pinned — an enum-
#       collapse refactor on any pair would now be caught by at
#       least two neg-compiles.  [GCC-WRAPPER-TEXT]
#
#   neg_graded_inject_on_absolute,
#   neg_graded_extract_on_relative_monad
#       Complete the modality × method (extract / inject) 2×2
#       rejection grid.  Together with the prior
#       neg_graded_extract_on_absolute and
#       neg_graded_inject_on_comonad, both methods are pinned as
#       rejecting BOTH non-matching modalities.  Without these, a
#       partial loosening (e.g. "extract now also works on
#       RelativeMonad") would slip past the prior neg-compiles.
crucible_neg_compile_test(neg_happens_before_spaceship_cross_tag
  "no match for|invalid operands|no matching function")
crucible_neg_compile_test(neg_lifetime_tolerance_cross_lattice_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_graded_inject_on_absolute
  "constraints not satisfied|RelativeMonadModality")
crucible_neg_compile_test(neg_graded_extract_on_relative_monad
  "constraints not satisfied|ComonadModality")

# ─── FOUND-G03 NumericalTier wrapper neg-compiles (3 fixtures) ──────
#
# Pin the wrapper-level structural invariants of the §4.2.1 worked-
# example wrapper from 28_04_2026_effects.md.  Each fixture catches
# a class of bug a refactor of NumericalTier could introduce:
#
#   neg_numerical_tier_relax_to_tighter
#       relax<TighterTier>() on a looser-pinned wrapper rejected by
#       the requires-clause `ToleranceLattice::leq(LooserTier, T_at)`.
#       Pins the lattice DIRECTION — going up the chain (loose →
#       tight) is forbidden because the looser computation cannot
#       satisfy the stricter contract.  Going down is the only
#       legal motion and is positively-tested in NumericalTier.h's
#       self-test block.
#       [GCC-WRAPPER-TEXT] — requires-clause rejection.
#
#   neg_numerical_tier_cross_tier_assign
#       Cross-tier assignment (NumericalTier<A, T> = NumericalTier<B, T>)
#       rejected by the assignment operator's parameter type.  Pins
#       per-tier identity at the assignment surface — the load-
#       bearing structural property keeping BITEXACT and ULP_FP16
#       slots disjoint.
#       [GCC-WRAPPER-TEXT] — type-mismatch on operator=.
#
#   neg_numerical_tier_cross_tier_swap
#       Cross-tier swap rejected (member AND ADL-friend forms).
#       Pins per-tier identity at the swap surface — without this,
#       a "convenience" cross-tier swap could let recipe-tier
#       labels swap independently of value bytes.
#       [GCC-WRAPPER-TEXT] — swap parameter-type mismatch.
crucible_neg_compile_test(neg_numerical_tier_relax_to_tighter
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_numerical_tier_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_numerical_tier_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_numerical_tier_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G07 Consistency wrapper neg-compiles (4 fixtures) ────────
#
# Mirror of the NumericalTier neg-compile suite for the §4.2.2
# Consistency<Level, T> wrapper.  Each fixture catches the same
# class of bug a refactor of Consistency could introduce.  Pinning
# four fixtures (relax-up / cross-level assign / cross-level swap /
# cross-level equality) per wrapper is the load-bearing structural
# discipline keeping per-Level identity a hard type-system fence.
crucible_neg_compile_test(neg_consistency_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_consistency_cross_level_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_consistency_cross_level_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_consistency_cross_level_equality
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G11 OpaqueLifetime wrapper neg-compiles (4 fixtures) ─────
#
# Mirror of the NumericalTier and Consistency neg-compile suites for
# the §4.2.3 OpaqueLifetime<Scope, T> wrapper.  The relax-to-wider
# fixture is the LOAD-BEARING one: it fences cross-request data
# leaks (a PER_REQUEST state being widened to PER_FLEET would expose
# request-scoped data across requests).
crucible_neg_compile_test(neg_opaque_lifetime_relax_to_wider
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_opaque_lifetime_cross_scope_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_opaque_lifetime_cross_scope_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_opaque_lifetime_cross_scope_equality
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G16 DetSafe wrapper neg-compiles (4 fixtures) ────────────
#
# THE LOAD-BEARING NEG-COMPILES.  The relax-to-stronger fixture is
# the foundation of the 8th-axiom (DetSafe) compile-time fence — it
# proves that a clock-derived value cannot be re-typed as PhiloxRng
# and silently flow into the Cipher replay log.  Without this
# fixture (or the requires-clause it pins), the 8th axiom is no
# better fenced than it is today (CI-only, 12h after a regression).
crucible_neg_compile_test(neg_det_safe_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_det_safe_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_det_safe_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_det_safe_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G16-AUDIT DetSafe LATTICE-level neg-compiles (2 fixtures) ──
#
# Mirror of the per-At<T> + cross-lattice mixing fixtures shipped for
# Tolerance / Consistency / Lifetime.  The wrapper surface
# (relax/assign/swap/equality) is fenced by the 4 fixtures above; THIS
# pair fences the LATTICE substrate underlying the wrapper:
#
#   neg_det_safe_at_cross_tier_mixing
#       DetSafeLattice::At<Pure>::leq cannot accept an At<PhiloxRng>::
#       element_type.  Defends against a refactor extracting a shared
#       singleton_carrier<DetSafeTier> alias above At<T>.
#
#   neg_det_safe_lattice_cross_lattice_mixing
#       DetSafeLattice::leq rejects a non-DetSafeTier value (e.g.,
#       Consistency).  Defends against a "let's collapse the per-
#       lattice enums into a shared Tier:uint8_t" refactor.
#
# Without these, the wrapper's tier-pin survives but the LATTICE could
# silently allow cross-tier or cross-lattice mixing to leak through
# Graded's compose / weaken paths that take element_type values
# directly.
crucible_neg_compile_test(neg_det_safe_at_cross_tier_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_det_safe_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── WRAP-Philox-4 op_key DetSafe gate neg-compiles (2 fixtures) ────
#
# The raw per-op key derivation API is not public.  Production callers
# must route through op_key_det(), carrying DetSafe<Pure> provenance
# until the boundary that explicitly unwraps the key bytes.
crucible_neg_compile_test(neg_philox_raw_op_key
  "op_key|no member|private|not a member")
crucible_neg_compile_test(neg_philox_op_key_det_raw_uint64
  "cannot convert|no viable|conversion from|DetSafe")

# ─── FOUND-G21 HotPath wrapper neg-compiles (6 fixtures: 4+2) ────────
#
# 4 wrapper-surface fixtures (relax / assign / swap / equality):
#   - neg_hot_path_relax_to_stronger    — load-bearing hot-path admission
#   - neg_hot_path_cross_tier_assign    — assignment-surface tier identity
#   - neg_hot_path_cross_tier_swap      — swap-surface tier identity
#   - neg_hot_path_cross_tier_equality  — operator==-surface tier identity
#
# 2 lattice-substrate fixtures:
#   - neg_hot_path_at_cross_tier_mixing       — At<T> nested-struct identity
#   - neg_hot_path_lattice_cross_lattice_mixing — HotPathTier vs DetSafeTier
#
# Together these fence the hot-path discipline at all FOUR surfaces
# the dispatcher's hot-path admission gate (28_04 §6.4) depends on.
crucible_neg_compile_test(neg_hot_path_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_hot_path_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_hot_path_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_hot_path_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_hot_path_at_cross_tier_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_hot_path_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G22 HotPath production-call-site neg-compile ─────────────
#
# Load-bearing rejection at the CONSUMER-FENCE level (the hot-dispatch
# admission gate that TraceRing::try_append_pinned and
# MetaLog::try_append_pinned flow into).  Companion to the FOUND-G21
# wrapper-surface and lattice-substrate fixtures above.
crucible_neg_compile_test(neg_hot_path_cold_at_hot_fence
  "constraints not satisfied|no matching function|associated constraints")

# ─── FOUND-G12 OpaqueLifetime production-call-site neg-compile ─────
#
# Load-bearing rejection at the persistence boundary: an
# OpaqueLifetime<PER_REQUEST, T> value rejected by Cipher::
# commit_per_fleet, whose requires-clause demands satisfies<PER_FLEET>.
# Captures the inferlet cross-request leak bug class (PdaState /
# user state declared PER_REQUEST silently flowed to fleet-durable
# cold storage).  Companion to the FOUND-G11 wrapper-surface
# matrix-fill fixtures.
crucible_neg_compile_test(neg_opaque_lifetime_per_request_at_per_fleet_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G12-AUDIT — matrix-fill of the lifetime → tier rejection set:
#   neg_opaque_lifetime_per_request_at_per_program_fence — secondary
#       leak fence (request data cannot promise program-long persistence;
#       Warm-tier cross-restart leak class).
#   neg_opaque_lifetime_per_program_at_per_fleet_fence — distinct bug
#       class (program-instance scratch replicated across fleet =
#       canary / blue-green contamination on reshard).
crucible_neg_compile_test(neg_opaque_lifetime_per_request_at_per_program_fence
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_opaque_lifetime_per_program_at_per_fleet_fence
  "constraints not satisfied|no matching function|associated constraints")

# ─── FOUND-G32 MemOrder production-call-site neg-compile ───────────
#
# Load-bearing rejection at the consumer-fence level: a SeqCst-emitting
# site (the canonical "make a race go away" bypass) cannot reach an
# Acquire-fence consumer (the AtomicSnapshot::load_mo_pinned reader
# pattern).  Captures the SeqCst-creep hot-path regression class.
crucible_neg_compile_test(neg_mem_order_seqcst_at_acquire_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G32-AUDIT companion fixtures — prove the consumer-fence rejection
# is STRUCTURAL across the lattice, not a SeqCst-special-case.
#
# One-tier rejection: AcqRel does NOT satisfy Acquire (AcqRel is one tier
# below Acquire in the hardware-friendliness lattice).  Captures the
# "RMW-upgrade crept onto an Acquire-load hot path" regression.
crucible_neg_compile_test(neg_mem_order_acqrel_at_acquire_fence
  "constraints not satisfied|no matching function|associated constraints")

# Three-tier-gap rejection: SeqCst is THREE positions below Relaxed in the
# chain (SeqCst ⊑ AcqRel ⊑ Release ⊑ Acquire ⊑ Relaxed).  Strongest
# possible rejection claim in the lattice — proves the requires-clause
# correctly handles maximal cross-tier flow, not just adjacent gaps.
crucible_neg_compile_test(neg_mem_order_seqcst_at_relaxed_fence
  "constraints not satisfied|no matching function|associated constraints")

# ─── FOUND-G04 NumericalTier production-call-site neg-compile ──────
#
# Load-bearing rejection at the consumer-fence level: a RELAXED-tier
# value (origin = ORDERED or UNORDERED recipe via tolerance_of() →
# RELAXED mapping) cannot reach a BITEXACT-fence consumer (the CPU-
# oracle / deterministic-replay / cross-vendor numerics CI pattern
# that FOUND-G04 production sites flow into).  Captures the
# "tolerance downgrade silently flowed into BITEXACT-tier consumer"
# bug class — strongest possible rejection across the 7-tier lattice.
crucible_neg_compile_test(neg_recipe_registry_relaxed_at_bitexact_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G04-AUDIT companion fixtures — span the lattice rejection
# surface across the 7-tier ToleranceLattice:
#   - relaxed_at_bitexact_fence  (FOUND-G04 baseline) — 6-tier-gap (max)
#   - fp64_at_bitexact_fence     (FOUND-G04-AUDIT)    — 1-tier-gap (adjacent)
#   - fp16_at_bitexact_fence     (FOUND-G04-AUDIT)    — 3-tier-gap (mid;
#                                                       matches starter
#                                                       BITEXACT_TC + FP16
#                                                       starter recipes)
# Together: adjacent / mid-range / max gaps prove rejection is
# structural across the entire lattice, not boundary-special-cased.
crucible_neg_compile_test(neg_recipe_registry_fp64_at_bitexact_fence
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_recipe_registry_fp16_at_bitexact_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G78 production-call-site fence — distinct axis-kind from
# the regime-1 by_name_pinned<Tolerance T> overload.  A consumer
# wanting two-axis admission (Tolerance × RecipeFamily) must use
# `by_name_spec(name)` then `.admits(req_tier, req_family)`; reaching
# for `by_name_pinned<RecipeFamily::Kahan>` is a substitution failure
# at the template parameter.  Catches the canonical "wrong-API-for-
# wrong-axis" bug class at the registry boundary.
crucible_neg_compile_test(neg_recipe_registry_pinned_with_family_template_arg
  "could not convert|cannot convert|no matching function|enum class|conversion from")

# FOUND-G78-AUDIT — symmetric misuse: correct two-axis API
# (by_name_spec) called but result-type slot typed as the
# regime-1 single-axis NumericalTier overload's return.
# RecipeSpec<T> ≠ NumericalTier<T_static, T> structurally; the
# std::expected<T, E> conversion is rejected.  Catches the
# "copy-pasted a sibling call site's return type" refactor
# footgun in Forge Phase E.RecipeSelect integration.
crucible_neg_compile_test(neg_recipe_registry_spec_assigned_to_pinned_expected
  "conversion from|could not convert|cannot convert|no viable conversion|non-scalar type")

# FOUND-G78-AUDIT-2 — symmetric companion fence.  Where the
# spec→pinned fence above catches the two-axis-call → one-axis-
# slot drift, this catches the OTHER direction: single-axis
# (by_name_pinned<>) called but result-type slot typed as the
# two-axis (RecipeSpec) return.  Closes the cross-API result-type
# bug class in BOTH directions.
crucible_neg_compile_test(neg_recipe_registry_pinned_assigned_to_spec_expected
  "conversion from|could not convert|cannot convert|no viable conversion|non-scalar type")

# WRAP-RecipeReg-6 (#981) — registry lookup APIs are pure projections.
# by_name/by_hash/entries, the NumericalTier-pinned lookups, and the
# RecipeSpec lookups all reject non-empty caller rows.
crucible_neg_compile_test(neg_recipe_registry_by_name_io_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_registry_by_hash_init_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_registry_entries_bg_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_registry_pinned_alloc_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_registry_spec_block_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")

# WRAP-RecipeReg-3 — entries() span carries source::JsonRegistry
# provenance. Raw spans and differently tagged spans must not reach a
# registry-entry consumer without an explicit trusted unwrap.
crucible_neg_compile_test(neg_recipe_registry_entries_raw_span
  "could not convert|cannot convert|no viable conversion|conversion from")
crucible_neg_compile_test(neg_recipe_registry_entries_cross_tag
  "conversion from|could not convert|cannot convert|no viable conversion")

# ─── FOUND-G26 Wait wrapper neg-compiles (6 fixtures: 4+2) ──────────
#
# 4 wrapper-surface fixtures (mirrors HotPath / DetSafe pattern):
#   - neg_wait_relax_to_stronger    LOAD-BEARING admission rejection
#   - neg_wait_cross_strategy_assign      assignment-surface identity
#   - neg_wait_cross_strategy_swap        swap-surface identity
#   - neg_wait_cross_strategy_equality    operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_wait_at_cross_strategy_mixing       At<S> nested-struct identity
#   - neg_wait_lattice_cross_lattice_mixing   WaitStrategy ≠ HotPathTier
#
# Together these fence the wait-strategy discipline at all FOUR
# surfaces the dispatcher's hot-path waiter admission gate (per
# 28_04 §6.4 + the canonical wrapper-nesting from §4.7) depends on.
crucible_neg_compile_test(neg_wait_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_wait_cross_strategy_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_wait_cross_strategy_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_wait_cross_strategy_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_wait_at_cross_strategy_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_wait_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G31 MemOrder wrapper neg-compiles (6 fixtures: 4+2) ──────
#
# Type-fences CLAUDE.md §VI seq_cst ban at the call site.
#
# 4 wrapper-surface fixtures:
#   - neg_mem_order_relax_to_stronger    LOAD-BEARING seq_cst-ban fence
#   - neg_mem_order_cross_tag_assign     assignment-surface identity
#   - neg_mem_order_cross_tag_swap       swap-surface identity
#   - neg_mem_order_cross_tag_equality   operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_mem_order_at_cross_tag_mixing       At<T> nested-struct identity
#   - neg_mem_order_lattice_cross_lattice_mixing  MemOrderTag ≠ WaitStrategy
#
# Together these fence the seq_cst-ban discipline at all FOUR
# surfaces the dispatcher's hot-path atomic-RMW admission gate
# (per 28_04 §6.4 + CLAUDE.md §VI) depends on.
crucible_neg_compile_test(neg_mem_order_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_mem_order_cross_tag_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_mem_order_cross_tag_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_mem_order_cross_tag_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_mem_order_at_cross_tag_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_mem_order_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G36 Progress wrapper neg-compiles (6 fixtures: 4+2) ──────
#
# FIFTH and FINAL Month-2 chain wrapper.  Type-fences FORGE.md §5
# wall-clock budgets at every Forge phase admission gate.
#
# 4 wrapper-surface fixtures:
#   - neg_progress_relax_to_stronger    LOAD-BEARING Forge wall-clock fence
#   - neg_progress_cross_class_assign   assignment-surface identity
#   - neg_progress_cross_class_swap     swap-surface identity
#   - neg_progress_cross_class_equality operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_progress_at_cross_class_mixing      At<T> nested-struct identity
#   - neg_progress_lattice_cross_lattice_mixing  ProgressClass ≠ MemOrderTag
#
# Together these fence the termination-class discipline at all FOUR
# surfaces the dispatcher's Forge phase admission gate (per
# 28_04 §6.4 + FORGE.md §5) depends on.
crucible_neg_compile_test(neg_progress_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_progress_cross_class_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_progress_cross_class_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_progress_cross_class_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_progress_at_cross_class_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_progress_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G41 AllocClass wrapper neg-compiles (6 fixtures: 4+2) ────
#
# SIXTH chain wrapper (first of Month-2 second-pass).  Type-fences
# CLAUDE.md §VIII no-malloc-on-hot-path discipline at every call site.
#
# 4 wrapper-surface fixtures:
#   - neg_alloc_class_relax_to_stronger    LOAD-BEARING no-malloc fence
#   - neg_alloc_class_cross_tag_assign     assignment-surface identity
#   - neg_alloc_class_cross_tag_swap       swap-surface identity
#   - neg_alloc_class_cross_tag_equality   operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_alloc_class_at_cross_tag_mixing       At<T> nested-struct identity
#   - neg_alloc_class_lattice_cross_lattice_mixing  AllocClassTag ≠ ProgressClass
crucible_neg_compile_test(neg_alloc_class_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_alloc_class_cross_tag_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_alloc_class_cross_tag_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_alloc_class_cross_tag_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_alloc_class_at_cross_tag_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_alloc_class_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# safety::Bits<EnumType> structural-wrapper — HS14 fixtures (#1079):
#   * neg_bits_int_ctor      raw int literal in Bits<E>{42} ctor must
#                            fail (only initializer_list<E> ctor is
#                            public; from_raw is the only raw escape).
#   * neg_bits_cross_enum    Bits<E1> | Bits<E2> mixing two distinct
#                            scoped enums must fail (friend operator|
#                            only matches same-instantiation pairs;
#                            the load-bearing rejection that prevents
#                            silent NodeFlags vs RecipeFlags confusion).
crucible_neg_compile_test(neg_bits_int_ctor
  "no matching function|could not convert|cannot convert|no match for|invalid conversion")
crucible_neg_compile_test(neg_bits_cross_enum
  "no match for|no matching function|cannot convert|invalid operands")

# safety::reflected::* (Reflected.h) — HS14 fixtures (#1089):
#   * neg_reflected_unscoped_enum   bits_to_string<UnscopedEnum>(...)
#                                   must fail because ScopedEnum
#                                   concept rejects unscoped enums
#                                   (load-bearing — unscoped enum
#                                   silently converts from int and
#                                   defeats the typed-bit-field
#                                   discipline Bits<E> exists for).
#   * neg_reflected_lambda_arity    for_each_enumerator<E>([]{}) with
#                                   a zero-arg lambda must fail; the
#                                   runtime callback contract is
#                                   `f(E value, std::string_view name)`
#                                   (load-bearing — pins the
#                                   value+name API away from the old
#                                   NTTP-info sketch that was
#                                   rejected for immediate-escalation
#                                   reasons; see Reflected.h doc).
#   * neg_reflected_null_buffer_with_capacity
#                                   bits_to_string(b, nullptr, cap>0)
#                                   must fail at compile time per the
#                                   pre(cap==0 || out!=nullptr)
#                                   contract (load-bearing — without
#                                   this guard the function silently
#                                   dereferences nullptr at the first
#                                   write_byte call; audit caught the
#                                   missing contract on first read).
crucible_neg_compile_test(neg_reflected_unscoped_enum
  "constraints not satisfied|ScopedEnum|no matching function|cannot convert|use of deleted")
crucible_neg_compile_test(neg_reflected_lambda_arity
  "no match for call|no matching function|wrong number of arguments|too many arguments")
crucible_neg_compile_test(neg_reflected_null_buffer_with_capacity
  "contract predicate is false|false in constant expression|predicate is false|pre")

# safety::Borrowed / BorrowedRef structural-wrappers — HS14 fixtures (#1080):
#   * neg_borrowed_ref_default_ctor  BorrowedRef<T>{} default ctor must
#                                    fail (deleted to forbid the null
#                                    sentinel state — every dereference
#                                    assumes the pointer is bound).
#   * neg_borrowed_cross_source      Borrowed<T, A> = Borrowed<T, B>
#                                    cross-source assignment must fail
#                                    (Source phantom distinguishes the
#                                    instantiations; load-bearing for
#                                    "char* from ExprPool" vs "char*
#                                    from SchemaTable" confusion).
crucible_neg_compile_test(neg_borrowed_ref_default_ctor
  "use of deleted function|deleted|default constructor|no matching")
crucible_neg_compile_test(neg_borrowed_cross_source
  "no match for|no matching function|cannot convert|no known conversion")

# safety::FixedArray<T, N> structural-wrapper — HS14 fixtures (#1081):
#   * neg_fixed_array_zero_sized        FixedArray<T, 0> must fail
#                                       (the (N > 0) concept gate
#                                       guarantees front()/back() are
#                                       always valid AND the index_type
#                                       Refined<bounded_above<N-1>>
#                                       is well-defined for N >= 1).
#   * neg_fixed_array_in_place_wrong_arity   FixedArray<int, 8>(in_place,
#                                       3 args) must fail (the
#                                       requires(sizeof...(Args) == N)
#                                       gate prevents partially-filled
#                                       arrays slipping through with
#                                       uninitialized tail — defeats
#                                       the NSDMI default-zero
#                                       guarantee).
#   * neg_fixed_array_at_template_oob   FixedArray<int, 8>::at<99>()
#                                       must fail at TU-scope (the
#                                       template's (I < N) requires-
#                                       clause makes compile-time-known
#                                       OOB access ill-formed — distinct
#                                       from the in-self-test
#                                       can_compile_at concept which
#                                       only exercises the gate inside
#                                       a single consteval evaluation).
crucible_neg_compile_test(neg_fixed_array_zero_sized
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_fixed_array_in_place_wrong_arity
  "no matching function|constraints not satisfied|associated constraints|cannot convert")
crucible_neg_compile_test(neg_fixed_array_at_template_oob
  "no matching function|constraints not satisfied|associated constraints")

# #1057 WRAP-TraceRing-5: Entry::scalar_values raw int64_t[5] →
# safety::FixedArray<int64_t, 5>.  HS14 mandates ≥2 negative-compile
# fixtures for the new soundness gate (the typed inline-scalar buffer):
#   * neg_tracering_scalar_values_at_oob              ACCESS-side gate —
#                                                     compile-time-known
#                                                     index ≥ N rejected
#                                                     by at<I>()'s
#                                                     `requires (I < N)`.
#   * neg_tracering_scalar_values_in_place_wrong_arity CONSTRUCTION-side
#                                                     gate — variadic
#                                                     ctor's
#                                                     `sizeof...(Args)
#                                                     == N` rejection
#                                                     on a 6-arg
#                                                     brace-init.
crucible_neg_compile_test(neg_tracering_scalar_values_at_oob
  "no matching function|constraints not satisfied|associated constraints")
crucible_neg_compile_test(neg_tracering_scalar_values_in_place_wrong_arity
  "no matching function|constraints not satisfied|associated constraints|cannot convert|too many initializers")

# WRAP-Foundation 2026-05-09: length-coupled raw-array primitives.
# Three new safety wrappers (AlignedBuffer, HugePageBuffer,
# SwissTableBuffer), each with HS14 fixtures (≥2 per primitive)
# covering both COPY-CTOR-side (init-time copy) and COPY-ASSIGN-side
# (assignment-time copy) gates of the move-only RAII discipline.
crucible_neg_compile_test(neg_aligned_buffer_copy
  "use of deleted function|deleted function|move-only")
crucible_neg_compile_test(neg_aligned_buffer_use_after_move
  "use of deleted function|deleted function|move-only")
crucible_neg_compile_test(neg_huge_page_buffer_copy
  "use of deleted function|deleted function|move-only")
crucible_neg_compile_test(neg_huge_page_buffer_use_after_move
  "use of deleted function|deleted function|move-only")
crucible_neg_compile_test(neg_swiss_table_buffer_copy
  "use of deleted function|deleted function|move-only")
crucible_neg_compile_test(neg_swiss_table_buffer_use_after_move
  "use of deleted function|deleted function|move-only")

# WRAP-SwissTab-2 (#1024) — BitMask stores a Refined group-width
# bitset.  The mask is bounded by the numeric ceiling
# ((1 << kGroupWidth) - 1), not by kGroupWidth itself: a valid
# kGroupWidth=16 mask can be 0xF0F0.  The two fixtures pin the
# boundary edge and a wide miss on the default AVX2/SSE/portable
# widths used by the local preset.
crucible_neg_compile_test(neg_swiss_bitmask_one_past_group_mask
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_swiss_bitmask_wide_miss
  "contract|Refined|bounded_above|not a constant|non-constant|constexpr")

# WRAP-SwissTab-6 (#1028) — kGroupWidth is a
# safety::PowerOfTwo<size_t> rather than a raw size_t.  The fixtures
# cover both structural mismatch classes for the predicate.
crucible_neg_compile_test(neg_swiss_group_width_zero
  "contract|Refined|power_of_two|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_swiss_group_width_not_power_of_two
  "contract|Refined|power_of_two|not a constant|non-constant|constexpr")

# WRAP-ExprPool-4 (#918) — int_cache_ access is routed through
# IntCacheLiteral = Refined<in_range<-128, 127>, int64_t>.  Two HS14
# fixtures pin both ends of the closed interval:
#   * neg_expr_pool_int_cache_literal_below_low — -129 is one below
#     the cache's lower endpoint.
#   * neg_expr_pool_int_cache_literal_above_high — 128 is one above
#     the cache's upper endpoint.
crucible_neg_compile_test(neg_expr_pool_int_cache_literal_below_low
  "contract|Refined|in_range|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_expr_pool_int_cache_literal_above_high
  "contract|Refined|in_range|not a constant|non-constant|constexpr")

# WRAP-ExprPool-2 (#916) — capacity_ is a PowerOfTwo<size_t>
# and intern_count_ is a Monotonic<size_t>.  The fixtures pin the two
# load-bearing absences: non-pow2 capacities cannot be constructed,
# and the monotonic intern counter cannot be overwritten with a raw
# integer.
crucible_neg_compile_test(neg_expr_pool_capacity_not_power_of_two
  "contract|Refined|power_of_two|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_expr_pool_intern_count_assign_raw
  "no match for|no known conversion")

# WRAP-ExprPool-5 (#919) — make() returns a DetSafe<Pure,
# Tagged<const Expr*, source::Interned>>.  Raw Expr* and same-tier
# cross-source values must fail at the consumer boundary.
crucible_neg_compile_test(neg_expr_pool_make_raw_expr
  "no match|no known conversion|cannot convert|could not convert")
crucible_neg_compile_test(neg_expr_pool_make_cross_tag
  "no match|no known conversion|cannot convert|could not convert")

# WRAP-RecipePool-2 (#971) — capacity_ is a PowerOfTwo<uint32_t>
# and size_ is a Monotonic<uint32_t>.  The fixtures pin the two
# load-bearing absences: non-pow2 capacities cannot be constructed,
# and the monotonic size counter cannot be overwritten with a raw
# integer.
crucible_neg_compile_test(neg_recipe_pool_capacity_not_power_of_two
  "contract|Refined|power_of_two|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_recipe_pool_size_assign_raw
  "no match for|no known conversion")

# WRAP-RecipePool-3 (#972) — arena_ is a BorrowedRef<Arena>.
# Raw Arena* and wrong-owner BorrowedRef<T> must fail at the
# constructor boundary.
crucible_neg_compile_test(neg_recipe_pool_arena_raw_pointer
  "no matching function|no known conversion|cannot convert")
crucible_neg_compile_test(neg_recipe_pool_arena_wrong_borrow
  "no matching function|no known conversion|cannot convert")

# WRAP-RecipePool-5 (#974) — RecipePool construction is init-phase
# work. The constructor defaults to Row<Init>; explicit pure,
# Alloc-only, and Bg-only caller rows fail the Subrow gate.
crucible_neg_compile_test(neg_recipe_pool_ctor_pure_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_pool_ctor_alloc_only_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")
crucible_neg_compile_test(neg_recipe_pool_ctor_bg_only_row
  "associated constraints are not satisfied|constraints not satisfied|Subrow|no matching function")

# WRAP-RecipeReg-5 (#980) — RecipeRegistry seeds from
# BorrowedRef<RecipePool>. Raw RecipePool* and wrong-owner borrowed
# references must fail at the constructor boundary.
crucible_neg_compile_test(neg_recipe_registry_pool_raw_pointer
  "no matching function|no known conversion|cannot convert")
crucible_neg_compile_test(neg_recipe_registry_pool_wrong_borrow
  "no matching function|no known conversion|cannot convert")

# GAPS-174 — forge/recipes/Network.h network-kernel recipe constraints.
#   * neg_network_recipe_strict_sharp: BITEXACT_STRICT cannot enter SHARP.
#   * neg_network_recipe_non_commutative_sharp: SHARP requires commutativity.
#   * neg_network_recipe_bitexact_lossy: BITEXACT tiers reject lossy Top-K.
#   * neg_network_recipe_recursive_non_power2: recursive halving-doubling
#                                             requires power-of-two peers.
#   * neg_network_recipe_raw_constraints: consumers require registry provenance.
crucible_neg_compile_test(neg_network_recipe_strict_sharp
  "constraints not satisfied|NetworkRecipeEligible|BITEXACT_STRICT|Sharp")
crucible_neg_compile_test(neg_network_recipe_non_commutative_sharp
  "constraints not satisfied|NetworkRecipeEligible|commutative|Sharp")
crucible_neg_compile_test(neg_network_recipe_bitexact_lossy
  "constraints not satisfied|NetworkRecipeEligible|BITEXACT_TC|TopK")
crucible_neg_compile_test(neg_network_recipe_recursive_non_power2
  "constraints not satisfied|NetworkRecipeEligible|RecursiveHalvingDoubling|power")
crucible_neg_compile_test(neg_network_recipe_raw_constraints
  "could not convert|no matching function|DeclaredNetworkRecipeConstraints|Tagged")

# GAPS-167 — forge/Ir001/Comm.h comm-through-IR substrate.
#   * neg_ir001_comm_raw_node: Forge visitors require source::Ir001 provenance.
#   * neg_ir001_comm_send_as_collective: point-to-point ops are not collectives.
#   * neg_ir001_comm_zero_participants: participant counts are positive.
crucible_neg_compile_test(neg_ir001_comm_raw_node
  "could not convert|no matching function|DeclaredIr001Node|Tagged")
crucible_neg_compile_test(neg_ir001_comm_send_as_collective
  "constraints not satisfied|Ir001CollectiveKind|SendAsync")
crucible_neg_compile_test(neg_ir001_comm_zero_participants
  "contract|precondition|in_range|positive|bounded")

# GAPS-168 WIP — forge/_wip/Phases/Comm.h vendor-neutral comm phase sketch.
#   * neg_forge_comm_phase_raw_compute: fusion admission requires source::Ir001.
#   * neg_forge_comm_phase_send_as_collective: ReduceOnRecv requires collective
#     communication, not point-to-point Send.
#   * neg_forge_comm_phase_oversubscribed: combined resource row must FitCog.
#   * neg_forge_comm_phase_strict_compress: BITEXACT_STRICT rejects lossy
#     compression at the compile-time recipe gate.
crucible_neg_compile_test(neg_forge_comm_phase_raw_compute
  "could not convert|no matching function|DeclaredIr001Node|Tagged|source::Ir001")
crucible_neg_compile_test(neg_forge_comm_phase_send_as_collective
  "static assertion failed|constraints not satisfied|CommFusionEligible|ReduceOnRecv|Ir001CollectiveKind")
crucible_neg_compile_test(neg_forge_comm_phase_oversubscribed
  "static assertion failed|constraints not satisfied|CommFusionEligible|FitsCog|999")
crucible_neg_compile_test(neg_forge_comm_phase_strict_compress
  "constraints not satisfied|CommFusionRecipeAllowed|BITEXACT_STRICT|CompressBeforeSend")

# GAPS-169 WIP — mimic/_wip/network/Backend.h per-vendor backend sketch.
#   * neg_mimic_network_backend_raw_node: backend planning requires admitted
#     source::Ir001 nodes, not raw comm descriptors.
#   * neg_mimic_network_backend_compute_node: backend planning accepts only
#     network-visible point-to-point or collective IR001 nodes.
#   * neg_mimic_network_backend_cross_vendor: backend artifacts are source-tagged
#     per vendor and cannot cross vendor launch boundaries.
#   * neg_mimic_network_backend_wrong_cog: vendor/cog compatibility is a
#     compile-time gate.
crucible_neg_compile_test(neg_mimic_network_backend_raw_node
  "no matching function|could not convert|DeclaredIr001Node|Tagged|source::Ir001")
crucible_neg_compile_test(neg_mimic_network_backend_compute_node
  "static assertion failed|NetworkKernelNode|ir001_network_kernel_kind")
crucible_neg_compile_test(neg_mimic_network_backend_cross_vendor
  "could not convert|no known conversion|AmNetwork|NvNetwork|Tagged")
crucible_neg_compile_test(neg_mimic_network_backend_wrong_cog
  "static assertion failed|BackendAcceptsCog|Mellanox|Gpu")

# WRAP-RE-3 (#995) — ReplayEngine::init consumes
# BorrowedRef<const PoolAllocator>. Raw PoolAllocator* and wrong-owner
# borrowed references must fail at the init boundary.
crucible_neg_compile_test(neg_replay_engine_pool_raw_pointer
  "no matching function|no known conversion|cannot convert")
crucible_neg_compile_test(neg_replay_engine_pool_wrong_borrow
  "no matching function|no known conversion|cannot convert")

# safety::Saturated<T> structural-wrapper — HS14 fixtures (#1084 piece):
#   * neg_saturated_non_arithmetic   Saturated<MyStruct> must fail
#                                    (the std::is_arithmetic_v<T>
#                                    requires-clause prevents non-
#                                    arithmetic instantiations where
#                                    saturating arithmetic + clamped
#                                    semantics are meaningless).
#   * neg_saturated_implicit_to_t    `unsigned long x = sat;` must
#                                    fail (the operator T() is
#                                    explicit — prevents accidentally
#                                    dropping the was_clamped() signal
#                                    through implicit conversion).
crucible_neg_compile_test(neg_saturated_non_arithmetic
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_saturated_implicit_to_t
  "cannot convert|no matching function|conversion from|invalid conversion")

# safety::Cyclic<T, N> structural ring-cursor — HS14 fixtures (#1084 piece):
#   * neg_cyclic_non_power_of_two   Cyclic<uint32_t, 6> must fail — the
#                                   `(N & (N-1)) == 0` requires-clause is
#                                   what makes `counter & (N-1)` == `% N`;
#                                   non-power-of-two N silently mis-slots.
#   * neg_cyclic_signed_counter     Cyclic<int32_t, 8> must fail — the
#                                   std::unsigned_integral<T> clause; the
#                                   free-running `++counter` wrap is only
#                                   well-defined (DetSafe) for unsigned T.
crucible_neg_compile_test(neg_cyclic_non_power_of_two
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_cyclic_signed_counter
  "constraints not satisfied|associated constraints|no matching|no type named")

# safety::CyclicBuffer<T, N> structural MRU ring — HS14 fixtures (#1084
# piece).  Two DISTINCT rejection mechanisms:
#   * neg_cyclic_buffer_non_power_of_two   CLASS-template requires fires —
#                                          CyclicBuffer<int, 6> has no valid
#                                          specialization (N must be 2^k so
#                                          the composed Cyclic mask works).
#   * neg_cyclic_buffer_push_non_assignable  MEMBER-function requires fires
#                                          — the class instantiates (claim()
#                                          works) but both push overloads
#                                          drop out for a non-assignable T.
crucible_neg_compile_test(neg_cyclic_buffer_non_power_of_two
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_cyclic_buffer_push_non_assignable
  "constraints not satisfied|associated constraints|no matching|no type named")

# safety::WeakRef<T> structural nullable non-owning ref — HS14 fixtures
# (#1084 piece).  Two DISTINCT rejection mechanisms:
#   * neg_weakref_reference_type   CLASS-template requires fires —
#                                  is_object_v<int&> is false, so
#                                  WeakRef<int&> has no specialization
#                                  (also rejects void / function T).
#   * neg_weakref_implicit_raw_ptr CONVERSION fires — WeakRef ships no
#                                  implicit operator T*, so `int* p = wr`
#                                  has no viable conversion (TypeSafe:
#                                  the named escapes are try_get/from_raw).
crucible_neg_compile_test(neg_weakref_reference_type
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_weakref_implicit_raw_ptr
  "cannot convert|no viable conversion|invalid conversion|no match")

# WRAP-Saturate-3 (#1001) — *_sat_det returns
# DetSafe<Pure, Saturated<T>>. Raw Saturated consumers and implicit
# cross-tier DetSafe consumers must fail.
crucible_neg_compile_test(neg_saturate_det_raw_result_escape
  "cannot convert|conversion from|no matching function|non-scalar type")
crucible_neg_compile_test(neg_saturate_det_cross_tier_assignment
  "cannot convert|conversion from|no matching function|non-scalar type")

# WRAP-Saturate-4 (#1002) — memory-resident counter helpers keep
# read-only projections separate from mutating updates and preserve
# the Saturated<T> clamp signal.
crucible_neg_compile_test(neg_saturate_into_const_dest
  "no matching function|binding reference|discards qualifiers|cannot convert")
crucible_neg_compile_test(neg_saturate_from_raw_escape
  "cannot convert|conversion from|no matching function|invalid conversion")

# FairSharedPermissionPool fairness wrapper — audit fixtures (#861-followup):
#   * BurstLimit=0 fires the static_assert ("must be > 0").
#   * Copying the Pinned wrapper fires the deleted-copy diagnostic.
crucible_neg_compile_test(neg_fair_pool_burst_zero
  "BurstLimit must be > 0|static assertion failed")
crucible_neg_compile_test(neg_fair_pool_copy_attempt
  "use of deleted function|deleted|FairSharedPermissionPool")

# mint_crash_watched_session — Universal Mint Pattern (CLAUDE.md §XXI)
# soundness fixtures (HS14):
#   * non-handle first argument fails substitution (parameter-type gate).
#   * missing PeerTag template arg fails resolution (non-deduced ctx).
crucible_neg_compile_test(neg_mint_crash_watched_session_non_handle
  "no matching function|could not match|cannot convert")
crucible_neg_compile_test(neg_mint_crash_watched_session_missing_peer_tag
  "no matching function|could not (deduce|infer)|missing template argument")
crucible_neg_compile_test(neg_crash_watched_no_survivors
  "CrashWatchedHandle requires mint_permission_inherit survivors")
crucible_neg_compile_test(neg_crash_event_missing_survivor
  "CrashEvent survivor list must match survivors_t")
crucible_neg_compile_test(neg_crash_watched_ps_mismatch
  "no matching function|cannot convert")
crucible_neg_compile_test(neg_crash_return_resource_mismatch
  "wrap_crash_return recovered resource type must match inner resource_type")

# FOUND-G42 — production-call-site fence: a hot-path consumer
# requiring AllocClass<Pool> rejects an AllocClass<Heap> source.
# This is the load-bearing rejection for the no-malloc-on-hot-path
# discipline at the production call boundary (slot_ptr_pinned ⇄
# consumer template).  Without this fence, jemalloc allocations
# (50-200 ns setup) could silently flow into TraceRing/MetaLog/
# Vigil hot-path sites budgeted at 2-3 ns per call.
crucible_neg_compile_test(neg_alloc_class_heap_at_pool_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G62 — production-call-site fence: a NoThrow steady-state
# consumer rejects a Throw-classified value.  Models the
# OneShotFlag::peek_nothrow ⇄ consumer pattern at the boundary.
# Without this fence, signal-side values would silently flow into
# hot-path observation gates that require the "this call NEVER
# fails" guarantee.
crucible_neg_compile_test(neg_crash_throw_at_nothrow_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G62-AUDIT — additional crash-class boundary fences:
# Abort rejected at ErrorReturn (recovery-aware boundary); admitting
# a value from a Keeper init path that may have called
# crucible_abort() into a recovery-handler branch would defeat the
# recovery code's right-to-run invariant.
crucible_neg_compile_test(neg_crash_abort_at_error_return_fence
  "constraints not satisfied|no matching function|associated constraints")

# ErrorReturn rejected at NoThrow (steady-state hot-path observer);
# admitting an error-returning value into a shape-budgeted observer
# would silently break the "this call NEVER fails" guarantee that
# hot-path code depends on.
crucible_neg_compile_test(neg_crash_error_return_at_nothrow_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G27 — production-call-site fence: a hot-path consumer
# requiring Wait<SpinPause> rejects a Wait<Park> source.  This is
# the load-bearing rejection for the no-park-on-hot-path discipline
# (CLAUDE.md §IX).  Park's ~1-5 μs futex wakeup latency is two
# orders of magnitude above the per-call shape budget.
crucible_neg_compile_test(neg_wait_park_at_spin_pause_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G47 — production-call-site fence: a hot-tier reincarnation
# consumer requiring CipherTier::satisfies<Hot> rejects a
# CipherTier<Cold> source.  This is the load-bearing rejection for
# the Keeper hot-tier reshard gate (CRUCIBLE.md §L14).  Cold tier
# carries minutes-of-S3-GET latency, never appropriate at single-
# node-failure reincarnation where the recovery path has
# microseconds-to-milliseconds budget at most.
crucible_neg_compile_test(neg_cipher_tier_cold_at_hot_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G47-AUDIT — matrix-fill: Warm rejected at Hot fence (one-tier-
# off refactor bug — NVMe-backed value misused at the RAM-replicated
# hot path, silently degrading recovery latency by 100-1000×).
crucible_neg_compile_test(neg_cipher_tier_warm_at_hot_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G47-AUDIT — matrix-fill: Cold rejected at Warm fence (freshness-
# guarantee violation — replay-engine S3 archive misused at a per-
# iteration publish path, breaking runtime policy engine
# integrity).
crucible_neg_compile_test(neg_cipher_tier_cold_at_warm_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G52 — production-call-site fence: a hot-dispatch consumer
# requiring ResidencyHeat::satisfies<Hot> rejects a
# ResidencyHeat<Cold> source.  This is the load-bearing rejection
# for the per-op recording site / Vigil hot-path budget (CRUCIBLE.md
# §L4 + §L2).  Cold-tier (L3 compiled-bytes archive, S3-backed) has
# hundreds-of-ns to ms latency, two-to-five orders of magnitude
# above the ~5 ns per-call shape budget.
crucible_neg_compile_test(neg_residency_heat_cold_at_hot_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G52-AUDIT — matrix-fill: Warm rejected at Hot fence (one-tier-
# off refactor bug — L2-fetched kernel pointer misused at the
# foreground L1 dispatch path, paying tens-of-ns L2 access penalty
# per call against a ~5 ns budget).
crucible_neg_compile_test(neg_residency_heat_warm_at_hot_fence
  "constraints not satisfied|no matching function|associated constraints")

# FOUND-G52-AUDIT — matrix-fill: Cold rejected at Warm fence
# (federation freshness violation — L3 archive entry misused at L2
# publish path, broadcasting stale entries into per-vendor-family
# federation and breaking Forge recompile-decision integrity).
crucible_neg_compile_test(neg_residency_heat_cold_at_warm_fence
  "constraints not satisfied|no matching function|associated constraints")

# ─── FOUND-D01 — signature_traits<FnPtr> param-index overflow ─────
#
# The `requires (I < arity)` clause on signature_traits<FnPtr>::
# param_type_t<I> rejects out-of-range indices with a clean
# constraint-failure diagnostic rather than letting the splice fail
# deep inside std::define_static_array / std::span::operator[].
crucible_neg_compile_test(neg_signature_traits_param_index_overflow
  "constraints not satisfied|template constraint failure|associated constraints")

# Boundary case: param_type_t<&nullary, 0> on arity-0 function.  Off-
# by-one in the requires clause would slip through here.
crucible_neg_compile_test(neg_signature_traits_param_index_on_nullary
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D03 — owned_region_value_t<T> on non-OwnedRegion T ─────
#
# The `requires is_owned_region_v<T>` constraint on
# owned_region_value_t / owned_region_tag_t rejects non-OwnedRegion
# arguments at the alias declaration rather than producing `void`
# from the primary template's `value_type = void;`.
crucible_neg_compile_test(neg_is_owned_region_value_t_on_non_region
  "constraints not satisfied|template constraint failure|associated constraints")

# Sister fixture — owned_region_tag_t<void> independently locks in the
# constraint on the tag-extractor alias.
crucible_neg_compile_test(neg_is_owned_region_tag_t_on_non_region
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D04 — permission_tag_t<T> on non-Permission T ──────────
#
# The `requires is_permission_v<T>` constraint on permission_tag_t
# rejects non-Permission arguments at the alias declaration.  The
# fixture passes a SharedPermission<Tag> — guards against the
# fractional ↔ linear confusion (a refactor that loosens the gate
# would let SharedPermission slip through, breaking the linear/
# fractional CSL discipline at the dispatcher boundary).
crucible_neg_compile_test(neg_is_permission_tag_t_on_non_permission
  "constraints not satisfied|template constraint failure|associated constraints")

# Sister fixture — shared_permission_tag_t<Permission<Tag>> exercises
# the OTHER direction of the linear/fractional discrimination so the
# two requires-clauses stay independently load-bearing.
crucible_neg_compile_test(neg_is_shared_permission_tag_t_on_non_shared_permission
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D07-AUDIT — reduce_into_*_t<T> on non-reduce_into T ────
#
# The `requires is_reduce_into_v<T>` constraint on
# reduce_into_accumulator_t / reduce_into_reducer_t rejects
# non-reduce_into arguments at the alias declaration.  Without the
# constraint, the alias would resolve to `void` from the primary
# template's `accumulator_type = void; reducer_type = void;`,
# silently propagating that void into downstream type machinery
# (e.g. the FOUND-D14 Reduction concept).  Two fixtures, one per
# extractor alias, keep both requires-clauses independently
# load-bearing.
crucible_neg_compile_test(neg_reduce_into_accumulator_t_on_non_reduce_into
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_reduce_into_reducer_t_on_non_reduce_into
  "constraints not satisfied|template constraint failure|associated constraints")

# Move-only fence on the reduce_into wrapper itself.  Mirrors
# neg_permission_copy_attempt — the reduce_into accumulator is unique
# state (linear semantics); copying would defeat parallel-reduce
# linearity.  Catches a future edit that accidentally relaxes the
# `reduce_into(reduce_into const&) = delete` line.
crucible_neg_compile_test(neg_reduce_into_copy_attempt
  "use of deleted function|deleted|copy")

# ─── FOUND-D09 — universal GradedWrapper extractors ──────────────
#
# Each of the FIVE universal extractors (value_type_of_t /
# lattice_of_t / grade_of_t / modality_of_v / graded_type_of_t)
# carries its `requires IsGradedWrapper<W>` constraint independently.
# Five fixtures pin the constraint propagation — one per extractor —
# probing both bare-primitive (int) and concept-cheat (lookalike
# struct missing graded_type / lattice_type / modality) inputs.
crucible_neg_compile_test(neg_grade_of_t_on_non_graded
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_lattice_of_t_on_non_graded
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_value_type_of_t_on_non_graded
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_modality_of_v_on_non_graded
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_graded_type_of_t_on_non_graded
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D12 — UnaryTransform constrained extractors ───────────
#
# Each of the THREE constrained extractors on UnaryTransform.h
# (unary_transform_input_tag_t / input_value_t / output_tag_t)
# carries its `requires UnaryTransform<FnPtr>` clause independently.
# Three fixtures pin the constraint propagation, each probing a
# different shape-recognition failure mode (non-region parameter /
# arity mismatch / lvalue-ref instead of rvalue-ref).  Together
# they cover the three orthogonal failure axes of the UnaryTransform
# concept.
crucible_neg_compile_test(neg_unary_transform_input_tag_t_on_non_unary
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_unary_transform_input_value_t_on_non_unary
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_unary_transform_output_tag_t_on_non_unary
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D13 — BinaryTransform constrained extractors ──────────
#
# Each of the SIX constrained extractors on BinaryTransform.h
# (binary_transform_lhs_tag_t / rhs_tag_t / lhs_value_t / rhs_value_t /
# output_tag_t / has_same_tag_v) carries its
# `requires BinaryTransform<FnPtr>` clause independently.  The
# variable-template `has_same_tag_v` requires its own fixture
# because the constraint propagation surface for variable templates
# differs structurally from type aliases.  Four fixtures pin the
# constraints; the lhs_value_t and rhs_value_t aliases share their
# failure mode with the rhs_tag_t fixture (arity mismatch) so a
# single fixture covers them — the requires clause is identical.
crucible_neg_compile_test(neg_binary_transform_lhs_tag_t_on_non_binary
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_binary_transform_rhs_tag_t_on_non_binary
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_binary_transform_output_tag_t_on_non_binary
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_binary_transform_has_same_tag_v_on_non_binary
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D14 — Reduction constrained extractors ────────────────
#
# Four constrained extractors on Reduction.h
# (reduction_input_tag_t / input_value_t / accumulator_t / reducer_t)
# each carry their `requires Reduction<FnPtr>` clause independently.
# Without the constraint, the aliases would chain through
# owned_region_*_t / reduce_into_*_t and surface a confusing
# inner-layer diagnostic naming the harvested mismatch instead of the
# parameter-shape mismatch.  Four fixtures, one per extractor, keep
# all four requires-clauses independently load-bearing.  Each fixture
# witnesses a distinct shape-recognition failure mode so a refactor
# that breaks one alias doesn't accidentally pass the others.
crucible_neg_compile_test(neg_reduction_input_tag_t_on_non_reduction
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_reduction_input_value_t_on_non_reduction
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_reduction_accumulator_t_on_non_reduction
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_reduction_reducer_t_on_non_reduction
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-F04-AUDIT — parallel_reduce_views static_assert fences ─
#
# Three body static_asserts in safety/Workload.h's
# `parallel_reduce_views<N, R>` definition each get their own
# neg-compile fixture.  Without these, a refactor that accidentally
# weakened ANY of the three assertions (e.g. dropped `static_assert(N>0)`
# on a "looks like a redundant check" review pass, or relaxed the
# noexcept clauses to plain is_invocable_v) would compile cleanly and
# only manifest as a runtime std::terminate inside a jthread body.
crucible_neg_compile_test(neg_parallel_reduce_views_n_zero
  "parallel_reduce_views<N, R> requires N > 0|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_reduce_views_throwing_mapper
  "Mapper must be noexcept-invocable|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_reduce_views_throwing_reducer
  "Reducer must be noexcept-invocable|static_assert|static assertion")

# ─── FOUND-F01-AUDIT — parallel_for_views static_assert fences ───
#
# Mirrors the F04-AUDIT pattern for the sibling primitive in
# safety/Workload.h.  Two body static_asserts in `parallel_for_views<N>`
# (N > 0; body noexcept-invocable) each get a neg-compile fixture so a
# refactor that drops or weakens either assertion is caught at build
# time rather than at runtime.
crucible_neg_compile_test(neg_parallel_for_views_n_zero
  "parallel_for_views<N> requires N > 0|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_for_views_throwing_body
  "body must be noexcept-invocable|static_assert|static assertion")

# ─── FOUND-F01-AUDIT-3 / F04-AUDIT-2 — copy-constructibility fences ──
#
# parallel_for_views<N> and parallel_reduce_views<N, R> capture body /
# mapper BY VALUE into per-worker jthread lambdas (one copy per worker
# in the N >= 2 branch).  Without an API-boundary fence, a move-only
# callable fails deep inside std::jthread machinery with a "use of
# deleted function" diagnostic.  These fixtures pin the boundary
# static_asserts so the diagnostic stays at the API boundary even
# under future refactor.
crucible_neg_compile_test(neg_parallel_for_views_move_only_body
  "must be CopyConstructible|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_reduce_views_move_only_mapper
  "must be CopyConstructible|static_assert|static assertion")

# ─── FOUND-F02 — parallel_apply_pair static_assert fences ────────────
#
# Mirrors the F01 / F04 patterns for the pair-iterated primitive in
# safety/Workload.h.  Three body static_asserts in
# `parallel_apply_pair<N>` (N > 0; body noexcept-invocable when
# N >= 1; body CopyConstructible when N >= 2) each get a neg-compile
# fixture so a refactor that drops or weakens any assertion is caught
# at build time rather than at runtime / in deep std::jthread template
# instantiation diagnostics.
crucible_neg_compile_test(neg_parallel_apply_pair_n_zero
  "parallel_apply_pair<N> requires N > 0|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_apply_pair_throwing_body
  "body must be noexcept-invocable|static_assert|static assertion")

crucible_neg_compile_test(neg_parallel_apply_pair_move_only_body
  "must be CopyConstructible|static_assert|static assertion")

# FOUND-F02-AUDIT — wrong body shape (body uses one tag for both
# parameter positions when the regions have different tags).
crucible_neg_compile_test(neg_parallel_apply_pair_wrong_body_shape
  "body must be noexcept-invocable|static_assert|static assertion")

# ─── FOUND-D15 ProducerEndpoint constrained-extractor fixtures ──────
#
# Pins the four constrained extractors on ProducerEndpoint.h:
#   - producer_endpoint_handle_value_t
#   - producer_endpoint_region_tag_t
#   - producer_endpoint_region_value_t
#   - producer_endpoint_value_consistent_v
# Each is constrained on `requires ProducerEndpoint<FnPtr>`; without
# the constraint, the alias would chain through `param_type_t<FnPtr,
# 0>` followed by `producer_handle_value_t` (or owned_region_*_t) and
# emit a diagnostic far from the actual mistake.  These fixtures
# guarantee the constraint stays in place and the diagnostic lands
# at the producer-endpoint-shape-recognition boundary.
crucible_neg_compile_test(neg_producer_endpoint_handle_value_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_producer_endpoint_region_tag_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_producer_endpoint_region_value_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_producer_endpoint_value_consistent_v_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D16 ConsumerEndpoint constrained-extractor fixtures ──────
#
# Symmetric to D15 — pins the four constrained extractors on
# ConsumerEndpoint.h: handle_value_t / region_tag_t / region_value_t /
# value_consistent_v.  Each is constrained on
# `requires ConsumerEndpoint<FnPtr>`.  Without the constraint,
# diagnostics would land deep inside consumer_handle_value_t /
# owned_region_*_t requires clauses rather than at the consumer-
# endpoint-shape boundary.
crucible_neg_compile_test(neg_consumer_endpoint_handle_value_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_consumer_endpoint_region_tag_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_consumer_endpoint_region_value_t_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_consumer_endpoint_value_consistent_v_on_non_endpoint
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D17 SwmrWriter constrained-extractor fixtures ────────────
#
# Pins the THREE constrained extractors on SwmrWriter.h:
#   - swmr_writer_handle_value_t
#   - swmr_writer_published_value_t
#   - swmr_writer_value_consistent_v
# Each constrained on `requires SwmrWriter<FnPtr>`.  Without the
# constraint, diagnostics would land in D07's swmr_writer_value_t
# requires clause (or in std::remove_cv_t deep templates) rather
# than at the SWMR-writer-shape boundary.
crucible_neg_compile_test(neg_swmr_writer_handle_value_t_on_non_swmr_writer
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_swmr_writer_published_value_t_on_non_swmr_writer
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_swmr_writer_value_consistent_v_on_non_swmr_writer
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D18 SwmrReader constrained-extractor fixtures ────────────
#
# Pins the THREE constrained extractors on SwmrReader.h:
#   - swmr_reader_handle_value_t
#   - swmr_reader_returned_value_t
#   - swmr_reader_value_consistent_v
# Each constrained on `requires SwmrReader<FnPtr>`.
crucible_neg_compile_test(neg_swmr_reader_handle_value_t_on_non_swmr_reader
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_swmr_reader_returned_value_t_on_non_swmr_reader
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_swmr_reader_value_consistent_v_on_non_swmr_reader
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D19 PipelineStage constrained-extractor fixtures ─────────
#
# Pins the THREE constrained extractors on PipelineStage.h:
#   - pipeline_stage_input_value_t
#   - pipeline_stage_output_value_t
#   - pipeline_stage_is_value_preserving_v
# Each constrained on `requires PipelineStage<FnPtr>`.
crucible_neg_compile_test(neg_pipeline_stage_input_value_t_on_non_pipeline
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_pipeline_stage_output_value_t_on_non_pipeline
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_pipeline_stage_is_value_preserving_v_on_non_pipeline
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D21 IsNumericalTier constrained-extractor fixtures ───────
#
# Pins the TWO constrained extractors on IsNumericalTier.h:
#   - numerical_tier_value_t  (alias to wrapped element type)
#   - numerical_tier_v        (variable yielding pinned Tolerance)
# Each constrained on `requires is_numerical_tier_v<T>`.
crucible_neg_compile_test(neg_numerical_tier_value_t_on_non_numerical_tier
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_numerical_tier_v_on_non_numerical_tier
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D22 IsConsistency constrained-extractor fixtures ─────────
#
# Pins the TWO constrained extractors on IsConsistency.h.
crucible_neg_compile_test(neg_consistency_value_t_on_non_consistency
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_consistency_level_v_on_non_consistency
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D23 IsOpaqueLifetime constrained-extractor fixtures ──────
crucible_neg_compile_test(neg_opaque_lifetime_value_t_on_non_opaque_lifetime
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_opaque_lifetime_scope_v_on_non_opaque_lifetime
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D24 IsDetSafe constrained-extractor fixtures ─────────────
crucible_neg_compile_test(neg_det_safe_value_t_on_non_det_safe
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_det_safe_tier_v_on_non_det_safe
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/CipherTier IsCipherTier constrained-extractor fixtures
crucible_neg_compile_test(neg_cipher_tier_value_t_on_non_cipher_tier
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_cipher_tier_tag_v_on_non_cipher_tier
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/ResidencyHeat IsResidencyHeat constrained-extractor fixtures
crucible_neg_compile_test(neg_residency_heat_value_t_on_non_residency_heat
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_residency_heat_tag_v_on_non_residency_heat
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/Vendor IsVendor constrained-extractor fixtures
crucible_neg_compile_test(neg_vendor_value_t_on_non_vendor
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_vendor_backend_v_on_non_vendor
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/Crash IsCrash constrained-extractor fixtures
crucible_neg_compile_test(neg_crash_value_t_on_non_crash
  "constraints not satisfied|template constraint failure|associated constraints")

crucible_neg_compile_test(neg_crash_class_v_on_non_crash
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/Budgeted IsBudgeted constrained-extractor fixture
# Single fixture (no tag-extractor — Budgeted is the FIRST product
# wrapper; its grade axes are runtime, not compile-time NTTPs).
crucible_neg_compile_test(neg_budgeted_value_t_on_non_budgeted
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/EpochVersioned IsEpochVersioned constrained-extractor
crucible_neg_compile_test(neg_epoch_versioned_value_t_on_non_epoch_versioned
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/NumaPlacement IsNumaPlacement constrained-extractor
crucible_neg_compile_test(neg_numa_placement_value_t_on_non_numa_placement
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D30/RecipeSpec IsRecipeSpec constrained-extractor
# CLOSES the D30 batch — final wrapper-detector for the 8-wrapper set.
crucible_neg_compile_test(neg_recipe_spec_value_t_on_non_recipe_spec
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D10 IsPureFunction concept rejection ──────────────────────
# Pins the substitution-failure path: a function with a non-empty
# inferred row must NOT match `requires IsPureFunction<FnPtr>`.
crucible_neg_compile_test(neg_inferred_row_pure_concept_rejects_alloc_fn
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-F06 IsFusable concept rejection on arity mismatch ──────
crucible_neg_compile_test(neg_fusion_concept_rejects_arity_mismatch
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-G79 / G80 F*-style alias predicate rejection fixtures ────
# Three fixtures, one per orthogonal lattice violation: empty-row vs
# state, divergence vs state, state vs context-tag.  Pin the production-
# shape failure surface (templated caller with `requires IsX<R>`).
crucible_neg_compile_test(neg_fx_aliases_pure_rejects_alloc_row
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_fx_aliases_div_rejects_alloc_row
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_fx_aliases_st_rejects_bg_row
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-F07 fuse<>() entry-point substitution-failure fixtures ───
# F06's fixture above tests IsFusable through a custom local template;
# these three fixtures call ::crucible::safety::fuse<>() directly,
# which is the production-shape failure surface.  Three distinct
# rejection classes — composability, throwing, impurity — to cover
# the three orthogonal axes of the V1 predicate.
crucible_neg_compile_test(neg_fusion_fuse_rejects_type_mismatch
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_fusion_fuse_rejects_throwing_producer
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_fusion_fuse_rejects_impure_producer
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-F09-AUDIT-2 IsCacheableFunction concept rejection ────────
# `auto FnPtr` accepts ANY structural NTTP — without the concept
# fence the dispatcher cache would silently produce nonsense slots
# for integral constants, data-pointers, or member-function
# pointers.  Three orthogonal rejection classes mirror the three
# wrong-shape NTTP categories the production API guards against.
crucible_neg_compile_test(neg_computation_cache_integral_nttp
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_computation_cache_data_ptr_nttp
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_computation_cache_member_fn_nttp
  "constraints not satisfied|template constraint failure|associated constraints")
# F09-AUDIT-3 sister rejection — member-DATA pointers (vs member-fn
# pointers above).  Closes the rejection-space symmetry across all
# four NTTP categories the cache cannot key on.
crucible_neg_compile_test(neg_computation_cache_member_data_ptr_nttp
  "constraints not satisfied|template constraint failure|associated constraints")
# F11 IsEffectRow concept fence — non-effects::Row<...> Row params
# rejected from the row-aware cache API.
crucible_neg_compile_test(neg_computation_cache_in_row_non_row_param
  "constraints not satisfied|template constraint failure|associated constraints")
# F11-AUDIT cross-constraint coverage — IsCacheableFunction conjunct
# on the row-aware path.  The `_in_row` template carries TWO
# constraints (`IsCacheableFunction<FnPtr> && IsEffectRow<Row>`); the
# row-blind neg fixtures cover the IsCacheableFunction half on the
# row-blind path and `neg_computation_cache_in_row_non_row_param` covers
# the IsEffectRow half on the row-aware path — this fixture closes
# the matrix on the row-aware × IsCacheableFunction cell.
crucible_neg_compile_test(neg_computation_cache_in_row_non_function_nttp
  "constraints not satisfied|template constraint failure|associated constraints")
# F13-AUDIT subset_row Mask precondition — pins the requires-clause
# on the subset_row<Mask> alias used by the F13 invalidation TU.
# Without the `requires (Mask < 64u)` fence, high bits of Mask would
# be silently truncated, aliasing multiple Mask values to the same
# Row<> type.  Catches the drift at substitution time.
crucible_neg_compile_test(neg_subset_row_mask_geq_64
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I09 record_event row-fence ───────────────────────────────
# Cipher::record_event<CallerRow> requires Subrow<Row<IO, Block>,
# CallerRow>.  Empty / Hot / Pure caller rows fail substitution at
# the requires-clause; this fixture pins that the fence fires loudly
# instead of silently misdispatching to the file-I/O path.
crucible_neg_compile_test(neg_cipher_record_event_pure_row
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I09-AUDIT (Finding A) per-axis neg-compile parity ────────
# Each fixture pins a distinct rejection leg of the
# Subrow<Row<IO, Block>, CallerRow> constraint:
#   • io_only    — Row<IO> rejects (missing Block leg)
#   • block_only — Row<Block> rejects (missing IO leg)
#   • alloc_only — Row<Alloc> rejects (wrong axis entirely)
#   • bg_only    — Row<Bg> rejects (Bg-tag does not imply IO/Block)
# Together with neg_pure_row, these four prevent any silent widening
# of the constraint via single-atom drift.
crucible_neg_compile_test(neg_cipher_record_event_io_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_cipher_record_event_block_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_cipher_record_event_alloc_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_cipher_record_event_bg_only
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-F12-AUDIT federation-bridge concept-fence parity ─────────
# F12's federation primitives carry the SAME `IsCacheableFunction<FnPtr>
# && IsEffectRow<Row>` constraint pair as F11's `*_in_row` family.  The
# F11 path has its own fence-rejection witnesses
# (neg_computation_cache_in_row_non_row_param +
# neg_computation_cache_in_row_non_function_nttp); these two fixtures
# extend the matrix to the F12 federation surface so a regression in
# the bridge doesn't silently pass non-Row / non-Fn args through to a
# row-blind federation key.
crucible_neg_compile_test(neg_computation_cache_federation_non_row
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_computation_cache_federation_non_fn
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_computation_cache_federation_without_local_permission
  "no matching function|candidate expects")

# ─── FOUND-I17 try_append_pure row-fence ────────────────────────────
# MetaLog::try_append_pure<CallerRow> requires IsPure<CallerRow>
# (CallerRow ⊆ Row<>).  These two fixtures cover distinct rejection
# legs: a caller declaring IO context and a caller declaring Bg
# context.  Together they pin that the IsPure constraint hasn't been
# silently widened to accept any single non-empty atom.
crucible_neg_compile_test(neg_metalog_try_append_pure_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I17-AUDIT per-axis matrix completion ─────────────────────
# Per-axis neg-compile parity: 4 fixtures completing the {Alloc, IO,
# Block, Bg, Init, Test} matrix (IO + Bg are above), plus 2 composite
# fixtures pinning multi-atom + saturation-row rejection.  Together
# these prevent silent widening of the IsPure constraint via any
# single-atom drift OR multi-atom composition that might slip through
# a per-axis-only audit.
crucible_neg_compile_test(neg_metalog_try_append_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_init
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_test
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_metalog_try_append_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I16 TraceRing row-fence — full per-axis matrix on both legs ─
# TraceRing::try_append_pure<CallerRow> AND TraceRing::drain_pure<CallerRow>
# both require IsPure<CallerRow>.  Sibling of the MetaLog::try_append_pure
# matrix above.  16 fixtures = 8 per direction (per-axis × 6 atoms +
# multi-atom + saturation).  Pins both directions of the SPSC ring.
# Producer side:
crucible_neg_compile_test(neg_tracering_try_append_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_init
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_test
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_try_append_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints")
# Consumer side:
crucible_neg_compile_test(neg_tracering_drain_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_init
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_test
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_tracering_drain_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I19 Vigil::dispatch_op row-fence ─────────────────────────
# Vigil::dispatch_op_pure<CallerRow> requires IsPure<CallerRow>.
# Sibling of TraceRing's I16 matrix and MetaLog's I17 matrix; the
# foreground per-op recording entry point is the canonical Pure
# operation per CRUCIBLE.md §L4.  8 fixtures complete the per-axis
# matrix on the Vigil leg of the row-fence cluster.
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_init
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_test
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I20 BackgroundThread::run row-fence ──────────────────────
# BackgroundThread::run_in_row<CallerRow> requires
# Subrow<Row<Bg, Alloc, IO, Block>, CallerRow>.  Structural inverse of
# the I16/I17/I19 hot-path Pure fences: instead of "caller declares
# AT MOST nothing", this is "caller declares AT LEAST {Bg, Alloc, IO,
# Block}".  9 fixtures cover the per-axis missing-atom matrix + the
# orthogonal-row rejections (Init/Test).  Mirrors the FOUND-I09
# Cipher::record_event fence (Subrow<Row<IO, Block>, CallerRow>) but
# at higher cardinality.
crucible_neg_compile_test(neg_background_thread_run_in_row_empty
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_bg_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_alloc_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_io_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_block_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_bg
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_block
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_init_only
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_test_only
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-I20-AUDIT saturation-minus-one matrix ────────────────────
# Maximum-cardinality (5 atoms) rejection witnesses — proves the
# fence is sharp at the high-cardinality boundary, not just at
# cardinality 3 or below.  Catches regressions where a future
# refactor switches to a size-based check (cardinality ≥ 4) instead
# of the membership-based Subrow.
crucible_neg_compile_test(neg_background_thread_run_in_row_all_but_bg
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_all_but_alloc
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_all_but_io
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_background_thread_run_in_row_all_but_block
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-D25-D29 constrained-extractor fixtures (10 total) ────────
# Two per detector — value_t + tier_v / strategy_v / tag_v / class_v.
# Mechanical extension of the D30-batch pattern; closes the
# detector-side neg-compile coverage symmetry.
crucible_neg_compile_test(neg_hot_path_value_t_on_non_hot_path
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_hot_path_tier_v_on_non_hot_path
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_wait_value_t_on_non_wait
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_wait_strategy_v_on_non_wait
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_mem_order_value_t_on_non_mem_order
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_mem_order_tag_v_on_non_mem_order
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_progress_value_t_on_non_progress
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_progress_class_v_on_non_progress
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_alloc_class_value_t_on_non_alloc_class
  "constraints not satisfied|template constraint failure|associated constraints")
crucible_neg_compile_test(neg_alloc_class_tag_v_on_non_alloc_class
  "constraints not satisfied|template constraint failure|associated constraints")

# ─── FOUND-G46 CipherTier negative-compile fixtures ─────────────────
#
# Pins the CipherTier wrapper's tier-pin discipline AND the underlying
# CipherTierLattice's per-At<T> nested-struct identity.  Closes the
# load-bearing claim-stronger-than-source path that would otherwise
# let a Cold-tier (S3-resident) value silently flow into the Keeper
# hot-tier reshard admission gate, blocking on minutes of disk IO at
# the worst possible moment (cluster-failure recovery fan-in).
# Type-fences CRUCIBLE.md §L14 three-tier persistence at every Cipher
# publish/flush boundary.
#
# 4 wrapper-surface fixtures:
#   - neg_cipher_tier_relax_to_stronger    LOAD-BEARING reshard fence
#   - neg_cipher_tier_cross_tier_assign    assignment-surface identity
#   - neg_cipher_tier_cross_tier_swap      swap-surface identity
#   - neg_cipher_tier_cross_tier_equality  operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_cipher_tier_at_cross_tier_mixing       At<T> nested-struct identity
#   - neg_cipher_tier_lattice_cross_lattice_mixing  CipherTierTag ≠ ProgressClass
crucible_neg_compile_test(neg_cipher_tier_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_cipher_tier_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_cipher_tier_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_cipher_tier_at_cross_tier_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_cipher_tier_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# GAPS-041 — named CipherTier mint sites must preserve direction:
# promote is upward-only, demote is downward-only.
crucible_neg_compile_test(neg_cipher_tier_promote_hot_to_cold
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_promote_unmovable_payload
  "PromotableTier|constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_demote_cold_to_hot
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_demote_unmovable_payload
  "DemotableTier|constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_restore_unmovable_payload
  "RestorableTier|constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_cipher_tier_restore_wrong_value_type
  "no matching function|could not convert|cannot convert|no viable")
# fixy-CR-10 fixture: production-shape T (movable, no
# content_hash_projection specialization) must red at the
# RestorableHashed gate; pre-CR-10 it compiled silently with the
# verification check elided for every T != ContentHash.
crucible_neg_compile_test(neg_cipher_tier_restore_no_hash_projection
  "RestorableTier|RestorableHashed|content_hash_projection|constraints not satisfied|no matching function|associated constraints")

# ─── FOUND-G51 ResidencyHeat negative-compile fixtures ──────────────
#
# Pins the ResidencyHeat wrapper's tier-pin discipline AND the
# underlying ResidencyHeatLattice's per-At<T> nested-struct
# identity.  Closes the load-bearing claim-stronger-than-source
# path that would otherwise let a Cold-tier (L3-resident) value
# silently flow into the KernelCache L1 fast-path admission gate,
# breaking the per-call shape budget (~10× cache-miss penalty).
# Type-fences CRUCIBLE.md §L2 KernelCache L1/L2/L3 + runtime observation
# metric-heat tracking at every cache publish/lookup boundary.
#
# Cross-lattice fixture is LOAD-BEARING for the 28_04 §4.7
# universal-vocabulary claim — three structurally-identical 3-tier
# chains (HotPath / CipherTier / ResidencyHeat) must stay type-
# disjoint even when their layout is byte-equivalent.
#
# 4 wrapper-surface fixtures:
#   - neg_residency_heat_relax_to_stronger    LOAD-BEARING L1 fence
#   - neg_residency_heat_cross_tier_assign    assignment-surface identity
#   - neg_residency_heat_cross_tier_swap      swap-surface identity
#   - neg_residency_heat_cross_tier_equality  operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_residency_heat_at_cross_tier_mixing       At<T> nested-struct
#   - neg_residency_heat_lattice_cross_lattice_mixing  ResidencyHeatTag
#                                                      ≠ CipherTierTag
crucible_neg_compile_test(neg_residency_heat_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_residency_heat_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_residency_heat_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_residency_heat_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_residency_heat_at_cross_tier_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_residency_heat_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G56 Vendor negative-compile fixtures ─────────────────────
#
# Pins the Vendor wrapper's PARTIAL-ORDER backend identity discipline
# AND the underlying VendorLattice's per-At<T> nested-struct
# identity.  THE FIRST WRAPPER WITH A PARTIAL-ORDER LATTICE: has
# SEVEN fixtures instead of the chain wrappers' six because the
# partial-order admits TWO load-bearing rejections (claim-stronger
# AND cross-vendor) where chains have only one (claim-stronger).
#
# Type-fences MIMIC.md per-vendor backend identity at every IR003*
# lowering and every cross-vendor numerics CI check.
#
# 5 wrapper-surface fixtures:
#   - neg_vendor_relax_to_stronger     LOAD-BEARING claim-Portable fence
#                                        (specific vendor → Portable rejected)
#   - neg_vendor_cross_vendor_relax    LOAD-BEARING cross-vendor fence
#                                        (NV → AMD rejected; THE wrapper's
#                                        defining safety guarantee)
#   - neg_vendor_cross_tier_assign     assignment-surface identity
#   - neg_vendor_cross_tier_swap       swap-surface identity
#   - neg_vendor_cross_tier_equality   operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_vendor_at_cross_tier_mixing       At<T> nested-struct identity
#   - neg_vendor_lattice_cross_lattice_mixing  VendorBackend ≠ HotPathTier
crucible_neg_compile_test(neg_vendor_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_vendor_cross_vendor_relax
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_vendor_cross_tier_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_vendor_cross_tier_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_vendor_cross_tier_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_vendor_at_cross_tier_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_vendor_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G61 Crash negative-compile fixtures ──────────────────────
#
# Pins the Crash<Class, T> wrapper's failure-mode discipline AND
# the underlying CrashLattice's per-At<T> nested-struct identity.
# The TENTH chain wrapper (28_04 §4.3.10).  Type-fences the
# OneShotFlag-skipping NoThrow-only admission gate that the
# dispatcher's recovery-aware boundaries depend on (see
# bridges/CrashTransport.h::CrashWatchedHandle).
#
# The bug class caught: a refactor that loosened the requires-
# clause guarding relax<>() — specifically, a slip from
# `CrashLattice::leq(WeakerClass, Class)` to a permissive form —
# would silently allow an Abort-tier value to claim NoThrow
# conformance.  Today caught only by the Keeper noticing process
# state inconsistency at recovery time; with the wrapper, becomes
# a compile error.
#
# 4 wrapper-surface fixtures:
#   - neg_crash_relax_to_stronger    LOAD-BEARING OneShotFlag fence
#   - neg_crash_cross_class_assign   assignment-surface identity
#   - neg_crash_cross_class_swap     swap-surface identity
#   - neg_crash_cross_class_equality operator==-surface identity
#
# 2 lattice-substrate fixtures:
#   - neg_crash_at_cross_class_mixing      At<T> nested-struct identity
#   - neg_crash_lattice_cross_lattice_mixing  CrashClass ≠ ProgressClass
crucible_neg_compile_test(neg_crash_relax_to_stronger
  "constraints not satisfied|no matching function|associated constraints")
crucible_neg_compile_test(neg_crash_cross_class_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_crash_cross_class_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_crash_cross_class_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_crash_at_cross_class_mixing
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_crash_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands")

# ─── FOUND-G63/G64 Budgeted product-wrapper negative-compile fixtures ──
#
# Pins Budgeted<T>'s product-wrapper axis discipline AND the
# underlying BitsBudgetLattice / PeakBytesLattice strong-newtype
# disjointness.  THE FIRST PRODUCT-WRAPPER NEG-FIXTURE BLOCK.
#
# Distinct from chain-wrapper neg-fixtures: there is NO `relax<>()`
# template, NO type-pinned class parameter, NO `at_cross_class`
# fixture.  Instead the discipline is around:
#   - Cross-T identity (Budgeted<int> ≠ Budgeted<double>).
#   - Axis identity (BitsBudget ≠ PeakBytes — both uint64_t-backed
#     but structurally distinct newtypes).
#
# 3 wrapper-surface fixtures (cross-T identity):
#   - neg_budgeted_cross_t_assign     assignment-surface identity
#   - neg_budgeted_cross_t_swap       swap-surface identity
#   - neg_budgeted_cross_t_equality   operator==-surface identity
#
# 1 wrapper-surface fixture (cross-axis identity — load-bearing):
#   - neg_budgeted_axis_swap          ctor parameter-type fence —
#                                       BitsBudget ↔ PeakBytes flip
#                                       at call site rejected.
#
# 1 lattice-substrate fixture:
#   - neg_budgeted_lattice_cross_lattice_mixing
#                                       BitsBudgetLattice::leq(b, p)
#                                       cross-axis mixing rejected
#                                       below the wrapper.
crucible_neg_compile_test(neg_budgeted_cross_t_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_budgeted_cross_t_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_budgeted_cross_t_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_budgeted_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_budgeted_satisfies_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_budgeted_combine_max_cross_t
  "no match for|cannot convert|could not convert|no matching function|no viable")
crucible_neg_compile_test(neg_budgeted_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands|could not convert")

# ─── FOUND-G67/G68 EpochVersioned product-wrapper neg-compile fixtures ──
#
# Pins EpochVersioned<T>'s product-wrapper axis discipline AND the
# underlying EpochLattice / GenerationLattice strong-newtype
# disjointness.  Mirrors Budgeted's neg-fixture template at the
# second product wrapper.
#
# 3 wrapper-surface fixtures (cross-T identity):
#   - neg_epoch_versioned_cross_t_assign     assignment-surface identity
#   - neg_epoch_versioned_cross_t_swap       swap-surface identity
#   - neg_epoch_versioned_cross_t_equality   operator==-surface identity
#
# 2 wrapper-surface fixtures (cross-axis identity — load-bearing):
#   - neg_epoch_versioned_axis_swap          ctor parameter-type fence —
#                                             Epoch ↔ Generation flip at
#                                             call site rejected
#   - neg_epoch_versioned_is_at_least_axis_swap
#                                             admission-gate parameter-
#                                             type fence
crucible_neg_compile_test(neg_epoch_versioned_cross_t_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_epoch_versioned_cross_t_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_epoch_versioned_cross_t_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_epoch_versioned_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_epoch_versioned_is_at_least_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_epoch_versioned_combine_max_cross_t
  "no match for|cannot convert|could not convert|no matching function|no viable")
crucible_neg_compile_test(neg_epoch_versioned_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands|could not convert")

# ─── FOUND-G71/G72 NumaPlacement product-wrapper neg-compile fixtures ──
#
# Pins NumaPlacement<T>'s product-wrapper axis discipline.  Third
# product-wrapper neg-fixture block; mirrors Budgeted/EpochVersioned.
# Distinctive in that the two axes are STRUCTURALLY DIFFERENT (1-byte
# enum vs 8-byte struct), so axis-swap is doubly-defended at the
# C++ type-system level.
#
# 3 wrapper-surface fixtures (cross-T identity):
#   - neg_numa_placement_cross_t_assign     assignment-surface identity
#   - neg_numa_placement_cross_t_swap       swap-surface identity
#   - neg_numa_placement_cross_t_equality   operator==-surface identity
#
# 2 wrapper-surface fixtures (cross-axis identity — load-bearing):
#   - neg_numa_placement_axis_swap          ctor parameter-type fence
#   - neg_numa_placement_admits_axis_swap   admission-gate parameter-
#                                            type fence
crucible_neg_compile_test(neg_numa_placement_cross_t_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_numa_placement_cross_t_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_numa_placement_cross_t_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_numa_placement_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_numa_placement_admits_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_numa_placement_combine_max_cross_t
  "no match for|cannot convert|could not convert|no matching function|no viable")
crucible_neg_compile_test(neg_numa_placement_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands|could not convert")

# ─── FOUND-G75/G76 RecipeSpec product-wrapper neg-compile fixtures ───
#
# Pins RecipeSpec<T>'s product-wrapper axis discipline.  Fourth and
# FINAL product-wrapper neg-fixture block.  Both axes are uint8_t
# strong scoped enums — distinct C++ types, but the structural
# similarity makes the axis-swap fence specifically valuable.
#
# 3 wrapper-surface fixtures (cross-T identity):
#   - neg_recipe_spec_cross_t_assign     assignment-surface identity
#   - neg_recipe_spec_cross_t_swap       swap-surface identity
#   - neg_recipe_spec_cross_t_equality   operator==-surface identity
#
# 2 wrapper-surface fixtures (cross-axis identity — load-bearing):
#   - neg_recipe_spec_axis_swap          ctor parameter-type fence
#   - neg_recipe_spec_admits_axis_swap   admission-gate parameter-
#                                         type fence
crucible_neg_compile_test(neg_recipe_spec_cross_t_assign
  "no match for|cannot convert|no viable")
crucible_neg_compile_test(neg_recipe_spec_cross_t_swap
  "no matching function|cannot convert|no match for")
crucible_neg_compile_test(neg_recipe_spec_cross_t_equality
  "no match for|cannot convert|no matching function|invalid operands")
crucible_neg_compile_test(neg_recipe_spec_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_recipe_spec_admits_axis_swap
  "no match for|cannot convert|could not convert|no matching function")
crucible_neg_compile_test(neg_recipe_spec_combine_max_cross_t
  "no match for|cannot convert|could not convert|no matching function|no viable")
crucible_neg_compile_test(neg_recipe_spec_lattice_cross_lattice_mixing
  "no match for|cannot convert|no matching function|invalid operands|could not convert")

# ─── FOUND-G71 AffinityMask UB-fence neg-compile ─────────────────────
#
# Pins the contract guard on AffinityMask::single() that prevents UB
# shifting uint64_t by ≥ 64 (or ≥ kBits = 256 for the multi-word
# implementation).  THE FIRST CONTRACT-VIOLATION NEG-FIXTURE in the
# wrapper family.
#
# Diagnostic comes from GCC's contract-pre-clause reporting; the
# regex matches the most stable substring across G16 contract
# implementations: "contract" + the failing clause's textual form.
crucible_neg_compile_test(neg_affinity_mask_core_out_of_bounds
  "contract|pre.*core|condition.*kMaxCore|core <= kMaxCore|in.*constant.*expression|array subscript.*outside the bounds")

# ─── ALGEBRA-15 N-ary ProductLattice neg-compiles ───────────────────
#
# Three neg-compiles pinning the variadic primary template's
# structural invariants:
#
#   neg_product_lattice_non_lattice_component
#       Instantiating ProductLattice<L0, NotALattice, L2> — the
#       primary's `static_assert((Lattice<Ls> && ...))` fires.
#       Without this neg, a refactor that loosened the concept
#       conjunction would let non-Lattice types slip through and
#       produce a confusing fold-expression diagnostic cascade
#       instead of the named violation.  [FRAMEWORK-CONTROLLED]
#
#   neg_product_lattice_get_out_of_range
#       Calling `get<I>(e)` with I >= sizeof...(Ls) — the requires
#       clause `requires (I < sizeof...(Ls))` rejects.  Pins the
#       per-call bounds discipline (symmetric to HappensBefore's
#       operator[] bounds-check).  [GCC-text + framework]
#
#   neg_product_lattice_cross_arity_mixing
#       Passing ProductLattice<L,L,L>::element_type to
#       ProductLattice<L,L>::leq — cross-arity element_types are
#       structurally different (different specializations).  Catches
#       a refactor that added implicit converting constructors
#       between ProductElement specializations.  [GCC-WRAPPER-TEXT]
crucible_neg_compile_test(neg_product_lattice_non_lattice_component
  "every L_i must satisfy the Lattice concept")
crucible_neg_compile_test(neg_product_lattice_get_out_of_range
  "constraints not satisfied|I < sizeof")
crucible_neg_compile_test(neg_product_lattice_cross_arity_mixing
  "could not convert|no matching function|cannot convert")

# ─── MIGRATE-10 TimeOrdered<T, N, Tag> wrapper neg-compiles ─────────
#
# Three wrapper-level neg-compiles pinning the per-(N, Tag) type
# distinction at the SAFETY surface (vs the underlying lattice-level
# tests).  TimeOrdered is the FIRST production wrapper to use Graded's
# regime-#4 storage (genuine 2-field — non-trivial element_type), so
# it deserves its own per-template-parameter coverage instead of
# inheriting the lattice's.  A future refactor that added an implicit
# converting constructor on TimeOrdered (for "interoperability") would
# defeat the cross-N or cross-Tag discipline at the wrapper while the
# lattice-level tests continued to pass.
#
#   neg_time_ordered_zero_n
#       TimeOrdered<T, 0> trips the wrapper's static_assert (mirroring
#       the lattice's N>0 guard).  [FRAMEWORK-CONTROLLED]
#
#   neg_time_ordered_cross_n_mixing
#       TimeOrdered<T, 4>::happens_before(TimeOrdered<T, 3>) rejected
#       at the wrapper's method signature.  [GCC-WRAPPER-TEXT]
#
#   neg_time_ordered_cross_tag_mixing
#       TimeOrdered<T, 4, TagA>::happens_before(TimeOrdered<T, 4, TagB>)
#       rejected — pins the Tag's protocol-distinction at the wrapper.
#       [GCC-WRAPPER-TEXT]
crucible_neg_compile_test(neg_time_ordered_zero_n
  "TimeOrdered<T, 0> is forbidden")
crucible_neg_compile_test(neg_time_ordered_cross_n_mixing
  "could not convert|no matching function|cannot convert")
crucible_neg_compile_test(neg_time_ordered_cross_tag_mixing
  "could not convert|no matching function|cannot convert")

# ─── MIGRATE-8 Stale<T> wrapper neg-compile ─────────────────────────
#
# Stale<T> has only ONE template-parameter axis (T itself; no N or
# Tag like TimeOrdered), so its wrapper-level neg-compile surface is
# correspondingly smaller — one cross-T type-identity test pins the
# per-payload distinction at every method signature.
crucible_neg_compile_test(neg_stale_cross_t_mixing
  "could not convert|no matching function|cannot convert")

# WriteOnceNonNull<T*> — pointer-specialized single-set primitive (#77).
#
# [FRAMEWORK-CONTROLLED] Two named diagnostics gate the primitive's
# discipline:
#   - [WriteOnceNonNull_NonPointer_Type] — instantiating with a non-
#     pointer T (e.g. WriteOnceNonNull<int>) fires the primary
#     template's static_assert, redirecting callers to WriteOnce<T>
#     which handles non-pointer single-set.
#   - [AppendOnly_Over_WriteOnceNonNull_Redundant] — stacking
#     AppendOnly<WriteOnceNonNull<T*>> fires the redundancy assert,
#     symmetric with AppendOnly<WriteOnce<T>>'s rejection above.
crucible_neg_compile_test(neg_writeoncenonnull_non_pointer
  "WriteOnceNonNull_NonPointer_Type")
crucible_neg_compile_test(neg_appendonly_over_writeoncenonnull
  "AppendOnly_Over_WriteOnceNonNull_Redundant")

# ─── P1-AUDIT (#852): RefinedAlgebra type-level guards ─────────────
#
# DivisibleBy<0>: modulo by zero is UB; the predicate's own
# static_assert fires at instantiation.  Bounded<Lo, Hi, T> with
# Lo > Hi: empty range, no value satisfies; the bounded_alias
# trampoline's static_assert fires.
crucible_neg_compile_test(neg_refined_algebra_divisible_by_zero
  "DivisibleBy.*0.*undefined|modulo by zero|Divisor != ")
crucible_neg_compile_test(neg_refined_algebra_bounded_inverted
  "Bounded.*range is empty|Lo <= Hi|Lo > Hi")

# PROD-WRAP-8 (#537): Graph::num_nodes_ migrated to safety::Monotonic
# <uint32_t>.  These two fixtures pin the discipline-load-bearing
# absences on the Monotonic wrapper itself: the field cannot be
# silently subverted by raw assignment, and it cannot be silently
# read via implicit conversion.  Future refactors that re-add either
# escape hatch break these tests, surfacing the policy regression.
crucible_neg_compile_test(neg_monotonic_assign_from_raw
  "no match for|no known conversion")
crucible_neg_compile_test(neg_monotonic_implicit_to_raw
  "cannot convert|invalid conversion|no viable conversion")

# SessionResource pin-discipline rejection (task #406, SAFEINT-B17 from
# misc/24_04_2026_safety_integration.md §17).  make_session_handle
# called with Resource = lvalue reference to a NON-Pinned object must
# fail to compile with the routed diagnostic naming the discipline
# violation.
crucible_neg_compile_test(neg_session_resource_not_pinned
  "SessionResource_NotPinned")

# Tier 1 compiler firings — each has a distinct diagnostic.

# [FRAMEWORK-CONTROLLED] Matches ScopedView's = delete("reason") string.
crucible_neg_compile_test(neg_assignment
  "ScopedView is single-binding; assignment hides state transitions")

# [GCC-WRAPPER-TEXT] Private-ctor access protection IS the mechanism;
# converting would require exposing a constraint that defeats the
# private-ctor pattern's purpose.  Documented; accepted as intrinsic.
crucible_neg_compile_test(neg_direct_construct
  "is private within this context")

# [FRAMEWORK-CONTROLLED] Matches ScopedView's explicit-deleted default
# ctor's reason string (added by #371 to replace the prior dependency
# on GCC's "no matching function for call to" overload-resolution text).
crucible_neg_compile_test(neg_default_construct
  "ScopedView default-construction is meaningless")

# [FRAMEWORK-CONTROLLED] Matches another of ScopedView's = delete reasons.
crucible_neg_compile_test(neg_heap_allocate
  "ScopedView must live on the stack; heap allocation defeats")

# State-discipline: SealedView cannot satisfy the MutableView parameter
# of register_name / register_op — the only typed registration path
# requires proof that the table is in the Mutable phase.
#
# [GCC-WRAPPER-TEXT] Production-side typed-view rejection.  The direct
# signature requires MutableView; SealedView reaches GCC's conversion
# diagnostic before any runtime path can exist.
crucible_neg_compile_test(neg_schema_register_with_sealed_view
  "cannot convert.*Sealed.*MutableView")
crucible_neg_compile_test(neg_ckernel_register_with_sealed_view
  "cannot convert.*Sealed.*MutableView")

# FIXY-U-135 — HS14 floor for CKernelTable::mint_mutable_view.
# Paired fixtures pinning the per-state lock at BOTH gates of the
# member mint that survived the prior MutableView=>SealedView sweep
# (which only pinned the inverse direction via register_op above):
#   * _to_sealed_overload — value-level typed-overload rejection when
#     a freshly-minted MutableView is offered where SealedView is
#     required.  Dual of neg_ckernel_register_with_sealed_view: same
#     per-state lock, opposite direction.  [GCC-WRAPPER-TEXT]
#   * _in_field — structural Tier-2 field-storage audit reject when a
#     class declares CKernelTable::MutableView as a member.  Routes
#     through the same no_scoped_view_field_check audit that fires
#     for IterDet Steady (#928), exercising the alias-stability path
#     (MutableView is a typedef of ScopedView<CKernelTable, Mutable>).
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_to_sealed_overload
  "invalid initialization of reference.*SealedView.*Mutable|cannot convert.*Mutable.*Sealed")
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_in_field
  "Type contains a safety::ScopedView<> in some field")

# FIXY-U-136 — HS14 spirit gap closure for SchemaTable::mint_mutable_view.
# U-135's CKernelTable fixtures bumped SchemaTable::mint_mutable_view's
# scanner-count from 0 → 2 via shared-name name-coincidence (the HS14
# scanner counts the bare token `mint_mutable_view` and conflates both
# carriers).  The SCANNER says satisfied; the SPIRIT is unsatisfied —
# a regression in SchemaTable's per-state discipline would slip past
# U-135's CKernelTable-only fixtures because the ScopedView<C, Tag>
# types are distinct per-carrier (different `view_ok` ADL overloads,
# different `no_scoped_view_field_check` instantiations).  U-136
# ships the missing carrier-specific pair so a future edit affecting
# SchemaTable's gate is caught independently.
crucible_neg_compile_test(neg_schema_mint_mutable_view_to_sealed_overload
  "invalid initialization of reference.*SealedView.*Mutable|cannot convert.*Mutable.*Sealed")
crucible_neg_compile_test(neg_schema_mint_mutable_view_in_field
  "Type contains a safety::ScopedView<> in some field")

# FIXY-U-137 — HS14 floor for CrucibleContext::mint_compiled_view.
# CrucibleContext has only one typed view (CompiledView; no companion
# RecordingView), so the intra-carrier per-state mismatch pattern used
# by CKernelTable/SchemaTable doesn't apply.  The cross-carrier pair:
#   * _cross_carrier — feed CompiledView to a CKernelTable::MutableView
#     parameter; type system rejects the ScopedView<C,T> instantiation
#     mismatch.  Demonstrates the carrier-level lock independently of
#     state-level locks tested by U-135/U-136.  [GCC-WRAPPER-TEXT]
#   * _in_field — same structural Tier-2 field-storage audit as the
#     other carriers; proves the audit reaches CrucibleContext via its
#     CompiledView typedef.
crucible_neg_compile_test(neg_crucible_context_mint_compiled_view_cross_carrier
  "invalid initialization of reference.*MutableView.*Compiled|cannot convert.*Compiled.*Mutable")
crucible_neg_compile_test(neg_crucible_context_mint_compiled_view_in_field
  "Type contains a safety::ScopedView<> in some field")

# [FIXY-U-138] HS14 floor for PoolAllocator::mint_initialized_view.  Same
# carrier-singleton shape as CrucibleContext::mint_compiled_view above —
# PoolAllocator has only one typed view (InitializedView; no companion
# "DestroyedView") so the cross-carrier mismatch is the equivalent type-
# system gate.  Together the pair pins both soundness layers (typed-
# overload + structural field-storage).
crucible_neg_compile_test(neg_pool_allocator_mint_initialized_view_cross_carrier
  "invalid initialization of reference.*MutableView.*Initialized|cannot convert.*Initialized.*Mutable")
crucible_neg_compile_test(neg_pool_allocator_mint_initialized_view_in_field
  "Type contains a safety::ScopedView<> in some field")

# [FIXY-U-139] HS14 floor for ReplayEngine::mint_active_view.  Same
# carrier-singleton shape as U-137 + U-138 — ReplayEngine has only one
# typed view (ActiveView; no companion "InactiveView" / "DivergedView")
# so the cross-carrier mismatch is the equivalent type-system gate.
# Together the pair pins both soundness layers (typed-overload +
# structural field-storage).  Closes the last HS14: 0 mint gap.
crucible_neg_compile_test(neg_replay_engine_mint_active_view_cross_carrier
  "invalid initialization of reference.*MutableView.*Active|cannot convert.*Active.*Mutable")
crucible_neg_compile_test(neg_replay_engine_mint_active_view_in_field
  "Type contains a safety::ScopedView<> in some field")

# [FIXY-U-140] safety::Machine<> HS14 floor — first non-ScopedView
# primitive to get neg-compile pair coverage (closes #146 A8-P2 slice).
# Class T: deleted-copy with reason string (linearity gate).
# Class U: transition without machine_transition opt-in (concept gate).
# Together they pin both soundness layers of the Machine state-machine
# primitive.
crucible_neg_compile_test(neg_machine_copy_rejected
  "Machine is move-only|use of deleted function|deleted function")
crucible_neg_compile_test(neg_machine_transition_without_optin
  "MachineTransition|constraints not satisfied|machine_transition")

# [FIXY-U-141] safety::ct::* HS14 floor — second non-ScopedView
# primitive to get neg-compile pair coverage (closes ConstantTime
# slice of #146 A8-P2 alongside U-140's Machine slice).
# Class U: ct::less<int32_t> fails std::unsigned_integral concept.
# Class M: ct::eq with mismatched-length spans fires CRUCIBLE_PRE
# at consteval (non-constexpr __builtin_trap poisons the call).
# Together they pin both soundness layers of the ct::* primitives:
# (a) unsigned-only type-domain gate, and (b) length-equality
# invariant on byte-span comparison.
crucible_neg_compile_test(neg_constanttime_less_signed_type
  "unsigned_integral|constraints not satisfied|the required expression")
crucible_neg_compile_test(neg_constanttime_eq_length_mismatch
  "non-constant condition|__builtin_trap|not a constant expression|non-constant.*static_assert|call to non.*constexpr")

# [FIXY-U-142] safety::Tagged<T, Tag> HS14 floor — third non-ScopedView
# primitive to get neg-compile pair coverage (closes Tagged slice of
# #146 A8-P2 alongside U-140's Machine + U-141's ConstantTime).
# Class U: mint_tagged<int, int>(42) fails ValidTaggedTag concept
#          (Tag must be std::is_class_v).
# Class T: Tagged<uint64_t, source::External> passed to a function
#          expecting Tagged<uint64_t, source::Sanitized> — unrelated
#          template instantiations, no implicit conversion across
#          the trust boundary.
# Together they pin both soundness layers of Tagged's discipline:
# (a) tag-shape gate (class-type-only), and (b) tag-identity gate
# (provenance Tag is load-bearing for overload resolution).
crucible_neg_compile_test(neg_tagged_mint_scalar_tag
  "ValidTaggedTag|constraints not satisfied|is_class_v|the required expression")
crucible_neg_compile_test(neg_tagged_unrelated_source_mismatch
  "cannot convert.*External.*Sanitized|cannot convert.*source::External|no matching function.*hash_for_kernel|could not convert.*source::External")

# [FIXY-U-143] safety::SealedRefined<Pred, T> HS14 floor — fourth
# non-ScopedView primitive (closes SealedRefined slice of #146 A8-P2
# alongside U-140's Machine, U-141's ConstantTime, U-142's Tagged).
# Class T: std::move(sealed).into() — method DELIBERATELY ABSENT from
#          SealedRefined (load-bearing distinguishing property vs
#          Refined<Pred, T>); name-lookup fails at the call site.
# Class U: mint_sealed_refined<non_null, int>(42) — non_null requires
#          pointer arg; int fails PredicateInvocableOn concept gate
#          (same predicate-invocability discipline as Refined).
# Together they pin both soundness layers of SealedRefined:
# (a) structural sealing (no into() escape hatch); and (b) predicate-
# invocability gate (mint refuses categorically mismatched pairs).
crucible_neg_compile_test(neg_sealedrefined_into_absent
  "has no member named.*into|no member named.*into|into.*is not a member")
crucible_neg_compile_test(neg_sealedrefined_mint_arg_mismatch
  "PredicateInvocableOn|constraints not satisfied|the required expression|cannot bind")

# [FIXY-U-144] safety::Pinned<> / NonMovable<> HS14 floor — fifth
# non-ScopedView primitive (closes Pinned/NonMovable slice of #146
# A8-P2 alongside U-140 Machine, U-141 ConstantTime, U-142 Tagged,
# U-143 SealedRefined).
# Class T-copy: PinnedRing copy-ctor deleted with "stable address —
#               address-as-identity" reason — Pinned<T> semantics.
# Class T-move: OwnedHandle move-ctor deleted with "exclusive
#               ownership — moved-from shell" reason — NonMovable<T>
#               semantics.
# Together they pin both soundness layers of the address-stability /
# exclusive-ownership CRTP mixin family: Pinned protects ADDRESS as
# identity (interior pointers stay valid); NonMovable protects
# RESOURCE as ownership (singleton owner never branches).
crucible_neg_compile_test(neg_pinned_copy_rejected
  "stable address|use of deleted function|deleted function")
crucible_neg_compile_test(neg_nonmovable_move_rejected
  "exclusive ownership|use of deleted function|deleted function")

# [FIXY-U-145] safety::OwnedRegion<T, Tag> HS14 floor — sixth
# non-ScopedView primitive (closes OwnedRegion slice of #146 A8-P2).
# Class T-copy: copy ctor deleted with linearity-duplication reason
#   ("OwnedRegion owns a Permission — copy would duplicate the
#   linear token") — embedded Permission<Tag>'s linearity is
#   preserved at the type system level.
# Class T-cross-tag: OwnedRegion<int, TagA> passed to function
#   demanding OwnedRegion<int, TagB> — unrelated template
#   instantiations, no implicit conversion across CSL provenance
#   tags.
# Together they pin both soundness layers of OwnedRegion:
# (a) embedded Permission's linearity preserved, and (b) phantom
# Tag is load-bearing for overload resolution (regions for
# different arenas / sub-arenas don't substitute for each other).
crucible_neg_compile_test(neg_ownedregion_copy_rejected
  "duplicate the linear token|use of deleted function|deleted function")
crucible_neg_compile_test(neg_ownedregion_cross_tag_mismatch
  "invalid initialization of reference.*RegionTagA|cannot convert.*RegionTag|no matching function.*process_region_a|could not convert.*RegionTag")

# [FIXY-U-146] HS14 floor for three lingering 1-fixture primitives —
# bumps FinalBy + NotInherited + Stale to ≥2 each.  Closes the last
# below-floor cells in the safety/ non-ScopedView §XVI catalog
# alongside U-140 Machine, U-141 ConstantTime, U-142 Tagged,
# U-143 SealedRefined, U-144 Pinned/NonMovable, U-145 OwnedRegion.
#
# Each fixture pair pins TWO distinct soundness layers per primitive:
#
#   FinalBy   — Class T-subclass (sibling neg_finalby_subclass_attempt:
#                private virtual ctor inaccessible from subclass-as-
#                most-derived) PLUS Class T-copy (THIS slice: deleted
#                copy on the CRTP base, propagated through the
#                protected Derived's defaulted copy ctor).
#
#   NotInherited — Class M-consteval (sibling neg_notinherited_non_
#                final: static_assert inside assert_not_inherited<>())
#                PLUS Class U-concept (THIS slice: NotInherited<T>
#                concept's constraint-not-satisfied diagnostic at
#                overload resolution time).
#
#   Stale     — Class T-cross-T (sibling neg_stale_cross_t_mixing:
#                payload-type mismatch in compose_add) PLUS Class
#                T-rvalue-only (THIS slice: consume() `&&`-qualified
#                rejects lvalue invocation, mirroring Linear<T>'s
#                consume discipline).
crucible_neg_compile_test(neg_finalby_protected_copy_rejected
  "FinalBy_Subclass_Forbidden|use of deleted function|deleted function")
crucible_neg_compile_test(neg_notinherited_concept_rejection
  "constraints not satisfied|NotInherited.*not satisfied|is_final_v.*not.*satisfied|no matching function.*register_final_type")
crucible_neg_compile_test(neg_stale_consume_lvalue
  "passing.*as.*this.*argument discards qualifiers|cannot bind rvalue reference|no matching.*consume|discards qualifiers")

# [FIXY-U-147] safety::AtomicMonotonic<T, Cmp> HS14 floor — closes
# the AtomicMonotonic slice of #146 A8-P2.
#
# Class T-pinned-copy: AtomicMonotonic inherits Pinned<Derived> and
#   therefore its copy ctor — the address-stability discipline IS
#   load-bearing because the std::atomic<T> field is at a fixed
#   cross-thread-shared address.
# Class U-non-trivially-copyable: requires `std::is_trivially_copyable
#   _v<T>` rejects non-trivial payloads at template instantiation —
#   the std::atomic<T> machinery requires it per [atomics.types.
#   generic]/1.
#
# Together they pin BOTH enforcement layers AtomicMonotonic ships:
# address stability for the wrapper instance + payload triviality
# for the templated atomic.  Softening either would slip past a
# single-fixture audit.
crucible_neg_compile_test(neg_atomicmonotonic_pinned_copy_rejected
  "stable address|use of deleted function|deleted function|Pinned")
crucible_neg_compile_test(neg_atomicmonotonic_non_trivially_copyable
  "constraints not satisfied|is_trivially_copyable.*not.*satisfied|AtomicMonotonic.*constraints|no type named")

# [FIXY-U-148] safety::BoundedMonotonic<T, Max> HS14 floor —
# bumps BoundedMonotonic from 1 → 2 fixtures.  Existing Class M
# (neg_pre_macro_bounded_monotonic_bump_at_max) pins the bound-
# invariant CRUCIBLE_PRE inside bump(); this Class T fixture pins
# the load-bearing role of Max as part of type identity (cross-Max
# overload-resolution rejection).  Closes the BoundedMonotonic
# slice of #146 A8-P2.
crucible_neg_compile_test(neg_boundedmonotonic_cross_max_mismatch
  "invalid initialization of reference.*BoundedMonotonic|cannot convert.*BoundedMonotonic|no matching function.*consume_capacity_8|could not convert.*BoundedMonotonic")

# [FIXY-U-149] safety::OrderedAppendOnly<T, ...> HS14 floor — bumps
# OrderedAppendOnly from 1 → 2 fixtures.  Existing Class M
# (neg_pre_macro_ordered_appendonly_backward_key) pins the order-
# preservation CRUCIBLE_PRE inside append(); this Class T fixture
# pins the destructive-extraction ownership discipline of drain()'s
# `&&` ref-qualifier.  Closes the OrderedAppendOnly slice of #146
# A8-P2.
crucible_neg_compile_test(neg_orderedappendonly_drain_lvalue
  "passing.*as.*this.*argument discards qualifiers|cannot bind rvalue reference|no matching.*drain|discards qualifiers")

# [FIXY-U-151] safety::WriteOnceNonNull<T*> HS14 floor — Class M-set-
# null complement to the existing Class T-primary-template fixture
# (neg_writeoncenonnull_non_pointer).  Closes the WriteOnceNonNull
# slice of #146 A8-P2.
#
# The non-null clause inside set() (Mutation.h:737) is the load-
# bearing structural differentiator vs WriteOnce<T*> — WriteOnce<T*>
# accepts nullptr-engaged optional as a legal first set; the nullptr-
# sentinel storage model in WriteOnceNonNull makes "ptr_ == nullptr"
# reserved for "slot empty", so publishing null is indistinguishable
# from "never set" — always a caller bug.  CRUCIBLE_PRE under
# consteval fires __builtin_trap, poisoning the surrounding
# static_assert.
crucible_neg_compile_test(neg_pre_macro_writeoncenonnull_set_null
  "non-constant condition for static assertion|__builtin_trap|CRUCIBLE_PRE|in.*consteval|is not a constant expression")

# [FIXY-U-152] safety::AppendOnly<T> Class T-rvalue-only-drain HS14
# floor complement.  Existing coverage was entirely composition-rule
# side (neg_appendonly_over_writeonce + neg_appendonly_over_
# writeoncenonnull — both Class T-redundancy-static-assert at
# class-instantiation time).  This fixture pins the destructive-
# extraction ownership discipline at the drain() boundary: drain()'s
# `&&` ref-qualifier rejects lvalue invocation at overload-resolution
# time — same enforcement pattern as OrderedAppendOnly's drain
# fixture (U-149).  Closes the AppendOnly slice of #146 A8-P2.
crucible_neg_compile_test(neg_appendonly_drain_lvalue
  "passing.*as.*this.*argument discards qualifiers|cannot bind rvalue reference|no matching.*drain|discards qualifiers")

# [FIXY-U-153] safety::HugePageBuffer<T> + safety::SwissTableBuffer
# <SlotPtr> HS14 floor — paired move-only RAII buffer wrappers.
# Both wrappers shared a structurally identical move-only-RAII shape
# (deleted copy/copy-assign + defaulted move/move-assign + private
# direct ctor + factory `allocate()` + `std::free()` in dtor) but
# had ZERO neg-compile fixtures.  Mirrors the U-144 Pinned-copy +
# NonMovable-move sibling pattern: each wrapper gets ONE Class T-
# deleted-method fixture pinning a structurally distinct
# enforcement point (copy CTOR vs copy ASSIGN), and together the
# pair covers both halves of the deleted-copy discipline.
#
# Phase ctor (HugePageBuffer): copy construction triggers the
#   deleted ctor with "HugePageBuffer is move-only" message.
# Phase assign (SwissTableBuffer): copy assignment triggers the
#   deleted operator= with "SwissTableBuffer is move-only" message.
#
# Production sites that depend on the discipline: TraceRing hugepage
# backing, MetaLog hugepage SPSC, ExprPool/RecipePool Swiss-table
# control-byte+slot-array coupled regions.  In every site, the
# wrapper IS the ownership token — duplication via copy would
# corrupt the allocation lifetime.
crucible_neg_compile_test(neg_hugepagebuffer_copy_rejected
  "use of deleted function.*HugePageBuffer.*HugePageBuffer|HugePageBuffer is move-only|deleted")
crucible_neg_compile_test(neg_swisstablebuffer_copy_assign_rejected
  "use of deleted function.*SwissTableBuffer.*operator=|SwissTableBuffer is move-only|deleted")

# [FIXY-U-154] safety::FixedArray<T, N> HS14 floor — paired Class U-
# concept-gate fixtures.  Wrapper had ZERO neg-compile coverage
# despite shipping three structurally distinct concept-gate
# enforcement points:
#
# Phase class (FIXY-U-154a): `requires (N > 0)` on the class template
#   rejects FixedArray<T, 0> at substitution time — pins the
#   non-empty-array structural invariant.
# Phase method (FIXY-U-154b): `requires (I < N)` on `at<I>()` rejects
#   out-of-range compile-time indices at overload resolution — pins
#   the compile-time-bounded access discipline (tier 3 of
#   FixedArray's three-tier bounds-checking surface; tier 2 is the
#   Refined<bounded_above<N-1>> proof token, tier 1 is bare op[]).
#
# Both Class U but on DISTINCT scopes (class template vs method
# template) — two structurally separate enforcement points.
# Closes the FixedArray slice of #146 A8-P2.
crucible_neg_compile_test(neg_fixedarray_zero_extent
  "constraints not satisfied|'N > 0'|template constraint failure|N > 0.*evaluated to.*false|FixedArray")
crucible_neg_compile_test(neg_fixedarray_compile_time_out_of_range
  "constraints not satisfied|'I < N'|template constraint failure|I < N.*evaluated to.*false|no matching function.*at")

# [FIXY-U-155] safety::Pinned<> / NonMovable<> HS14 audit-remediation —
# completes the deleted-special-member surface that U-144 left half-
# covered.  Under strict HS14 reading each wrapper needs ≥2 distinct
# fixtures.  U-144 shipped Pinned-copy + NonMovable-move (the paired-
# sibling pattern, which proves the SHAPE is enforced but leaves each
# wrapper with only ONE fixture).  This batch ships the COMPLEMENTARY
# pair — Pinned-move + NonMovable-copy — so each wrapper now has TWO
# fixtures pinning TWO structurally distinct deleted-special-member-
# function enforcement points with distinct load-bearing reason
# strings:
#
#   Pinned<T> (Pinned.h:52-55):
#     * Class T-copy (U-144 neg_pinned_copy_rejected):
#       "stable address — address-as-identity or interior pointers
#       into own storage"
#     * Class T-move (U-155 THIS slice neg_pinned_move_rejected):
#       "stable address — move would invalidate references held by
#       another thread or by self"
#
#   NonMovable<T> (Pinned.h:71-74):
#     * Class T-move (U-144 neg_nonmovable_move_rejected):
#       "exclusive ownership — moved-from shell that callers may
#       mistake for valid"
#     * Class T-copy (U-155 THIS slice neg_nonmovable_copy_rejected):
#       "exclusive ownership — copy would duplicate a singleton
#       resource"
#
# Each reason string is independently load-bearing on the production
# surface — a refactor that softens any one of the four reason
# strings (or collapses them to a single `=delete` without a reason)
# slips past a single-fixture audit.  The full 4-fixture matrix locks
# all four deleted-special-member-function enforcement points across
# BOTH wrappers in the address-stability / exclusive-ownership CRTP
# family.
crucible_neg_compile_test(neg_pinned_move_rejected
  "stable address|use of deleted function|deleted function")
crucible_neg_compile_test(neg_nonmovable_copy_rejected
  "exclusive ownership|use of deleted function|deleted function")

# [FIXY-U-156] safety::HugePageBuffer<T> + safety::SwissTableBuffer
# <SlotPtr> HS14 audit-remediation — completes the move-only RAII
# deleted-surface that U-153 left half-covered.  Under strict HS14
# reading each wrapper needs ≥2 distinct fixtures.  U-153 shipped
# HugePageBuffer-copy-ctor + SwissTableBuffer-copy-assign (the
# paired-sibling pattern, which proves the SHAPE is enforced but
# leaves each wrapper with only ONE fixture).  This batch ships the
# COMPLEMENTARY pair — HugePageBuffer-copy-assign + SwissTableBuffer-
# copy-ctor — so each wrapper now has TWO fixtures pinning the two
# structurally distinct deleted-special-member-function enforcement
# boundaries (construction vs assignment):
#
#   HugePageBuffer<T> (HugePageBuffer.h:102-103):
#     * Class T-copy-ctor (U-153 neg_hugepagebuffer_copy_rejected):
#       direct copy construction triggers deleted ctor at the
#       construction boundary.
#     * Class T-copy-assign (U-156 THIS slice neg_hugepagebuffer_
#       copy_assign_rejected): copy-assignment from a const lvalue
#       triggers deleted operator= at the assignment boundary —
#       the rebuild()-then-assign production pattern.
#
#   SwissTableBuffer<SlotPtr> (SwissTableBuffer.h:82-83):
#     * Class T-copy-assign (U-153 neg_swisstablebuffer_copy_assign_
#       rejected): copy-assignment triggers deleted operator= at
#       the assignment boundary.
#     * Class T-copy-ctor (U-156 THIS slice neg_swisstablebuffer_
#       copy_rejected): direct copy construction triggers deleted
#       ctor at the construction boundary — the rebuild()-return
#       production pattern.
#
# Both boundaries (construction + assignment) are independently
# load-bearing — a refactor that softens either deleted op on either
# wrapper slips past a single-fixture audit.  The full 4-fixture
# matrix locks all four deleted-special-member-function enforcement
# points across BOTH move-only RAII buffer wrappers.
crucible_neg_compile_test(neg_hugepagebuffer_copy_assign_rejected
  "use of deleted function.*HugePageBuffer.*operator=|HugePageBuffer is move-only|deleted")
crucible_neg_compile_test(neg_swisstablebuffer_copy_rejected
  "use of deleted function.*SwissTableBuffer.*SwissTableBuffer|SwissTableBuffer is move-only|deleted")

# [FIXY-U-157] safety::PublishCommitCell<Tag, WriteAuth> HS14 floor —
# paired Class T fixtures.  Wrapper had ZERO neg-compile coverage
# despite being the structural fix for the GAPS-FLUSH-RACE bug class
# (test_vigil_dispatch / test_end_to_end / test_region_cache /
# test_vigil_deadline_watchdog).  Two structurally distinct
# enforcement points:
#
# Phase copy (FIXY-U-157a): deleted copy ctor with "owns the channel
#   identity" reason — pins the unique-atomic-per-channel invariant
#   that allows the wrapper's acquire/release pairing to hold.
# Phase access (FIXY-U-157b): private + friend-gated bump_by()/bump()
#   write surface — pins the "only WriteAuth (stage-4 publisher) may
#   advance the counter" structural access discipline that closes
#   the GAPS-FLUSH-RACE class.  Stage-3 (or any non-friend code)
#   trying to call bump triggers a private-access diagnostic at the
#   call site.
#
# Both Class T but on STRUCTURALLY DISTINCT enforcement points —
# `= delete("reason")` on a special member vs private + friend list
# on an instance method.  Together they pin BOTH soundness layers
# the wrapper ships.  Closes the PublishCommitCell slice of #146
# A8-P2.
crucible_neg_compile_test(neg_publishcommit_copy_rejected
  "use of deleted function.*PublishCommitCell.*PublishCommitCell|owns the channel identity|deleted")
crucible_neg_compile_test(neg_publishcommit_non_friend_bump
  "is private within this context|private|bump.*is.*private|access.*private")

# [FIXY-U-158] safety::AlignedBuffer<T, Alignment> HS14 floor —
# paired Class T fixtures completing the move-only RAII buffer trio
# (HugePageBuffer + SwissTableBuffer + AlignedBuffer).  Wrapper had
# ZERO neg-compile coverage despite shipping the SAME structural
# discipline as its U-153/U-156 siblings:
#
#   AlignedBuffer<T, A> (AlignedBuffer.h:89-91):
#     * Class T-copy-ctor (U-158a): direct copy construction triggers
#       deleted ctor at the construction boundary — pins the
#       factory-result return path.
#     * Class T-copy-assign (U-158b): copy-assignment from a const
#       lvalue triggers deleted operator= at the assignment boundary
#       — pins the rebuild()-then-assign production path.
#
# Both Class T but on STRUCTURALLY DISTINCT enforcement boundaries
# (construction vs assignment).  Closes the AlignedBuffer slice of
# #146 A8-P2 alongside U-153 (HugePageBuffer/SwissTableBuffer first
# half) and U-156 (HugePageBuffer/SwissTableBuffer second half).
crucible_neg_compile_test(neg_alignedbuffer_copy_ctor_rejected
  "use of deleted function.*AlignedBuffer.*AlignedBuffer|AlignedBuffer is move-only|deleted")
crucible_neg_compile_test(neg_alignedbuffer_copy_assign_rejected
  "use of deleted function.*AlignedBuffer.*operator=|AlignedBuffer is move-only|deleted")

# ─── FIXY-U-159: NonZero / NonEmpty / NonEmptySpan alias surface ───
#
# Closes the §XVI "every load-bearing predicate gets a named alias"
# discipline gap for the unparameterised `non_zero` and `non_empty`
# predicates, plus the §XVI-cited canonical `NonEmptySpan<T>` alias.
# Production sites previously defined ad-hoc local aliases inline
# (CallSiteTable.h:114 NonZeroHash); after this ship one canonical
# name participates in grep/review.
#
# Refined<Pred, T> aliases inherit the constructor's
# `requires PredicateInvocableOn<Pred, T>` concept gate, so the
# alias-level diagnostic surfaces at the alias name (NonZero<...>,
# NonEmpty<...>) rather than as a SFINAE wall inside <contracts>.
#
# HS14 floor: two fixtures with DISTINCT concept-failure surfaces:
#   * U-159a (non_zero else-branch): if-constexpr-requires dispatch
#     where the else-branch's `x != decltype(x){0}` substitution
#     fails (struct with no operator!=, no ctor-from-int).
#   * U-159b (non_empty method-missing): single-branch lambda body
#     where the lone `.empty()` member function requirement fails
#     for arithmetic types.
#
# Both pin "PredicateInvocableOn" as the diagnostic substring so a
# future Refined refactor that loses the concept gate would red
# both fixtures simultaneously.
crucible_neg_compile_test(neg_refined_nonzero_alias_rejects_no_compare_struct
  "PredicateInvocableOn|constraint.*not satisfied|no matching")
crucible_neg_compile_test(neg_refined_nonempty_alias_rejects_arithmetic
  "PredicateInvocableOn|constraint.*not satisfied|no matching")

# FIXY-U-160 — paired parameterised §XVI alias fixtures (HS14 floor):
#   * MinLength<N, T>     — Refined<length_ge<N>, T>
#   * MaxBounded<Max, T>  — Refined<bounded_above<Max>, T>
# Four fixtures across four distinct mismatch classes:
#   * Class C (arithmetic .size() missing) — MinLength<5, int>
#   * Class N (nested Refined breaks .size()) — MinLength<5, Refined<>>
#   * Class M (operator< missing on struct) — MaxBounded<10, NoLtStruct>
#   * Class T (heterogeneous string-vs-int compare) — MaxBounded<10u, string>
# All four pin "PredicateInvocableOn" as the diagnostic substring so a
# future Refined refactor that loses the concept gate would red all
# four fixtures simultaneously.
crucible_neg_compile_test(neg_refined_minlength_alias_rejects_arithmetic
  "PredicateInvocableOn|constraint.*not satisfied|no matching")
crucible_neg_compile_test(neg_refined_minlength_alias_rejects_nested_refined
  "PredicateInvocableOn|constraint.*not satisfied|no matching")
# MaxBounded fixtures: bounded_above<Max>'s operator() body
# unconditionally evaluates `x <= decltype(x)(Max)` — no
# requires-guarded if-constexpr branch like non_zero has — so the
# rejection surfaces at constexpr-expansion time as "no match for
# operator<=" rather than at concept-gate time as
# "PredicateInvocableOn".  The wider regex captures both legitimate-
# rejection surfaces; either reading means the predicate body is
# unable to evaluate against T, which is the discipline this fixture
# witnesses.
crucible_neg_compile_test(neg_refined_maxbounded_alias_rejects_no_lt
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")
crucible_neg_compile_test(neg_refined_maxbounded_alias_rejects_string_t
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")

# FIXY-U-161 — paired parameterised §XVI alias closure fixtures
# (AlignedTo + WithinRange).  Four fixtures across four distinct
# mismatch classes:
#   * Class P (scalar T offered, pointer required) — AlignedTo<64, int>
#   * Class R (reference T, also rejects auto* binding) — AlignedTo<64, int&>
#   * Class C (no ordering at all) — WithinRange<0, 100, NoOrderStruct>
#   * Class H (half-ordering interface, distinguishing in_range vs
#             bounded_above) — WithinRange<0, 100, OnlyLeqStruct>
# AlignedTo fixtures use PredicateInvocableOn concept-gate path (auto*
# substitution); WithinRange fixtures route through constexpr-
# expansion of the dual-comparison in_range body (similar to MaxBounded's
# bounded_above pattern).  Same wide-regex captures both routes.
crucible_neg_compile_test(neg_refined_alignedto_alias_rejects_scalar_t
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")
crucible_neg_compile_test(neg_refined_alignedto_alias_rejects_reference_t
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")
crucible_neg_compile_test(neg_refined_withinrange_alias_rejects_no_order
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")
crucible_neg_compile_test(neg_refined_withinrange_alias_rejects_half_ordering
  "PredicateInvocableOn|constraint.*not satisfied|no matching|no match for")

# [GCC-WRAPPER-TEXT] Same class as schema/ckernel above — production-
# side overload-set rejection in Cipher.  Same accepted-intrinsic
# rationale.
crucible_neg_compile_test(neg_cipher_wrong_tag
  "cannot convert.*SchemaTable.*Sealed.*Cipher::OpenView")

# ─── Session-type negative-compile tests (SEPLOG-J4, task #354) ───
#
# Each test is a .cpp file under test/safety_neg/ that deliberately
# triggers a session-type invariant violation.  The ctest passes IFF
# the build fails with the expected-diagnostic substring — strictly
# stronger than WILL_FAIL (which would accept any failure; here we
# require the PRECISE classified error).
#
# Coverage: one neg-compile per major layer's assertion contract.
# Adding a new session-type invariant should come with a new
# neg-compile test alongside it.

# L1 Session.h — well-formedness: Continue outside Loop.
crucible_neg_compile_test(neg_session_wellformed_free_continue
    "proto: protocol is ill-formed")

# L6 SessionSubtype.h — assert_subtype_sync on non-subtype pair.
crucible_neg_compile_test(neg_session_subtype_mismatch
    "T is not a synchronous subtype of U")

# L2 SessionContext.h — Context with duplicate (session, role) keys.
crucible_neg_compile_test(neg_session_context_duplicate_keys
    "entries must have pairwise-distinct")

# L2 SessionContext.h — compose_context_t with overlapping domains.
crucible_neg_compile_test(neg_session_context_compose_overlap
    "share one or more")

# L2 SessionContext.h — lookup_context_t on missing (session, role).
crucible_neg_compile_test(neg_session_lookup_missing_entry
    "has no entry for")

# L4 SessionGlobal.h — global type with Var_G outside Rec_G.
# is_global_well_formed_v returns false; the static_assert in this
# test expects it to be true, so the assert fails.
crucible_neg_compile_test(neg_session_global_free_var
    "should be ill-formed")

# L4 SessionGlobal.h — self-transmission Transmission<X, X, ...>
# (#363).
#
# [FRAMEWORK-CONTROLLED] assert_no_self_loop<G>() emits the routed
# [ProtocolViolation_Self_Loop] tag — stable across GCC versions.
crucible_neg_compile_test(neg_session_global_self_transmission
    "ProtocolViolation_Self_Loop")

# L3 SessionQueue.h — head_queue_t on an empty queue.
crucible_neg_compile_test(neg_session_queue_head_empty
    "Q is empty or is not a Queue")

# L5 SessionAssoc.h — assert_associated on Γ missing a role.
crucible_neg_compile_test(neg_session_assoc_missing_role
    "condition .1. fails")

# L8 SessionCrash.h — assert_has_crash_branch_for on missing branch.
crucible_neg_compile_test(neg_session_crash_branch_missing
    "lacks a Recv<Crash<PeerTag>, _> branch")

# CheckpointedSession combinator — mismatched base-branch assertion.
crucible_neg_compile_test(neg_session_checkpoint_wrong_base
    "base branch does not match")

# L11 SessionDiagnostic.h — diagnostic_name_v on non-tag type.
#
# [FRAMEWORK-CONTROLLED] Per #371, the accessor variable templates
# now route their concept-rejection through a `detail::diag::accessor_check`
# helper that fires a static_assert with the framework-controlled
# "[DiagnosticAccessor_NonTag]" prefix.  Stable across GCC versions.
crucible_neg_compile_test(neg_session_diagnostic_non_tag
    "DiagnosticAccessor_NonTag")

# Concurrent — SchedulerPolicy accessor on non-policy type.
#
# [FRAMEWORK-CONTROLLED] Same #371 pattern: the scheduler accessor
# variable templates route through `detail::sched::accessor_check`
# and emit "[SchedulerAccessor_NonPolicy]" on rejection.
crucible_neg_compile_test(neg_scheduler_policy_non_inheritance
    "SchedulerAccessor_NonPolicy")

# SessionHandle::detach() called without a typed reason tag (#376).
#
# [FRAMEWORK-CONTROLLED] The zero-arg overload is `= delete("...")`
# with the framework-controlled "[DetachReason_Required]" prefix; the
# templated detach<Reason>(Reason) requires `DetachReason<Reason>` so
# the deleted overload outranks it for a zero-arg call.  Stable across
# GCC versions because the framework owns the deleted-overload string.
crucible_neg_compile_test(neg_session_detach_no_reason
    "DetachReason_Required")

# Direct CrashEvent construction is rejected (#430) — the pass-key
# `WrapCrashReturnKey` has a private default ctor and friends only
# the `wrap_crash_return` helper, so wrapper code that tries to
# fabricate a CrashEvent without going through the helper (the bug
# pattern from #400 — return std::unexpected without first calling
# inner.detach()) gets a crisp compile error naming the pass-key.
# This makes the silent-abandonment-via-inner-destructor failure
# mode structurally impossible at the type level, complementing
# #429's runtime diagnostic improvements.
crucible_neg_compile_test(neg_crash_event_direct_construction
    "WrapCrashReturnKey")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-030 — on_crash returns void to prevent moved-from inspection
# ─────────────────────────────────────────────────────────────────
#
# Pre-fix `proto::on_crash(Expected&& result, CrashHandler&& handler)`
# returned `Expected&&` AFTER moving out `result.error()` into the
# handler.  The returned Expected silently carried a moved-from
# error.  A caller writing:
#
#   auto leftover = on_crash(result, [](auto){});  // moved-from
#   if (!leftover) read(leftover.error());          // UB
#
# or chaining inline:
#
#   on_crash(result, [](auto){}).error()            // moved-from inline
#
# read undefined data: unique_ptr → nullptr, trivially-moved struct
# → zero bytes, partially-moved struct → undefined per-field.  The
# footgun was invisible at the type level — the same `Expected&&`
# return carried both pre-handler (well-formed error) and
# post-handler (moved-from error) data with no structural
# distinction.
#
# fixy-A2-030 changes `on_crash` to return `void`.  Inspection
# becomes a compile error structurally:
#   * variable capture: `auto x = on_crash(...);` — void can't
#     deduce auto
#   * chained access:   `on_crash(...).error()` — void has no members
#
# The pair of fixtures below witnesses both rejection paths:
# fixture #1 pins the variable-capture failure mode, fixture #2 pins
# the chained-member-access failure mode.  Together they pin that
# fixy-A2-030's contract holds against BOTH common moved-from
# inspection idioms.
crucible_neg_compile_test(neg_on_crash_capture_return_value
    "void|incomplete type|cannot|fixy-A2-030")
crucible_neg_compile_test(neg_on_crash_inspect_moved_from_error
    "void|not a class|incomplete|fixy-A2-030")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-033 — CrashEvent Resource must be nothrow-move-constructible
# ─────────────────────────────────────────────────────────────────
#
# Pre-fix `CrashEvent(WrapCrashReturnKey, Resource r, ...)` took
# Resource by value, forcing one extra move at the ctor boundary.
# Post-fix takes `Resource&& r` and saves one move per crash event
# (~3µs for a 16KB Resource at 5GB/s memcpy).  The accompanying
# class-body `static_assert(std::is_nothrow_move_constructible_v<
# Resource>)` locks the invariant that the field-move-init —
# called inside CrashEvent's `noexcept` ctor — never throws.
# Without that assert, a Resource refactor adding a non-noexcept
# member would silently reintroduce mid-recovery `std::terminate`.
# This fixture witnesses that the assert fires on a Resource with
# `noexcept(false)` move ctor — i.e., the static_assert IS load-
# bearing rather than tautological.
crucible_neg_compile_test(neg_crash_event_resource_throwing_move
    "fixy-A2-033|nothrow-move-constructible|static assertion failed|noexcept")

# Empty Select<> / Offer<> at the handle boundary (#364) — type-level
# uses (Gay-Hole 2005 minimum-subtype rule, SessionSubtype.h's tests)
# stay legitimate, but constructing a runnable handle is rejected.
crucible_neg_compile_test(neg_session_empty_select
    "Empty_Choice_Combinator")

# fixy-CR-14: HS14 floor for the recursive is_empty_choice walk.
# Distinct rejection paths: Loop → Recv → empty Select, and Send →
# empty Offer.  Both protocols pass is_well_formed at the type level
# (no free Continue, Loop body is well-formed) but the mint now
# rejects them because a reachable empty choice exists anywhere in
# the tree.
crucible_neg_compile_test(neg_session_empty_select_nested_loop
    "Empty_Choice_Combinator")
crucible_neg_compile_test(neg_session_empty_offer_nested_send
    "Empty_Choice_Combinator")

# fixy-CR-14 follow-up: SessionDelegate.h's Delegate/Accept/
# EpochedDelegate/EpochedAccept is_empty_choice specs.  Two fixtures
# pin distinct OR arms: Delegate<Select<>, End> exercises the T
# (delegated-inner) arm; Accept<End, Select<>> exercises the K
# (continuation) arm.  Together they witness the SessionDelegate.h
# specs at compile time.
crucible_neg_compile_test(neg_session_empty_in_delegate_inner
    "Empty_Choice_Combinator")
crucible_neg_compile_test(neg_session_empty_in_accept_continuation
    "Empty_Choice_Combinator")

# fixy-CR-15: ProtocolPermissionedRunnable runnable-trait gate on
# empty Select<> / Offer<> / Offer<Sender<R>> at mint_channel.
# Three fixtures pin distinct empty-Choice shapes: bare Select<>,
# bare Offer<>, and Sender-annotated empty Offer.  The trait was
# previously vacuous-true on empty branch packs (AND-fold identity);
# CR-15's explicit false_type specializations sit above the variadic
# ones in partial ordering.
crucible_neg_compile_test(neg_channel_runnable_empty_select
    "ProtocolPermissionedRunnable|CtxFitsChannel|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_channel_runnable_empty_offer
    "ProtocolPermissionedRunnable|CtxFitsChannel|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_channel_runnable_empty_offer_sender
    "ProtocolPermissionedRunnable|CtxFitsChannel|constraints not satisfied|no matching function")

# pick<I> / select<I> out-of-range index (#432, #433) — replaces the
# generic GCC "constraints not satisfied" message with a framework-
# named [Branch_Index_Out_Of_Range] diagnostic carrying the protocol
# context.  Covers BOTH Select::select<I> (internal choice) and
# Offer::pick<I> (peer-driven branch dispatch) in one file.
crucible_neg_compile_test(neg_session_pick_index_out_of_range
    "Branch_Index_Out_Of_Range")

# Endpoint-pair duality mismatch (#431) — the framework's main
# safety promise is that dual endpoints can never deadlock.
# ensure_dual<P1, P2>() catches any pair-up mistake at the
# establishment site with [Dual_Mismatch].
crucible_neg_compile_test(neg_session_dual_mismatch
    "Dual_Mismatch")

# fixy-A2-022 — HS14 floor for symmetric `is_dual_v` over the
# Sender-annotated Offer asymmetric case.  Two fixtures witness that
# `ensure_dual` still REJECTS genuinely-non-dual pairs after the
# OR-disjunction fix:
#   reversed-orientation — branch-count mismatch in reverse argument order
#   branches            — element-wise payload-type drift on the
#                         Sender-Offer shape (regression test that the
#                         OR doesn't relax detection)
crucible_neg_compile_test(neg_session_dual_mismatch_sender_offer_reversed
    "static assertion failed|Dual_Mismatch|ensure_dual")
crucible_neg_compile_test(neg_session_dual_mismatch_sender_offer_branches
    "static assertion failed|Dual_Mismatch|ensure_dual")

# fixy-A2-023 — HS14 floor for the `is_dual_involutive_v` gate on
# `refines_self_and_double_dual_v` (SessionPatterns.h:944+).
# Two fixtures witness rejection of non-involutive shapes:
#   sender_offer        — direct Offer<Sender<R>, B> as P
#   nested_sender_offer — Loop<Offer<Sender<R>, ...>> — propagation case
crucible_neg_compile_test(neg_refines_self_double_dual_sender_offer
    "static assertion failed|refines_self_and_double_dual|is_dual_involutive")
crucible_neg_compile_test(neg_refines_self_double_dual_nested_sender_offer
    "static assertion failed|refines_self_and_double_dual|is_dual_involutive")

# fixy-A2-024 — HS14 floor for the orthogonal two-gate composition at
# `mint_session_handle` over `CheckpointedSession<B, R>`.  Documents
# (in SessionCheckpoint.h doc-blocks) and witnesses (in these two
# fixtures) that `is_well_formed_v` (structural Continue/Loop
# placement) and `is_empty_choice_v` (reachable Choice runnability)
# are intentionally orthogonal traits BOTH composed at the mint site:
#   ill_formed_base       — Continue outside Loop in base branch →
#                           [Protocol_Ill_Formed] from is_well_formed_v
#   empty_choice_recovery — empty Select<> in recovery branch →
#                           [Empty_Choice_Combinator] from is_empty_choice_v
# The two gates catch different defect classes; neither subsumes the
# other.  These fixtures complement the trait-level static_asserts in
# `fixy_a2_004_is_empty_choice_checkpointed` (SessionCheckpoint.h:601+)
# by exercising the mint-level composition.
crucible_neg_compile_test(neg_session_mint_checkpointed_ill_formed_base
    "static assertion failed|Protocol_Ill_Formed|is_well_formed")
crucible_neg_compile_test(neg_session_mint_checkpointed_empty_choice_recovery
    "static assertion failed|Empty_Choice_Combinator|is_empty_choice")

# Cipher federation MPST order gates (GAPS-039).  These fixtures use
# concept probes plus framework-controlled static_assert messages so
# drift in method availability is surfaced without depending on GCC's
# overload text.
crucible_neg_compile_test(neg_federation_sender_recv_before_announce
    "FederationSender_RecvBeforeAnnounce_Rejected")
crucible_neg_compile_test(neg_federation_receiver_send_before_pull
    "FederationReceiver_SendBeforePull_Rejected")
crucible_neg_compile_test(neg_federation_coord_non_coord_role
    "FederationCoord_NonCoordRole_Rejected")
crucible_neg_compile_test(neg_federation_wrong_ack_key
    "FederationAck_ContentHashMismatch_Rejected")
crucible_neg_compile_test(neg_federation_deserialize_without_permission
    "no matching function|candidate expects")
crucible_neg_compile_test(neg_federation_deserialize_wrong_org_permission
    "no matching function|could not convert|invalid initialization")

# Bare select<I>() / pick<I>() (no transport, no `_local` suffix) are
# now `= delete`'d (#377) so the wire-vs-in-memory choice is forced
# to be explicit at every call site.  Pre-#377 the bare form silently
# selected the in-memory variant — wire-based sessions silently drifted.
crucible_neg_compile_test(neg_session_select_no_wire
    "Wire_Variant_Required")

# Bare `delegate(handle)` (no transport) on a Delegate state is now
# `= delete`'d (#369) — same Wire_Variant_Required discipline as
# select/pick (#377), specialized for Delegate's "ship to peer"
# semantic.  Without the transport, the peer's Accept hangs forever
# waiting for an endpoint that was never shipped.
crucible_neg_compile_test(neg_session_delegate_no_wire
    "Wire_Variant_Required")

# compose_at_branch_t<P, I, Q> — branch-asymmetric composition (#378).
# Replaces only branch I's continuation in the first reachable
# Select<Bs...> / Offer<Bs...> in P's spine.  Two named diagnostics
# fire on misuse: out-of-range I and no-choice-in-spine.
crucible_test(test_session_compose_at_branch)

crucible_neg_compile_test(neg_session_compose_at_branch_index
    "Branch_Compose_Index_Out_Of_Range")
crucible_neg_compile_test(neg_session_compose_at_branch_no_choice
    "Branch_Compose_No_Choice")

# Variadic safe_add_all<T, Xs...> (#134) — partial-sum overflow.
# Same [Checked_Capacity_Overflow] prefix as safe_add / safe_mul;
# the helper detects overflow at each fold step, not just the final
# result.
crucible_neg_compile_test(neg_checked_add_all_overflow
    "Checked_Capacity_Overflow")

# ensure_bytes_fit<Budget, Used>() (#134) — budget exceeded.  Used
# for compile-time verification of struct-of-arrays layouts,
# cache-line-tight metadata, arena page footprints.  The named
# [Byte_Budget_Exceeded] diagnostic lists common causes so the
# developer doesn't have to re-derive them.
crucible_neg_compile_test(neg_checked_bytes_fit_exceeded
    "Byte_Budget_Exceeded")

# Crash-walker deep-tree rejection (#368).
#
# [FRAMEWORK-CONTROLLED] assert_every_offer_has_crash_branch_for
# fires the "[CrashBranch_Missing_In_Tree]" prefix when a deep Offer
# hidden inside a continuation lacks the Recv<Crash<Peer>, _> branch.
# Covers the gap that per-node has_crash_branch_for_peer_v misses.
crucible_neg_compile_test(neg_session_crash_branch_tree_missing
    "CrashBranch_Missing_In_Tree")

# Compile-time capacity arithmetic — overflow rejection (#408).
#
# [FRAMEWORK-CONTROLLED] safe_mul_impl / safe_sub_impl fire a
# static_assert with the "[Checked_Capacity_Overflow]" prefix on
# overflow / underflow.  Two scenarios cover both directions: SIZE_MAX
# * 2 (multiplication overflow) and 10 - 100 (unsigned subtraction
# underflow).
crucible_neg_compile_test(neg_checked_safe_capacity_overflow
    "Checked_Capacity_Overflow")
crucible_neg_compile_test(neg_checked_safe_size_diff_underflow
    "Checked_Capacity_Overflow")

# Secret<T>::transform(f) capture-leak rejection (#151).
#
# [FRAMEWORK-CONTROLLED] Secret.h's transform() fires two named
# static_asserts when f would leak classified data:
#   - [Capture_Leak_Reference_Return] — f returns a reference that
#     aliases the moved-from secret storage OR a member of f's
#     closure, silently declassifying without the declassify<Policy>
#     audit trail.
#   - [Capture_Leak_Void_Return] — f returns void, producing the
#     meaningless Secret<void>; the likely intent is a side-effecting
#     observation and belongs in declassify<Policy>() instead.
crucible_neg_compile_test(neg_secret_transform_reference_return
    "Capture_Leak_Reference_Return")
crucible_neg_compile_test(neg_secret_transform_void_return
    "Capture_Leak_Void_Return")

# H-24: Secret<T>::declassify<Policy>() must reject any Policy that
# does NOT derive from `secret_policy::secret_policy_base`.  Pre-H-24
# the `DeclassificationPolicy` concept admitted ANY non-void class
# type, leaving the `grep "declassify<secret_policy::"` audit
# trail as a convention rather than a type-system gate.  H-24
# tightened the concept (via std::derived_from) AND surfaces a
# named [SecretPolicy_NotInBase] diagnostic at the call site.
#
# Two fixtures cover the two clauses of the tightened concept:
#   #1 — class type but NOT derived from secret_policy_base
#         (fails `std::derived_from` clause).
#   #2 — primitive type (int), not a class at all
#         (fails `std::is_class_v` clause).
# Per HS14, every new mint factory / gate ships ≥2 neg-compile
# fixtures.  declassify<Policy>() is a soundness gate; the
# tightening lands with its negative witnesses.
crucible_neg_compile_test(neg_secret_declassify_non_policy_class
    "SecretPolicy_NotInBase|DeclassificationPolicy|constraints not satisfied")
crucible_neg_compile_test(neg_secret_declassify_non_class_type
    "SecretPolicy_NotInBase|DeclassificationPolicy|constraints not satisfied")

# Sender-annotated Offer missing its sender's crash branch (#367).
#
# [FRAMEWORK-CONTROLLED] Reuses the existing
# [CrashBranch_Missing_In_Tree] diagnostic from
# `assert_every_offer_has_crash_branch_for<Proto, PeerTag>()`: when an
# Offer<Sender<Role>, Branches...> declares Role AND the queried
# PeerTag == Role, the crash branch becomes mandatory.  An Offer
# from OTHER roles is silently accepted — "not your peer, not your
# problem."  The neg-test triggers the sender-matches branch to
# prove the walker correctly distinguishes the two paths.
crucible_neg_compile_test(neg_session_sender_role_missing_crash
    "CrashBranch_Missing_In_Tree")

# ── A8-P3 #148 — NotInherited<T>/FinalBy<T> primitive ──────────────
# Caller-side witness form: assert_not_inherited<T>() rejects a T
# that is not declared `final` with the named diagnostic
# `[NotInherited_Not_Final]`.  This neg-test passes a non-final
# struct to the helper and checks the diagnostic by name — exercising
# the WITNESS half of the FinalBy/NotInherited pair documented in
# include/crucible/safety/NotInherited.h.  The ENFORCEMENT half
# (FinalBy<Derived> CRTP private-ctor) carries `[FinalBy_Subclass_
# Forbidden]` in every deleted-copy/move reason; a separate neg-test
# for that is task #146 (broader neg-compile coverage sweep).
crucible_neg_compile_test(neg_notinherited_non_final
    "NotInherited_Not_Final")

# ── A8-P3 #148 — FinalBy<Derived> CRTP enforcement half ───────────
# Companion to neg_notinherited_non_final.cpp.  Where the witness
# half exercises a concept on the CALLER side, this exercises the
# CRTP private-ctor + virtual-inheritance discipline on the LIBRARY
# side: a class declared `final-by-CRTP` (rather than with the
# `final` keyword) must remain just as inextensible.  Subclass
# construction fails because virtual inheritance promotes
# FinalBy<MyType>'s private ctor responsibility to the most-derived
# class, but the subclass is not a friend.
crucible_neg_compile_test(neg_finalby_subclass_attempt
    "FinalBy")

# ── A8-P2 #146 — Linear<T> move-only enforcement ────────────────
# Foundational L0 ownership wrapper.  Copy-constructing a Linear<T>
# would create two simultaneous owners and allow double-consume,
# defeating the very contract the wrapper exists to prove.  The
# copy ctor is = delete with reason "Linear<T> is move-only".
crucible_neg_compile_test(neg_linear_copy_attempt
    "move-only")

# ── fixy-A1-004 #1546 — Linear<Permission<Tag>> redundant-stacking ─
# Linear<T> rejects T = Permission<Tag> / SharedPermission<Tag> via
# is_already_linear_v<T> static_assert.  Permission is itself a
# move-only linearity token (CLAUDE.md §XVI); wrapping in Linear<>
# stacks two disciplines, adds no new bug class, and breaks the
# §XXI grep discipline by routing Permission synthesis through
# mint_linear<Permission<Tag>> instead of the canonical
# mint_permission_root / mint_permission_share factory.
# Two HS14 fixtures cover both branches of the trait:
#   #1 exclusive-token  : Linear<Permission<Tag>>
#   #2 fractional-token : Linear<SharedPermission<Tag>>
crucible_neg_compile_test(neg_linear_of_permission_redundant
    "redundant")
crucible_neg_compile_test(neg_linear_of_shared_permission_redundant
    "redundant")
# fixy-A1-028 #1565 / fixy-L-01 #1517 — close doc-vs-code drift:
# the original A1-004 trait specialization matched bare
# Permission<Tag> / SharedPermission<Tag> exactly, so cv-qualified
# variants slipped past static_assert.  Public trait now strips via
# remove_cvref_t.  Two additional HS14 fixtures cover the 2-D
# closure (Permission × const-qualified ⊕ SharedPermission ×
# volatile-qualified) — distinct mismatch classes per HS14 floor.
crucible_neg_compile_test(neg_linear_of_const_permission_redundant
    "redundant")
crucible_neg_compile_test(neg_linear_of_volatile_permission_redundant
    "redundant")

# ── fixy-A1-005 #1547 — §XXI mint_tagged/refined/sealed_refined ──
# CLAUDE.md §XXI Universal Mint Pattern: every cross-tier
# composition factory is named `mint_<noun>` so `grep "mint_"`
# finds every authorization point.  Tagged / Refined /
# SealedRefined construction was previously bypassing §XXI by
# routing through raw class-template constructors.  Three new
# factories restore the grep discipline.  Six HS14 neg-compile
# fixtures cover both concept-gate failure modes per factory:
#
#   mint_tagged<Tag, T> — ValidTaggedTag<Tag>:
#     #1 scalar tag (int) rejection
#     #2 pointer tag (int*) rejection
#   mint_refined<Pred, T> — PredicateInvocableOn<Pred, T>:
#     #1 predicate-arg-mismatch (Pred not invocable on T)
#     #2 predicate-return-mismatch (void return)
#   mint_sealed_refined<Pred, T> — PredicateInvocableOn<Pred, T>:
#     #1 predicate-arg-mismatch (mirrors mint_refined)
#     #2 predicate-return-mismatch (mirrors mint_refined)
crucible_neg_compile_test(neg_mint_tagged_scalar_tag_rejected
    "ValidTaggedTag")
crucible_neg_compile_test(neg_mint_tagged_pointer_tag_rejected
    "ValidTaggedTag")
crucible_neg_compile_test(neg_mint_refined_predicate_arg_mismatch
    "PredicateInvocableOn")
crucible_neg_compile_test(neg_mint_refined_predicate_return_mismatch
    "PredicateInvocableOn")
crucible_neg_compile_test(neg_mint_sealed_refined_predicate_arg_mismatch
    "PredicateInvocableOn")
crucible_neg_compile_test(neg_mint_sealed_refined_predicate_return_mismatch
    "PredicateInvocableOn")

# ── fixy-L-04 #1520 — mint_secret §XXI gate witness ─────────────
# CLAUDE.md §XXI Universal Mint Pattern: every authorization
# factory is named `mint_<noun>`.  mint_secret's docstring
# previously overclaimed "canonical authorization point";
# Secret<T> ships an open public ctor + CTAD guide (mirrors
# mint_linear / mint_tagged precedent), so mint_secret is the
# §XXI-grep-discoverable surface, NOT a stricter chokepoint.
# The real chokepoint is `declassify<Policy>()`.  Two HS14
# fixtures witness that the mint's
# `requires std::is_constructible_v<T, Args...>` gate fires
# across two distinct mismatch classes:
#   #1 wrong-arg-type — int not constructible from const char*
#   #2 deleted-ctor   — class with explicit `= delete`'d ctor
crucible_neg_compile_test(neg_secret_mint_unconstructible_args
    "constraints not satisfied|no matching function|is_constructible|mint_secret|deduction/substitution failed")
crucible_neg_compile_test(neg_secret_mint_deleted_ctor
    "constraints not satisfied|no matching function|use of deleted function|is_constructible|mint_secret|deduction/substitution failed")

# ── A8-P2 #146 — Permission<Tag> linearity enforcement ──────────
# Foundational CSL frame-rule token (O'Hearn 2007).  Duplicating a
# Permission<Tag> would create two simultaneous owners of the same
# named region, breaking CSL's frame rule.  Copy ctor is = delete
# with reason mentioning "linear — duplicating creates two
# simultaneous owners".
crucible_neg_compile_test(neg_permission_copy_attempt
    "linear")

# PublishSlot<T> owns an atomic publication cell.  Copying it would
# duplicate channel identity and split observers across two unrelated
# slots, so the copy constructor is deleted with an explicit reason.
crucible_neg_compile_test(neg_publish_slot_copy_attempt
    "publication slot identity cannot be copied")
crucible_neg_compile_test(neg_vigil_copy_attempt
    "Vigil owns the runtime organism; not copyable")
crucible_neg_compile_test(neg_vigil_mode_bridge_illegal_transition
    "VigilModeBridge_IllegalTransition|use of deleted function")
crucible_neg_compile_test(neg_vigil_mode_bridge_branch_out_of_range
    "Branch_Index_Out_Of_Range|branch index")

# WRAP-Vigil-7 (#1077) — Vigil::AlignmentPos = safety::Refined<safety::
# bounded_above<ALIGNMENT_POS_MAX=ALIGNMENT_K=5>, uint8_t>; constructing
# AlignmentPos{> 5} in constexpr context drives the bounded_above
# pre-clause through constant evaluation.  Two fixtures per HS14:
#   * _above_max  — boundary edge (6 = ALIGNMENT_K+1, the off-by-one
#     past the threshold check at try_align_'s `>= threshold` branch),
#     catches drift in ALIGNMENT_POS_MAX (e.g., a refactor that lowers
#     the bound to ALIGNMENT_K - 1 silently passes the wide-miss
#     fixture but fails this one).
#   * _uint8_max  — wide miss (255 = uint8_t max), catches "lost
#     narrowing" where a wider intermediate is truncated to uint8_t
#     before being assigned (the migration risk if a future
#     refactor drops the Refined wrap and admits raw byte writes).
crucible_neg_compile_test(neg_vigil_alignment_pos_above_max
    "contract|bounded_above|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_vigil_alignment_pos_uint8_max
    "contract|bounded_above|not a constant|non-constant|constexpr")

# GAPS-044 — Permission inheritance is explicit and acyclic.  Crash-stop
# recovery may mint survivor permissions only for declared inheritance
# edges; a peer cannot inherit to itself.
crucible_neg_compile_test(neg_mint_permission_inherit_without_specialization
    "mint_permission_inherit requires inherits_from")
crucible_neg_compile_test(neg_mint_permission_inherit_self_cycle
    "mint_permission_inherit forbids circular inheritance")

# fixy-H-25 — mint_permission_inherit<DeadTag, SurvivorTags...>() must require
# a crash_witness_key, the proof-of-death passkey mintable only by
# bridges::wrap_crash_return.  Two fixtures cover the two failure modes:
#   #1 — call the factory with no witness (parameter-count rejected).
#   #2 — try to construct crash_witness_key directly (default ctor deleted;
#        WrapCrashReturnKey passkey requires private-ctor access).
crucible_neg_compile_test(neg_permission_inherit_no_witness
    "too few arguments|no matching function|mint_permission_inherit")
crucible_neg_compile_test(neg_permission_inherit_forge_witness
    "use of deleted function|no matching function|is private|crash_witness_key")

# GAPS-073 — permission_fork is now a ctx-bound mint.  These fixtures
# pin the three load-bearing gates: ctx-aware callables must provide a
# Ctx, foreground rows cannot authorize Effect::Bg, and spawned child
# bodies inherit the exact row-admitted Ctx type.
crucible_neg_compile_test(neg_permission_fork_no_ctx
    "no matching function|candidate expects|constraints not satisfied")
crucible_neg_compile_test(neg_permission_fork_ctx_no_bg
    "CtxFitsPermissionFork|row_contains|constraints not satisfied")
crucible_neg_compile_test(neg_permission_fork_child_ctx_mismatch
    "permission_fork_ctx_callables|constraints not satisfied|no matching function")

# fixy-A1-009 — `permission_fork_rebuild_<T>()` is no longer a public
# free function.  Post-join `Permission<T>` rebuild now routes through
# `detail::ForkRebuildAccess::rebuild<T>(ForkRebuildKey{})`, where
# `ForkRebuildKey`'s default ctor is private and `rebuild_parent_after_fork_`
# is the sole friend.  Two distinct mismatch classes:
#   #1: legacy free-function name is gone — name lookup fails.
#   #2: user-side construction of `ForkRebuildKey{}` hits the private ctor.
crucible_neg_compile_test(neg_permission_fork_rebuild_free_function_call
    "has not been declared|was not declared|no member named|not a member of")
crucible_neg_compile_test(neg_permission_fork_rebuild_key_private_ctor
    "is private within this context|constructor is private|cannot access private")

# fixy-A1-011 HS14 floor — Permission<Tag>'s primary template gates
# `Tag` through the `PermissionTag` concept (empty non-union class
# type).  Three distinct rejected shapes mirror the three failure
# modes of the pre-fix surface: primitives, pointer-to-class, and
# stateful structs.  Pre-fix all three compiled silently, carrying
# zero CSL guarantee; post-fix the class-body static_assert fires
# with `PermissionTag` named in the diagnostic.
crucible_neg_compile_test(neg_permission_non_class_tag
    "static assertion failed|PermissionTag|Tag must be an empty non-union class")
crucible_neg_compile_test(neg_permission_pointer_tag
    "static assertion failed|PermissionTag|Tag must be an empty non-union class")
crucible_neg_compile_test(neg_permission_non_empty_tag
    "static assertion failed|PermissionTag|Tag must be an empty non-union class")

# fixy-A1-012 HS14 floor — `Fd` newtype + `FileHandle(int)` ctor reject
# errno-shaped negatives.  Pre-fix `FileHandle{-9}` (≈ -EBADF) constructed
# an apparently-closed handle that silently swallowed the real error.
# Post-fix `CRUCIBLE_PRE(value == -1 || value >= 0)` fires at consteval
# AND runtime; only the POSIX-shape pattern (closed sentinel or non-
# negative fd) survives.  Two fixtures cover the two boundary types.
crucible_neg_compile_test(neg_fd_errno_shaped_negative
    "non-constant condition|__builtin_trap|contract violation|static assertion")
crucible_neg_compile_test(neg_file_handle_errno_shaped_negative
    "non-constant condition|__builtin_trap|contract violation|static assertion")

# fixy-A1-014 HS14 floor — `ct::eq` is a span-only API and fires
# CRUCIBLE_PRE on length mismatch.  Pre-fix `(ptr, ptr, size_t)` silently
# dereferenced null at any non-zero length and swallowed sized
# asymmetry as `n == 0 → true`.  Post-fix two fixtures pin the two
# rejection classes: (#1) two non-empty mismatched sizes, (#2) empty
# vs non-empty (asymmetric edge).
crucible_neg_compile_test(neg_ct_eq_length_mismatch_consteval
    "non-constant condition|__builtin_trap|contract violation|static assertion")
crucible_neg_compile_test(neg_ct_eq_zero_vs_nonempty_consteval
    "non-constant condition|__builtin_trap|contract violation|static assertion")

# fixy-CR-13 — substrate-side HS14 floor for federation::mint_* row
# admission.  Pairs with the fixy-wrapper-side fixtures in fixy_neg/.
# Two distinct rejection classes (Fg empty row × BgCompile missing
# Block), exercising mint_channel + mint_sender entry points.
crucible_neg_compile_test(neg_federation_mint_no_row_fg
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_federation_mint_no_row_bg_compile
    "CtxFitsFederation|EffectRowMismatch|constraints not satisfied|no matching function")

# fixy-A2-009 — substrate-side HS14 floor for the SharedPermission
# fractional-admittance discipline.  Two distinct rejection classes
# both targeting the per-role mint parameter-type concept:
#   * raw Permission instead of SharedPermission — Linear move-only
#     vs copyable empty proof token (no implicit conversion).
#   * SharedPermission Org mismatch — passing OrgA's token to
#     mint_sender<OrgB> (cross-org impersonation gap closed at type
#     level; tag carries Org).
crucible_neg_compile_test(neg_federation_mint_raw_permission
    "no matching function|could not convert|cannot convert|invalid initialization|deduction/substitution failed")
crucible_neg_compile_test(neg_federation_mint_wrong_org_share
    "no matching function|could not convert|cannot convert|invalid initialization|deduction/substitution failed")

# fixy-L-02 #1518 — substrate-side HS14 floor for
# `mint_self_signed_handshake<Org>` §XXI mint gate.
# Two distinct rejection classes both targeting FederationOrgTag<Org>:
#   * non-class type (int)        — fails std::is_class_v half
#   * non-empty class (data field) — fails std::is_empty_v half
# Per §XXI Universal Mint Pattern: every new mint factory ships with
# ≥2 negative-compile fixtures demonstrating distinct mismatch classes.
crucible_neg_compile_test(neg_federation_mint_self_signed_non_class_org
    "constraints not satisfied|no matching function|FederationOrgTag|is_class_v|deduction/substitution failed")
crucible_neg_compile_test(neg_federation_mint_self_signed_non_empty_org
    "constraints not satisfied|no matching function|FederationOrgTag|is_empty_v|deduction/substitution failed")

# fixy-A2-010 — substrate-side HS14 floor for the higher-order
# capability-transfer fix on `payload_row<DelegatedSession<InnerProto,
# InnerPS>>`.  Before the fix the trait fell through to the primary
# template's `Row<>` default, silently hiding the inner protocol's
# effects from the outer Ctx's row admission walk.  After the fix the
# trait returns `protocol_effect_row_t<InnerProto>`, so the outer
# Ctx must admit the union of effects every Send/Recv in the inner
# protocol carries.  Two distinct rejection classes:
#   * Fg empty-row Ctx + inner protocol with Row<IO>          (full miss)
#   * Bg compile Ctx Row<Bg, Alloc, IO> + inner with Row<Block> (subset miss)
crucible_neg_compile_test(neg_delegated_session_payload_row_fg
    "CtxFitsPermissionedProtocol|CtxFitsProtocol|EffectRowMismatch|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_delegated_session_payload_row_bg
    "CtxFitsPermissionedProtocol|CtxFitsProtocol|EffectRowMismatch|constraints not satisfied|no matching function")

# fixy-A2-011 — substrate-side rejection of Choice<From, To> with
# ZERO branches at the global-wellformedness gate (Honda 2008 MPST).
# Before the fix, the variadic `Bs...` specialization of
# `is_global_well_formed<Choice<From, To, Bs...>>` folded branch
# validity via `(... && ...)` over an empty pack — vacuously true.
# After the fix, an explicit zero-branch specialization returns
# `false_type` and a recursive `has_empty_choice_v<G>` + routed
# `assert_no_empty_choice<G>()` helper surface the case for nested
# occurrences.  Two distinct rejection classes:
#   * top-level Choice<F, T> — caught by is_global_well_formed_v
#   * nested Choice<F, T> inside Rec_G/Transmission body — caught
#     by has_empty_choice_v + the routed [Choice_Empty_Branches]
#     diagnostic (analogous to [ProtocolViolation_Self_Loop] #371).
crucible_neg_compile_test(neg_session_global_empty_choice
    "should be well-formed|static assertion failed|constraints not satisfied")
crucible_neg_compile_test(neg_session_global_empty_choice_nested
    "Choice_Empty_Branches")

# fixy-A2-012 — `all_offers_have_crash_branch<P, PeerTag>` recursion
# specialisations for VendorPinned (SessionCrash.h) and
# CheckpointedSession (SessionCheckpoint.h).  Before the fix, the
# SessionCrash.h primary template was forward-declared only; any of
# the listed combinators in the protocol tree produced hard
# substitution failure (or false-by-default under SFINAE), making
# BSYZ22 crash-safety verification vacuous for every production
# CNTP vendor-pinned or transactional/checkpointed session.  Four
# rejection classes spanning two combinators × two depths:
#   * checkpointed_base_missing    — Offer-defect in BASE branch
#   * checkpointed_rollback_missing — Offer-defect in ROLLBACK
#                                      branch (proves AND-fold
#                                      walks both legs)
#   * vendorpinned_missing         — root-level vendor-pinned
#                                      Offer-defect (the canonical
#                                      CNTP production shape)
#   * vendorpinned_nested          — Offer-defect buried two levels
#                                      deep through Send/Recv
#                                      under VendorPinned (proves
#                                      transparent recursion at
#                                      depth)
# All four fire the framework-controlled [CrashBranch_Missing_In_Tree]
# diagnostic via assert_every_offer_has_crash_branch_for() (#368).
# Pairs with `neg_session_crash_branch_tree_missing.cpp` (deep-Offer
# in plain Offer<> tree) — together they pin the walker against root,
# nested, vendor-wrapped, and checkpointed shapes.
crucible_neg_compile_test(neg_session_crash_branch_checkpointed_base_missing
    "CrashBranch_Missing_In_Tree")
crucible_neg_compile_test(neg_session_crash_branch_checkpointed_rollback_missing
    "CrashBranch_Missing_In_Tree")
crucible_neg_compile_test(neg_session_crash_branch_vendorpinned_missing
    "CrashBranch_Missing_In_Tree")
crucible_neg_compile_test(neg_session_crash_branch_vendorpinned_nested
    "CrashBranch_Missing_In_Tree")

# fixy-A1-008 — strong-type discipline on FederationHandshake fields
# + federation_signature_fingerprint + make_self_signed_handshake.
# Three rejection classes:
#   * _org_id_swap — wrong-position cross-domain arg in
#     federation_signature_fingerprint (OrgId ↔ PeerKeyFingerprint).
#   * _passed_nonce_as_peer_key — wrong-position cross-domain arg in
#     make_self_signed_handshake (Nonce ↔ PeerKeyFingerprint).
#   * _swapped_org_id_and_nonce — raw-int literal + cross-domain field
#     assignment on FederationHandshake POD (closes the per-field
#     param-swap class).
crucible_neg_compile_test(neg_federation_org_id_swap
    "could not convert|cannot convert|no matching function|conversion from|no known conversion")
crucible_neg_compile_test(neg_federation_passed_nonce_as_peer_key
    "could not convert|cannot convert|no matching function|conversion from|no known conversion")
crucible_neg_compile_test(neg_federation_make_self_signed_with_swapped_org_id_and_nonce
    "could not convert|cannot convert|no matching function|conversion from|no known conversion")

# GAPS-075 — permission rows bind effectful ownership to ExecCtx row
# admission.  The pure permission APIs stay available for Row<> tags;
# row-bearing tags must flow through a ctx that admits their effect row.
crucible_neg_compile_test(neg_mint_perm_no_row_admission
    "CtxAdmitsPermission|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_perm_handoff_row_violation
    "CtxAdmitsPermission|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_perm_split_row_unifies_strict
    "CtxAdmitsPermission|constraints not satisfied|no matching function")
crucible_neg_compile_test(neg_perm_pool_lend_row_mismatch
    "CtxAdmitsPermission|constraints not satisfied|no matching function")

# ── METX-1 #473 — Computation::extract requires empty row ────────
# Effect-row substrate (Tang-Lindley POPL 2026, 25_04_2026.md §3).
# `extract()` on a Computation<R, T> requires `row_size_v<R> == 0` —
# unwrapping back to a pure value is only sound when no effect
# remains to discharge.  The neg-test calls extract on a Bg-row
# Computation and expects "no matching function" with the extract
# candidate's requires-clause spelled out.
crucible_neg_compile_test(neg_computation_extract_on_nonempty_row
    "extract")

# ── METX-1 #473 — Computation::weaken substitution principle ─────
# weaken<R2>() requires `Subrow<R, R2>` — capability propagation
# flows from narrower-effect rows to wider-effect rows, NEVER the
# reverse.  Trying to weaken from {Bg, IO} down to {Bg} would
# silently DROP the I/O capability obligation.  The constraint
# correctly rejects.  Substitution-principle test for the Met(X)
# row algebra.
crucible_neg_compile_test(neg_computation_weaken_non_subrow
    "weaken")

# ── METX-1 #473 — Computation::then bind-vs-fmap discrimination ────
# then(k) requires k's invoke_result to satisfy IsComputation — the
# trait specialization is true ONLY for Computation<R, T>.  Trying
# to bind a callback returning a plain T (which would belong in
# .map(f), not .then(k)) fails the requires-clause cleanly.  Without
# this gate, the M(X) monadic substrate would silently degenerate
# into a functor in disguise — bind and fmap would be
# interchangeable at the call site, but the resulting type's
# row_union_t<R, ?> would have an undefined row arithmetic.
crucible_neg_compile_test(neg_computation_then_non_computation_callback
    "IsComputation")

# ── FOUND-A23 — auto-permission-tree neg-compile witnesses ──────────
#
# Prove that the FOUND-A21 (1D) and FOUND-A22 (2D) generators reject
# every misuse the static_asserts were designed to catch.  All five
# patterns are FRAMEWORK-CONTROLLED — diagnostics come from
# auto_split_n / auto_split_grid / mint_grid_permissions / permission_split_n's
# own static_assert messages or §XXI requires-clause failures, not raw
# GCC phrasing.

# 1D (FOUND-A21): N=0 split is structurally meaningless.
crucible_neg_compile_test(neg_auto_split_n_zero
    "N must be greater than zero")
crucible_neg_compile_test(neg_auto_router_stream_void_payload
    "AutoRouter Stream payload does not satisfy|SpscValue")
crucible_neg_compile_test(neg_auto_router_zero_producers
    "AutoRouter Stream requires Producers > 0")
crucible_neg_compile_test(neg_auto_router_zero_max_shards
    "AutoRouter requires MaxShards > 0|AutoRouter Shardable requires MaxShards > 0")
crucible_neg_compile_test(neg_auto_split_bad_exec_ctx_type
    "AutoSplit body exec_ctx_type must satisfy|IsExecCtx")
crucible_neg_compile_test(neg_auto_split_move_only_body
    "AutoSplitShardBody|copy_constructible|constraints not satisfied")
crucible_neg_compile_test(neg_adaptive_scheduler_pool_copy_attempt
    "Pool owns worker jthreads and task queue")
crucible_neg_compile_test(neg_adaptive_scheduler_non_policy
    "SchedulerPolicy|constraints not satisfied")
crucible_neg_compile_test(neg_adaptive_scheduler_workload_non_invocable
    "is_invocable_r_v|constraints not satisfied")
crucible_neg_compile_test(neg_adaptive_scheduler_job_too_large
    "InlineTask|constraints not satisfied|sizeof")

# 2D (FOUND-A22): zero producers / zero consumers in the descriptor.
crucible_neg_compile_test(neg_auto_split_grid_zero_m
    "M .producer count. must be > 0")
crucible_neg_compile_test(neg_auto_split_grid_zero_n
    "N .consumer count. must be > 0")

# 2D (FOUND-A22 / fixy-A1-020): §XXI Universal Mint Pattern shape for
# mint_grid_permissions.  Two HS14 mismatch classes:
#   (1) requires-clause can_split_grid_v fold (M=0 or N=0)
#   (2) parameter-shape mismatch (non-Permission rvalue)
crucible_neg_compile_test(neg_mint_grid_permissions_call_zero
    "can_split_grid_v|constraints not satisfied")
crucible_neg_compile_test(neg_mint_grid_permissions_non_permission_arg
    "invalid initialization|cannot bind|no match|could not convert|mint_grid_permissions")

# 1D (FOUND-A21): cross-parent slice mixing — splits_into_pack fires
# only for matching Parent.  Non-matching parents trip
# permission_split_n's framework static_assert.
crucible_neg_compile_test(neg_slice_cross_parent_split
    "permission_split_n")

# ── FOUND-A05 — PermissionedMpscChannel role-discrimination tests ──
#
# Three witnesses prove the type system rejects every misuse the
# Permission/handle discipline was designed to catch:

# (a) Consumer cannot push — try_push is structurally absent from
#     ConsumerHandle's API.  GCC-WRAPPER-TEXT (no static_assert; the
#     diagnostic is the overload-set rejection itself).  GCC's text
#     is "has no member named 'try_push'" with fancy quotes.
crucible_neg_compile_test(neg_mpsc_consumer_try_push
    "has no member named.*try_push")

# (b) Producer cannot pop — try_pop is structurally absent from
#     ProducerHandle's API.  GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_mpsc_producer_try_pop
    "has no member named.*try_pop")

# (c) ConsumerHandle is non-copyable — copy would duplicate the
#     linear Consumer Permission, silently allowing two consumers
#     where MpscRing only supports one.  FRAMEWORK-CONTROLLED via
#     the = delete reason string.
crucible_neg_compile_test(neg_mpsc_consumer_copy
    "ConsumerHandle owns the Consumer Permission")

# ── FOUND-A09 — PermissionedMpmcChannel role-discrimination tests ──
#
# Mirror of the MPSC neg-compile suite, plus a copy-attempt witness
# for the consumer side (since MPMC's consumer is fractional via
# Pool, the copy-rejection is the SharedPermissionGuard's rejection
# rather than Permission's linear rejection).

# (a) Consumer cannot push.  GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_mpmc_consumer_try_push
    "has no member named.*try_push")

# (b) Producer cannot pop.  GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_mpmc_producer_try_pop
    "has no member named.*try_pop")

# (c) ConsumerHandle is non-copyable — copy would duplicate the
#     pool refcount share, miscounting outstanding consumers.
#     FRAMEWORK-CONTROLLED via the = delete reason string.
crucible_neg_compile_test(neg_mpmc_consumer_copy
    "ConsumerHandle owns a consumer-pool refcount share")

# ── FOUND-A15 — PermissionedShardedGrid role + slot-index tests ────
#
# Three witnesses prove the type system rejects every misuse the
# static-indexing discipline was designed to catch.

# (a) ConsumerHandle<J> cannot push.  GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_grid_consumer_try_push
    "has no member named.*try_push")

# (b) ProducerHandle<I> cannot recv.  GCC-WRAPPER-TEXT.
# Fixture source calls `p.try_pop()` — the recv-side queue API name —
# so GCC reports `has no member named 'try_pop'`.  Regex matches that.
# The fixture name keeps the conceptual "try_recv" label for grep
# discoverability with the producer/consumer symmetry test pair above.
crucible_neg_compile_test(neg_grid_producer_try_recv
    "has no member named.*try_pop")

# (c) producer<I>() with I >= M is a hard compile error at the
#     factory boundary — even if the caller constructs the type,
#     the static_assert in producer<I>() fires.  FRAMEWORK-CONTROLLED.
crucible_neg_compile_test(neg_grid_slot_index_out_of_range
    "I must be less than M")

# ── FOUND-A19 — PermissionedChaseLevDeque role-discrimination tests ──
#
# Three witnesses prove the type system rejects every misuse the
# linear Owner + fractional Thieves discipline was designed to catch.

# (a) Thief cannot push.  Only the Owner pushes to bottom.
#     GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_deque_thief_try_push
    "has no member named.*try_push")

# (b) Owner cannot steal.  Owner has the fast pop_bottom path;
#     try_steal is structurally absent.  GCC-WRAPPER-TEXT.
crucible_neg_compile_test(neg_deque_owner_try_steal
    "has no member named.*try_steal")

# (c) OwnerHandle is non-copyable — copy would duplicate the linear
#     Owner Permission, allowing two threads to race on bottom_.
#     FRAMEWORK-CONTROLLED via the = delete reason string.
crucible_neg_compile_test(neg_deque_owner_copy
    "OwnerHandle owns the linear Owner Permission")

# ── GAPS-061 — PermissionedMetaLog role-discrimination tests ───────
#
# MetaLog is append-only on the foreground side and drain-only on the
# background side.  The permissioned facade makes that distinction
# structural rather than convention-based.
crucible_neg_compile_test(neg_metalog_producer_drain
    "has no member named.*try_drain_one")

crucible_neg_compile_test(neg_metalog_consumer_append
    "has no member named.*try_append")

# ── GAPS-062 — PermissionedChainEdge role-discrimination tests ─────
crucible_neg_compile_test(neg_chainedge_signaler_wait
    "has no member named.*try_wait")

crucible_neg_compile_test(neg_chainedge_waiter_signal
    "has no member named.*signal")

crucible_neg_compile_test(neg_chainedge_raw_signal
    "raw ChainEdge::signal is substrate-only")

crucible_neg_compile_test(neg_chainedge_raw_wait
    "raw ChainEdge::wait is substrate-only")

crucible_neg_compile_test(neg_chainedge_raw_reset
    "raw ChainEdge::reset_under_quiescence is substrate-only")

# ─── FOUND-E diagnostic infrastructure — neg-compile harness ──────
#
# Four fixtures probing diagnostic-misuse paths that the framework
# catches via routed `static_assert` in safety/Diagnostic.h and
# safety/diag/RowMismatch.h.  Every regex is FRAMEWORK-CONTROLLED —
# matches a literal substring of a `static_assert` message we own.
crucible_neg_compile_test(neg_diag_non_tag_in_diagnostic_wrapper
    "constraints not satisfied")
crucible_neg_compile_test(neg_diag_non_tag_in_row_mismatch
    "RowMismatchTag_NonTag")
crucible_neg_compile_test(neg_diag_non_tag_accessor
    "DiagnosticAccessor_NonTag")
crucible_neg_compile_test(neg_diag_user_tag_not_in_catalog
    "Tag is not registered in the foundation Catalog")

# ─── FIXY-U-109: mint_diagnostic requires-gate witnesses ──────────
#
# mint_diagnostic<Tag>(...) gates on `requires is_diagnostic_class_v<Tag>`
# (must derive from tag_base, and not BE tag_base).  Two HS14 fixtures
# exercise the gate from distinct structural angles: (1) a fundamental
# (non-class) Tag, (2) a well-formed class lacking the tag_base lineage.
# These call the MINT factory; neg_diag_non_tag_in_diagnostic_wrapper
# exercises the Diagnostic<> WRAPPER template instead.
crucible_neg_compile_test(neg_diag_mint_diagnostic_non_class_tag
    "constraints not satisfied|is_diagnostic_class_v|no matching function")
crucible_neg_compile_test(neg_diag_mint_diagnostic_non_tag_base_class
    "constraints not satisfied|is_diagnostic_class_v|no matching function")

# ─── GAPS-094 audit follow-up — Vessel ABI Tagged direction fence ──
#
# from_typed() takes TypedHandle = Tagged<Vigil*, source::ABIBoundary>
# only.  This fixture witnesses that calling it with any other source
# tag (here: source::External) is rejected by the type system, so a
# refactor "promoting" the helper to accept any Tagged<Vigil*, *> would
# turn red here.  Closes the audit's GAPS-094 HS14 deficit.
crucible_neg_compile_test(neg_vessel_typed_handle_wrong_tag
    "no match for|cannot convert|no matching function|could not convert")

# ─── GAPS-095 — Vessel ABI typed-meta direction fence ─────────────
#
# The matching neg-compile witness for as_meta_typed / metas_from_typed
# (the second helper added by GAPS-095 to migrate the TensorMeta cast
# alongside the Vigil handle).  Pins that wrong-tag values cannot
# launder as ABIBoundary-tagged metas — same architectural rationale
# as the handle fence above, applied to the meta-array entry point.
crucible_neg_compile_test(neg_vessel_typed_meta_wrong_tag
    "no match for|cannot convert|no matching function|could not convert")

# ─── GAPS-096 — Vessel typed-accessor provenance fences ────────────
#
# data_ptr_typed returns Tagged<void*, source::External>; refusing
# implicit conversion into Tagged<void*, source::Sanitized> stops a
# refactor from laundering raw-FFI bytes into the trust-validated
# slot the dispatch path expects.  schema_name_typed returns
# Tagged<Borrowed<const char, SchemaTable>, source::Sanitized>;
# refusing the converse (Sanitized → External) keeps validated names
# and unsanitized FFI input distinguishable at the type level.  Two
# fixtures, two directions of the wrong-tag axis, so neither retag
# direction can silently launder.
crucible_neg_compile_test(neg_data_ptr_typed_wrong_tag
    "conversion from|cannot convert|could not convert|no match for|no matching function")
crucible_neg_compile_test(neg_schema_name_typed_wrong_tag
    "conversion from|cannot convert|could not convert|no match for|no matching function")

# ─── FOUND-C Phase 7 — PermissionedSessionHandle neg-compile harness
#
# Ten fixtures probing permission-flow violations that the type system
# catches via [PermissionImbalance] static_asserts in PSH's close,
# send, step_to_next_permissioned (loop balance), and the
# perm_set_union_t disjoint-operands assert.  Every regex below is
# FRAMEWORK-CONTROLLED — matches a literal substring of a static_assert
# message we own.
#
# ─── Soundness — what each regex pins ─────────────────────────────
#
# Each pattern triple-anchors the diagnostic so a fixture cannot pass
# for the wrong reason:
#
#   1. SOURCE FILE PATH   — PermissionedSession\.h or PermSet\.h.
#      The assert MUST originate in our framework header, not in a
#      stray static_assert in the user's .cpp.  GCC always names the
#      file:line:col of the failing assert; we anchor on that path.
#
#   2. "static assertion failed"  — the literal GCC prefix for a true
#      assert failure.  Distinguishes our static_assert from any other
#      diagnostic mentioning [PermissionImbalance] (e.g. an in-comment
#      reference, a doc-string, a #pragma message).
#
#   3. The specific message keyword for the intended check
#      ("reached End", "payload type T requires", "Loop body's
#      terminal PermSet", "disjoint operands").  Distinguishes which
#      of the four [PermissionImbalance] sites fired.
#
# Per-fixture audit (commit ee2b755 follow-up):
#
#   - Each fixture verified to fire EXACTLY ONE static_assert
#     (PermissionImbalance count = 1, static_assert count = 1).
#     No fixture has dual failure modes that could mask which
#     assertion actually fired.
#
#   - Each fixture's assert is anchored to the EXPECTED header and
#     framework-owned message.  No fixture should pass from a stale
#     call-site overload error or unrelated compiler diagnostic.
#
#   - "Good twin" of fixture #1 (perm dropped before establish)
#     verified to compile cleanly — proves the bad version fails for
#     the leftover-perm reason, not some scaffolding bug.

# (1) close at End with leftover Permission.  Establish with a Permission;
#     close() before any surrender step.
crucible_neg_compile_session_test(neg_psh_close_with_leftover_perm
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (2) Send Transferable<T, X> from a handle whose PS does not contain X.
crucible_neg_compile_session_test(neg_psh_send_transferable_without_holding
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*payload type T requires")

# (3) Send Returned<T, X> from a handle whose PS does not contain X.
crucible_neg_compile_session_test(neg_psh_send_returned_without_holding
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*payload type T requires")

# (4) Loop body that DRAINS a permission per iteration without surrender.
#     Continue's balance assert fires.
crucible_neg_compile_session_test(neg_psh_loop_drains_perm_per_iteration
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (5) Loop body that GAINS a permission per iteration without surrender
#     (the symmetric inverse of #4).
crucible_neg_compile_session_test(neg_psh_loop_gains_perm_per_iteration
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (6) Recv Transferable then close at End without surrender.
crucible_neg_compile_session_test(neg_psh_recv_imbalance_at_end
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (7) Select with branch that closes without surrendering — Decision D4
#     STRUCTURAL convergence: close on the imbalanced branch fires.
crucible_neg_compile_session_test(neg_psh_select_diverging_branch
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (8) Nested Loop where the OUTER body balances but the INNER body does
#     not — exercises Decision D3's loop-context shadowing.
crucible_neg_compile_session_test(neg_psh_nested_loop_inner_imbalance
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (9) Multi-step body where MOST operations balance but the LAST step
#     leaves PS imbalanced.  Subtle: balanced sub-chains hide a trailing
#     half-pair that the type system catches via Continue's balance assert.
crucible_neg_compile_session_test(neg_psh_subtle_chain_imbalance
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (10) perm_set_union_t with overlapping operands — disjoint-union assert
#      fires.  CSL frame rule violation: a permission cannot be held by
#      two participants simultaneously.
crucible_neg_compile_session_test(neg_perm_set_union_overlap
    "PermSet\\.h.*static assertion failed.*PermissionImbalance.*disjoint operands")

# GAPS-058: PermissionedSessionHandle × Delegate/Accept bridge.  These
# pin the DelegatedSession<P, InnerPS> handoff boundary so higher-order
# session transfer cannot fabricate, duplicate, or leak InnerPS.
crucible_neg_compile_session_test(neg_psh_delegate_inner_ps_not_in_sender
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*PermSet does not contain inner_ps tokens")

crucible_neg_compile_session_test(neg_psh_accept_inner_ps_already_owned
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*inner_ps already owned")

crucible_neg_compile_session_test(neg_psh_delegate_inner_proto_unbalanced
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*is_permission_balanced_v<InnerProto> against InnerPS rejects")

crucible_neg_compile_session_test(neg_psh_delegate_to_self
    "ProtocolViolation_Self_Loop|self-loop")

# GAPS-059: ctx-bound permissioned session mint.  These fixtures pin the
# unified mint boundary: Ctx is mandatory, row admission sees through
# permission payload wrappers, initial PermSet must locally close, and a
# Permission<Tag> cannot be consumed twice.
crucible_neg_compile_session_test(neg_mint_session_no_ctx
    "mint_session<Proto>\\(resource\\) is removed")

crucible_neg_compile_session_test(neg_mint_permissioned_session_no_ctx
    "no matching function for call to.*mint_permissioned_session|candidate expects at least 2 arguments")

crucible_neg_compile_session_test(neg_psh_direct_construct
    "no matching function for call to.*PermissionedSessionHandle")

crucible_neg_compile_session_test(neg_psh_fabricate_permset
    "no matching function for call to.*PermissionedSessionHandle")

crucible_neg_compile_session_test(neg_mint_session_ctx_no_admit_proto
    "associated constraints are not satisfied|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_transferable_io_payload_to_pure_ctx
    "associated constraints are not satisfied|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_borrowed_io_payload_to_pure_ctx
    "associated constraints are not satisfied|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_returned_io_payload_to_pure_ctx
    "associated constraints are not satisfied|CtxFitsPermissionedProtocol|CtxFitsProtocol")

# GAPS-076: paired channel minting validates both local protocols.  Endpoint A
# runs Proto; endpoint B runs dual(Proto).  These fixtures pin sender-side row
# admission, receiver-side row admission, continuation drift, and Capability
# payload rows.
crucible_neg_compile_session_test(neg_mint_channel_receiver_no_admit
    "associated constraints are not satisfied|CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_sender_no_admit
    "associated constraints are not satisfied|CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_recv_row_drift
    "associated constraints are not satisfied|CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_capability_unauthorized_receiver
    "associated constraints are not satisfied|CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_requires_ctx
    "mint_channel<Proto>\\(resource_a, resource_b\\) is removed")

crucible_neg_compile_session_test(neg_persisted_session_hot_ctx
    "constraints not satisfied|template constraint failure|associated constraints|CtxAdmits")

crucible_neg_compile_session_test(neg_persisted_session_no_ctx
    "PersistedSession_CtxRequired|use of deleted function")

# fixy-A2-025: PSH detach is no longer deleted — it forwards to inner.detach,
# records SessionEvent::detach, and flushes the audit log.  The surviving
# discipline (rvalue-only, typed Reason required) is pinned by the two
# fixy-A2-025 fixtures below.

crucible_neg_compile_session_test(neg_psh_detach_bare_no_reason
    "DetachReason_Required|deleted|matching function")

crucible_neg_compile_session_test(neg_psh_detach_untyped_reason
    "DetachReason|requires|constraint")

crucible_neg_compile_session_test(neg_persisted_session_no_open_view
    "PersistedSession_OpenViewRequired|use of deleted function")

# fixy-A2-006 — PSH overloads of mint_recording_session / mint_persisted_session.
# (Phase 1 deferral witnesses neg_recording_session_psh_delegate_unsupported +
# neg_recording_session_psh_accept_unsupported retired in FIXY-U-123 — Phase 2
# specializations shipped via fixy-A2-006b/c/d/e (tasks #1716/#1717/#1718/#1719);
# the primary template's incomplete-type hole is sealed and positive coverage
# at the four Delegate/Accept/EpochedDelegate/EpochedAccept specializations
# lives in the RecordingPermissionedSessionHandle.h sentinel block.)

crucible_neg_compile_session_test(neg_persisted_session_psh_hot_ctx
    "constraints not satisfied|template constraint failure|associated constraints|CtxAdmits")

crucible_neg_compile_session_test(neg_persisted_session_psh_no_open_view
    "PersistedSession_OpenViewRequired|use of deleted function")

# fixy-M-17 — HS14 floor for mint_session_view<Tag>(handle).
# Two fixtures × distinct mismatch classes: #1 wrong protocol position
# (handle at Send, request AtRecv); #2 unrecognized position tag
# (BogusPositionTag).  Both fire mint_session_view's
# `requires HandleIsAt<Handle, Tag>` clause.
crucible_neg_compile_session_test(neg_mint_session_view_wrong_position
    "constraints not satisfied|HandleIsAt|handle_is_at|no matching function|mint_session_view")
crucible_neg_compile_session_test(neg_mint_session_view_invalid_tag
    "constraints not satisfied|HandleIsAt|handle_is_at|no matching function|mint_session_view")

crucible_neg_compile_session_test(neg_mint_session_perms_unbalanced
    "associated constraints are not satisfied|CtxFitsPermissionedProtocol|permission_flow_closes")

crucible_neg_compile_session_test(neg_mint_session_perm_double_consume
    "PermSet\\.h.*static assertion failed.*PermissionImbalance.*unique Tags")

# GAPS-060: Chase-Lev owner/thief typed-session facade.  The thief
# protocol is Recv-only; the owner Recv branch is pop_bottom, not the
# thief borrowed-steal transport.
crucible_neg_compile_session_test(neg_chaselev_thief_session_push
    "has no member named.*send")

crucible_neg_compile_session_test(neg_chaselev_owner_session_steal
    "try_steal|no matching function|constraints not satisfied")

# GAPS-081: generic SubstrateSessionBridge and Endpoint preserve the
# Chase-Lev owner/thief protocol split.  The thief direction remains
# Recv-only through both factories.
crucible_neg_compile_session_test(neg_chaselev_substrate_thief_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_chaselev_endpoint_thief_try_send
    "no matching function for call|constraints not satisfied|candidate")

# GAPS-082: indexed ShardedGrid substrate sessions.  ShardId<I>
# selects the statically-indexed producer/consumer handle, and role
# misuse remains rejected through the generic bridge.
crucible_neg_compile_session_test(neg_sharded_grid_substrate_consumer_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_sharded_grid_substrate_producer_recv
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_sharded_grid_bridge_producer_shard_oob
    "ShardedGridBridge_ProducerShardOutOfRange|producer shard I")

# GAPS-083: indexed CalendarGrid substrate sessions.  CalendarProducerId<P>
# selects a producer row; CalendarConsumerId selects the single drain
# consumer.  Role misuse remains rejected through the generic bridge.
crucible_neg_compile_session_test(neg_calendar_grid_substrate_consumer_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_calendar_grid_substrate_producer_recv
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_calendar_grid_bridge_producer_row_oob
    "CalendarGridBridge_ProducerRowOutOfRange|producer row P")

# GAPS-084: indexed ShardedCalendarGrid substrate sessions.  ShardId<S>
# selects the shard-local producer/consumer calendar handle; role misuse
# and out-of-range shard IDs remain compile-time failures.
crucible_neg_compile_session_test(neg_sharded_calendar_grid_substrate_consumer_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_sharded_calendar_grid_substrate_producer_recv
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_sharded_calendar_grid_bridge_shard_oob
    "ShardedCalendarGridBridge_ShardOutOfRange|shard S")

# GAPS-062: ChainEdge one-shot session facade.  SignalerProto is
# Send-only; WaiterProto is Recv-only.
crucible_neg_compile_session_test(neg_chainedge_waiter_session_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_chainedge_signaler_session_recv
    "has no member named.*recv")

# SEPLOG-H2: MpmcChannelSession typed-session facade for the fractional
# × fractional cell of the channel-permission family.  Four fixtures
# pin the four canonical mismatch classes:
#
#   #1 ConsumerSession.send()   — Recv-protocol cannot send.
#   #2 ProducerSession.recv()   — Send-protocol cannot recv.
#   #3 surface concept rejects partial-shape "almost-channel".
#   #4 handle copy rejected at channel layer (pool refcount discipline).
#
# Together these satisfy HS14 (≥ 2 fixtures per new mint factory) for
# both mint_mpmc_producer_session and mint_mpmc_consumer_session.
crucible_neg_compile_session_test(neg_mpmc_session_consumer_session_send
    "has no member named.*send")

crucible_neg_compile_session_test(neg_mpmc_session_producer_session_recv
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_mpmc_session_surface_rejects_non_channel
    "MpmcChannelSessionSurface|constraints not satisfied|no type named|the required expression")

crucible_neg_compile_session_test(neg_mpmc_session_handle_copy
    "ProducerHandle owns a producer-pool refcount share")

# ─── HS14 fixtures #1 + #2 for safety::PublishCommitCell ────────────
#
# GAPS-FLUSH-RACE harness — see safety/PublishCommit.h.  The cell's
# write surface (bump_by, bump) is friend-gated so only the listed
# WriteAuth type can advance the counter.  Two fixtures, each a
# distinct mismatch class:
#
#   * #1 UNAUTHORIZED-BUMP — code outside WriteAuth's body cannot
#     call bump_by.  An attempt is rejected with the standard
#     "private member" diagnostic.  Without this gate, the
#     pre-fix BackgroundThread regression — bumping
#     total_processed from BuildTraceFn (stage 3) instead of
#     MakeRegionFn (stage 4) — was structurally invisible.
#
#   * #2 CELL-COPY — PublishCommitCell deletes both copy and move
#     with explicit reasons.  Copying would duplicate the channel
#     identity (two atomic counters racing under one foreground
#     reader).  Moving would invalidate the reference held by the
#     foreground.  Both are rejected at compile time.
crucible_neg_compile_test(neg_publish_commit_unauthorized_bump
    "is private within this context|private within this context|private member")

crucible_neg_compile_test(neg_publish_commit_cell_move
    "use of deleted function|deleted function|PublishCommitCell")

# ─── safety/Pre.h + safety/Post.h consteval-firing witnesses ────────
#
# CONTRACT-001 / CONTRACT-002: HS14 fixture pair pinning the consteval-
# enforcement guarantee of CRUCIBLE_PRE and CRUCIBLE_POST.  The two
# fixtures cover distinct mismatch classes:
#
#   * neg_pre_macro_consteval_struct_ref — CRUCIBLE_PRE on `T const&`
#     input (Shape #2 in the empirical probe table; was bypassed by
#     P2900 `pre()`).
#   * neg_post_macro_consteval_scalar_return — CRUCIBLE_POST on scalar
#     return (Shape #6 in the probe; was bypassed by P2900 `post()`).
#
# Both rely on the if-consteval-trap pattern in safety/Pre.h: the
# `__builtin_trap()` planted under `if consteval` is not a constant
# expression, so the consteval evaluator fails with "non-constant
# condition" — exactly the diagnostic shape we expect.  The PASS
# regex below matches that diagnostic family.
#
# See misc/08_05_2026_harness.md §4 + §7.2 for the full rationale.
crucible_neg_compile_test(neg_pre_macro_consteval_struct_ref
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_post_macro_consteval_scalar_return
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-007: HS14 fixture-corpus expansion.  Five additional
# fixtures pinning the remaining Probe shapes that bypass on the
# un-patched distro GCC 16.1.1 May-1 build.  Together with the two
# above, this gives full 7-shape coverage of the empirical probe in
# misc/08_05_2026_harness.md §4.5 — every shape that EVER bypassed
# native pre()/post() on any supported build is pinned by an HS14
# fixture proving CRUCIBLE_PRE/POST catches it.
#
# Distinct mismatch classes:
#   * neg_pre_macro_consteval_scalar_byval — Probe Shape #1 (scalar
#     by-value).  Simplest possible parameter shape.
#   * neg_pre_macro_consteval_struct_cval — Probe Shape #3 (struct
#     const-by-value).  Const-qualified copy.
#   * neg_pre_macro_consteval_struct_byval — Probe Shape #4 (struct
#     by-value).  Mutable copy.
#   * neg_pre_macro_consteval_struct_cptr — Probe Shape #5 (struct
#     const-pointer).  Compound predicate (non-null AND data check);
#     pins firing on the SECOND conjunct, not just nullptr.
#   * neg_post_macro_consteval_struct_field — Probe Shape #7 (struct
#     return with field-projecting post).  Dominant shape for PoD-
#     aggregate invariants.
crucible_neg_compile_test(neg_pre_macro_consteval_scalar_byval
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_consteval_struct_cval
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_consteval_struct_byval
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_consteval_struct_cptr
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_post_macro_consteval_struct_field
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# ─── fixy-A1-007 — handles/Once.h SetOnce CRUCIBLE_PRE witnesses ────
#
# Three HS14 fixtures pinning the body-CRUCIBLE_PRE migration of
# SetOnce<T>::set and SetOnce<T>::reset (handles/Once.h).  The vanilla
# P2900 `pre(ptr_ == nullptr)` / `pre(has_value())` form references
# class members through implicit `this->` — the canonical consteval-
# bypass shape on GCC 16.1.1 for foldable-body functions.
#
# Distinct mismatch classes (HS14 mandate ≥2 per gate; we ship 3):
#   * neg_pre_macro_setonce_set_null_param — parameter-side null
#     check.  Easy case (vanilla pre handles it correctly), included
#     for clause-shape uniformity and as the baseline witness.
#   * neg_pre_macro_setonce_set_double — this->-member double-set
#     check.  THE load-bearing fixture: vanilla pre silently bypasses
#     this exact shape; CRUCIBLE_PRE must fire.
#   * neg_pre_macro_setonce_reset_unset — this->-member-FUNCTION
#     lifecycle-reset check.  The harder consteval-bypass class
#     because the predicate goes through member-function call
#     resolution before reaching the foldable body.
#
# See memory feedback_gcc16_c26_contract_gotchas + feedback_crucible_
# pre_post_macros for the full diagnosis of the bypass class.
crucible_neg_compile_test(neg_pre_macro_setonce_set_null_param
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_setonce_set_double
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_setonce_reset_unset
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# ─── fixy-A1-007 Phase 2 — safety/Mutation.h CRUCIBLE_PRE witnesses ──
#
# Six HS14 fixtures pinning the body-CRUCIBLE_PRE migration of 12
# this->-member pre clauses in safety/Mutation.h across five wrapper
# families (WriteOnce, WriteOnceNonNull, Monotonic, BoundedMonotonic,
# OrderedAppendOnly, AtomicMonotonic).  WriteOnceNonNull's predicate
# shape is already witnessed by the SetOnce trio above (same nullptr-
# sentinel + ptr_ pattern); Phase 2 adds witnesses for the NEW
# distinct mismatch classes:
#
#   * neg_pre_macro_writeonce_set_twice — optional<T>::has_value()
#     member-fn-on-member predicate (double-set setter path).
#   * neg_pre_macro_writeonce_get_unset — same predicate but on const
#     accessor path (read-before-set).
#   * neg_pre_macro_monotonic_advance_backward — lattice_type::leq
#     NESTED-member-function predicate.
#   * neg_pre_macro_monotonic_bump_at_max — member-fn vs static
#     numeric_limits sentinel.
#   * neg_pre_macro_bounded_monotonic_bump_at_max — two-level
#     nested-wrapper member-fn-on-member chain.
#   * neg_pre_macro_ordered_appendonly_backward_key — DISJUNCTION
#     with member-fn-on-member subexpressions plus stateless functor
#     calls (KeyFn{}/Cmp{}) — most complex predicate shape in the file.
crucible_neg_compile_test(neg_pre_macro_writeonce_set_twice
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_writeonce_get_unset
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_monotonic_advance_backward
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_monotonic_bump_at_max
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_bounded_monotonic_bump_at_max
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_pre_macro_ordered_appendonly_backward_key
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# ─── crucible::decide::* predicate library — HS14 fixtures ──────────
#
# CONTRACT-030: HS14 fixture pair pinning that decide::no_overflow_mul
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# overflowing operands.  The two fixtures cover distinct mismatch
# classes:
#
#   * neg_decide_no_overflow_mul_unsigned — UNSIGNED wrap-around
#     (UINT64_MAX × 2).  Tests __builtin_mul_overflow's unsigned
#     overflow detection branch.
#   * neg_decide_no_overflow_mul_signed   — SIGNED asymmetric range
#     (INT64_MIN × -1).  Tests __builtin_mul_overflow's signed
#     branch, which catches a bug class the unsigned path misses.
#
# Together these prove the predicate composes correctly with
# CRUCIBLE_PRE's if-consteval-trap pattern and the GCC 16.1.1
# __builtin_mul_overflow constexpr evaluator.  Discipline scales
# linearly: every new decide::* procedure ships with ≥2 fixtures
# pinning its consteval-firing surface.
crucible_neg_compile_test(neg_decide_no_overflow_mul_unsigned
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_no_overflow_mul_signed
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-031: HS14 fixture pair pinning that decide::no_overflow_sum
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# overflowing operands.  Mismatch classes:
#
#   * neg_decide_no_overflow_sum_unsigned — UNSIGNED wrap-around
#     (UINT64_MAX + 1).  Tests __builtin_add_overflow's unsigned path.
#   * neg_decide_no_overflow_sum_signed   — SIGNED asymmetric range
#     at the negative extreme (INT64_MIN + (-1)).  Distinct from the
#     mul fixture's INT64_MIN × -1 path: addition's overflow detection
#     branches on sign-bit + carry-out, not on width-doubled product
#     range, so this is a genuinely different bug class.
crucible_neg_compile_test(neg_decide_no_overflow_sum_unsigned
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_no_overflow_sum_signed
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-032: HS14 fixture pair pinning that
# decide::no_overflow_pow2_shift correctly fires inside CRUCIBLE_PRE
# at consteval when called with operands that would invoke any of the
# four [expr.shift] UB / overflow classes.  Mismatch classes:
#
#   * neg_decide_no_overflow_pow2_shift_count_oor — SHIFT COUNT OUT
#     OF RANGE (b == bitwidth(T)).  Fires the predicate's first
#     short-circuit branch (`b >= W`).  Distinct from sum/mul because
#     this UB class exists ONLY at the shift operator.
#   * neg_decide_no_overflow_pow2_shift_signed_negative — LEFT-SHIFT
#     OF SIGNED NEGATIVE VALUE (a == -1, b == 1).  Fires the third
#     branch (`a < 0` for signed T) AFTER both range checks pass.
#     This bug class survives any shift-count check and cannot be
#     caught by sum/mul/range predicates — unique to the pow2_shift
#     family.
crucible_neg_compile_test(neg_decide_no_overflow_pow2_shift_count_oor
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_no_overflow_pow2_shift_signed_negative
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-040: HS14 fixture pair pinning that decide::all_in_range
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# a span containing any element outside [lo, hi].  Mismatch classes
# correspond to the predicate's two short-circuit branches:
#
#   * neg_decide_all_in_range_element_below_lo — element < lo at
#     index 1 (middle).  Exercises signed-comparison: a negative
#     element vs non-negative lo bound.  Catches the bug class
#     where a -1 sentinel slips through a uint→int reinterpretation.
#   * neg_decide_all_in_range_element_above_hi — element > hi at
#     index 1 (middle).  Exercises unsigned upper-bound: a
#     deserialized counter past MAX_KERNELS = 146 drifts.  Distinct
#     branch from below-lo because most production manual checks
#     (`if (idx < n)`) catch upper-bound cleanly but the lower-bound
#     side is where signed/unsigned drift hides.
#
# Both fixtures place the violating element at index 1 (NOT the
# endpoints) — pins the "endpoint-only manual check" anti-pattern
# the procedure exists to eliminate.
crucible_neg_compile_test(neg_decide_all_in_range_element_below_lo
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_all_in_range_element_above_hi
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-041: HS14 fixture pair pinning that decide::strictly_increasing
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# a sequence whose consecutive-pair monotonicity is violated.
# Mismatch classes:
#
#   * neg_decide_strictly_increasing_equal_pair — duplicate consecutive
#     pair {1, 5, 5, 9}.  THE strict-vs-weak distinguishing class:
#     std::is_sorted (uses <=) silently passes; only strictly_increasing
#     rejects.  Catches the "stalled counter" bug class in
#     event-sourced systems where a duplicate record persists without
#     advancing step_id.
#   * neg_decide_strictly_increasing_middle_regress — middle-pair
#     regression {1, 9, 5, 10}.  Different bug class: counter going
#     backward, distinct from stalling.  Production consequence:
#     event-sourced replay sees out-of-order record, breaks causal
#     ordering.
#
# Both fixtures place the violating pair at the middle (index 1->2)
# of a 4-element span — pins the "endpoint shortcut"
# pre(front() < back()) anti-pattern that misses any interior
# violation by construction.
crucible_neg_compile_test(neg_decide_strictly_increasing_equal_pair
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_strictly_increasing_middle_regress
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-042: HS14 fixture pair pinning that decide::weakly_increasing
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# a sequence containing a strict consecutive-pair regression (the
# ONLY bug class weakly_increasing rejects — equal pairs PASS, which
# is the load-bearing distinction vs strictly_increasing's `<` shape).
# Mismatch classes:
#
#   * neg_decide_weakly_increasing_middle_regress — INTERIOR regression
#     {0, 5, 3, 7}.  Endpoints (0, 7) satisfy `front <= back`; the
#     `pre(front() <= back())` endpoint-shortcut would silently
#     accept.  Catches the front-back endpoint anti-pattern.
#   * neg_decide_weakly_increasing_tail_regress — TAIL regression
#     {1, 2, 3, 5, 4}.  All but the final pair are weakly increasing;
#     a partial-scan / first-K-pairs / "every prefix is weakly
#     increasing therefore the whole sequence is" induction would
#     silently accept.  Catches the partial-scan anti-pattern that
#     a per-append predicate call invites.
#
# The two fixtures target ORTHOGONAL anti-patterns (endpoint shortcut
# vs. partial scan) — together they pin every common ad-hoc check
# shape that an engineer might reach for in lieu of citing the
# Decide procedure.
crucible_neg_compile_test(neg_decide_weakly_increasing_middle_regress
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_weakly_increasing_tail_regress
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-050: HS14 fixture pair pinning that decide::is_power_of_two_le
# correctly fires inside CRUCIBLE_PRE at consteval when called with
# a value that violates EITHER of the predicate's two conjuncts:
# "x is a power of two" AND "x <= bound".  Mismatch classes pin
# orthogonal anti-patterns at the call site.
# Mismatch classes:
#
#   * neg_decide_is_power_of_two_le_non_pow2 — value WITHIN bound
#     but with multiple set bits (48 = 32 + 16, bound = 64).
#     Catches the `pre(x % 2 == 0)` / `pre((x & 1) == 0)` modulo
#     anti-patterns that misread "power of two" as "even number".
#   * neg_decide_is_power_of_two_le_above_bound — value IS a power
#     of two (128 = 2^7, popcount == 1) but ABOVE bound (bound = 64).
#     Catches the `pre(__builtin_popcount(x) == 1)` /
#     `pre(std::has_single_bit(x))` / shift-only anti-patterns that
#     correctly identify the bit pattern but omit the capacity
#     ceiling.
#
# Together the two fixtures pin BOTH conjuncts: a reviewer who
# satisfies one must satisfy the other.  is_power_of_two_le is the
# single discharge that protects both bug classes (capacity overrun
# AND bit-mask invalidation downstream).
crucible_neg_compile_test(neg_decide_is_power_of_two_le_non_pow2
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_is_power_of_two_le_above_bound
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-051: HS14 fixture pair pinning that decide::factorization_eq
# correctly fires inside CRUCIBLE_PRE at consteval.  First multi-
# factor MULTIPLICATIVE predicate in the spine (vs. additive
# no_overflow_sum and elementwise all_in_range).  Production
# usage: 5D parallelism config where TP × DP × PP × EP × CP must
# equal world_size; partition optimizer in Meridian.  Two distinct
# mismatch classes:
#
#   * neg_decide_factorization_eq_wrong_product — value-mismatch
#     class.  Factors {8, 2, 4, 1, 2} multiply to 128 within
#     uint32_t range, but expected total is 64.  Catches the
#     transcription bug class where an engineer writes the wrong
#     dimension (TP=8 instead of TP=4).  No overflow involved.
#   * neg_decide_factorization_eq_overflow — overflow class.
#     Factors {65536, 65536} multiply to 2^32 which wraps to 0
#     in uint32_t arithmetic; total is also 0.  A naive
#     std::accumulate-with-multiplies<> implementation would
#     silently accept (wrapped product == total).  Catches the
#     untrusted-input / range-validation bug class.
#
# Together the two fixtures pin BOTH failure modes the predicate
# detects: mathematical-product mismatch AND running-product
# overflow.  Either produces a false return; only factorization_eq
# rejects both at the boundary.
crucible_neg_compile_test(neg_decide_factorization_eq_wrong_product
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_factorization_eq_overflow
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-052: HS14 fixture pair pinning that decide::coprime
# correctly fires inside CRUCIBLE_PRE at consteval.  Coprimality
# via Euclidean algorithm; production usage: hash-table double-
# probing secondary stride must be coprime to capacity; Philox
# stride constants; LCG period guarantees.  Two distinct mismatch
# classes:
#
#   * neg_decide_coprime_shared_factor — fires on `(6, 9)` with
#     gcd 3.  THE classical one-sided-modulo anti-pattern bait:
#     6 % 9 != 0 AND 9 % 6 != 0, but the pair shares factor 3.
#     A `pre(a % b != 0 && b % a != 0)` check accepts; only the
#     full Euclidean walk rejects.
#   * neg_decide_coprime_zero_pair — fires on `(0, 0)`.  Degenerate
#     input where gcd is 0 (not 1).  Without the predicate's
#     explicit zero short-circuit, hand-rolled Euclidean would UB
#     on `0 % 0`; std::gcd returns 0 silently.  Catches the
#     missing-zero-guard / uninitialized-config bug class.
#
# Together the two fixtures pin BOTH the most common mistake
# (one-sided modulo) AND the corner-case input (degenerate zero).
crucible_neg_compile_test(neg_decide_coprime_shared_factor
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_coprime_zero_pair
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-060 fixtures — pin Decide::intervals_pairwise_disjoint:
# `[lo, hi)` half-open intervals, well-formedness (lo <= hi) AND
# pairwise non-overlap; production usage: MemoryPlan slot offset
# assignment must produce non-overlapping byte intervals among
# slots that are simultaneously live.  Two distinct mismatch classes:
#
#   * neg_decide_intervals_pairwise_disjoint_overlap — fires on
#     `{[0, 100), [50, 150)}`.  Both well-formed individually;
#     pass-1 succeeds; pass-2 pairwise rejects.  Catches the
#     boundary-arithmetic bug class — sweep-line offset assignment
#     forgets to bump `next_offset` past previous slot's nbytes.
#   * neg_decide_intervals_pairwise_disjoint_inverted — fires on
#     a SINGLE inverted interval `[50, 30)`.  Pass-1 well-
#     formedness rejects before pass-2 runs.  Catches the missing-
#     validity-check bug class — forgot to verify `lo <= hi` (or
#     `offset + nbytes` underflowed silently).
#
# Together the two fixtures pin BOTH the canonical sweep-line bug
# (adjacent overlap) AND the malformed-input bug (inverted slot).
crucible_neg_compile_test(neg_decide_intervals_pairwise_disjoint_overlap
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_intervals_pairwise_disjoint_inverted
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-112 fixtures — pin crucible::live_intervals_disjoint_at<MaxLive>,
# the production cite that lifts decide::intervals_pairwise_disjoint to
# the MemoryPlan invariant: at every op boundary `t`, the simultaneously-
# live TensorSlots must have non-overlapping byte ranges.  Two distinct
# mismatch classes:
#
#   * neg_memory_plan_live_intervals_overlap — two internal slots whose
#     live ranges intersect (both alive at op 3) AND whose byte intervals
#     `[0, 128)` and `[64, 192)` overlap on `[64, 128)`.  Catches the
#     sweep-line offset-assignment bug where the planner forgets to bump
#     past the previous live slot's nbytes.  In production this is a
#     silent corruption bug: writes from one slot trample reads from
#     the other.
#   * neg_memory_plan_live_intervals_max_overflow — three internal slots
#     all simultaneously live at op 1, but the caller's MaxLive=2 cap is
#     too tight.  Predicate must return false (cannot safely build a
#     live-set span without overflowing the stack array).  Catches the
#     misuse class where production code hardcodes too-small MaxLive
#     and silently mis-validates plans on larger live sets.
crucible_neg_compile_test(neg_memory_plan_live_intervals_overlap
    "static assertion failed|non-constant condition|not a constant expression|__builtin_trap")
crucible_neg_compile_test(neg_memory_plan_live_intervals_max_overflow
    "static assertion failed|non-constant condition|not a constant expression|__builtin_trap")

# CONTRACT-061 fixtures — pin Decide::intervals_cover_unit:
# EXACT partition of `[0, total)` — pairwise disjoint AND each
# contained in `[0, total)` AND each non-empty AND widths sum to
# `total`.  Strictly stronger than intervals_pairwise_disjoint
# (which permits gaps).  Production cite: Cipher cold-tier blob
# layout (must hole-free tile the on-disk blob); 5D parallelism
# shard cover where TP×DP×PP×EP×CP shards must exactly partition
# the global tensor dim.  Two distinct mismatch classes:
#
#   * neg_decide_intervals_cover_unit_gap — fires on `{[0, 30),
#     [50, 100)}` with total=100.  Pairwise disjoint AND each in
#     bounds, but the sum of widths (80) != total (100): a 20-
#     byte gap at [30, 50).  Catches the missing-completeness
#     bug class.
#   * neg_decide_intervals_cover_unit_overshoot — fires on
#     `{[0, 50), [50, 110)}` with total=100.  Pairwise disjoint,
#     first interval in bounds, but the second has hi=110 >
#     total=100.  Pass-1 per-interval bounds check rejects.
#     Catches the missing-bounds-check bug class.
#
# Together the two fixtures pin BOTH the "incomplete partition"
# bug (gap) AND the "out-of-bounds shard" bug (overshoot).
crucible_neg_compile_test(neg_decide_intervals_cover_unit_gap
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_intervals_cover_unit_overshoot
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-070 fixtures — pin Decide::tier_replaces: chain-lattice-
# agnostic admission predicate over the project's chain-tier scoped
# enums (CipherTierTag, DetSafeTier, HotPathTier, NumericalTier, …).
# Convention: stronger guarantee = HIGHER ordinal (see
# algebra/lattices/CipherTierLattice.h §"Direction convention");
# `tier_replaces(c, r)` ≡ `to_underlying(c) >= to_underlying(r)`.
# Two distinct mismatch classes — the SAME structural rule (correct
# comparator direction on chain ordering) exercised on TWO DIFFERENT
# production bug families spanning DIFFERENT lattice cardinalities
# (3 vs 7), demonstrating the predicate's lattice-agnosticism:
#
#   * neg_decide_tier_replaces_storage_downgrade — fires on
#     `(CipherTierTag::Cold, CipherTierTag::Hot)`.  Strictly-weaker
#     storage-residency provider replacing a strictly-stronger
#     requirement.  Production consequence: KernelCache slot
#     expecting Hot-tier persistence (μs RAID-replicated recovery)
#     silently admits a cold-tier S3 blob; SLA recovery target
#     downgrades from μs to minutes.
#   * neg_decide_tier_replaces_determinism_downgrade — fires on
#     `(DetSafeTier::EntropyRead, DetSafeTier::Pure)`.  Strictly-
#     weaker determinism-budget provider replacing a Pure
#     requirement.  Production consequence: bit_exact_replay_
#     invariant CI test reddens; cross_vendor_step_invariant CI
#     fails; runtime drift-attribution loses its ground truth.
#
# A regression in tier_replaces (sign reversed; identity-only;
# strict-instead-of-non-strict) breaks BOTH fixtures.  A reviewer
# satisfying one must verify the other still holds — the
# storage-axis bug and the determinism-axis bug share one fix.
crucible_neg_compile_test(neg_decide_tier_replaces_storage_downgrade
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_tier_replaces_determinism_downgrade
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-071 fixtures — pin Decide::row_subset: payload-row ⊆
# Ctx-row substitution principle for Met(X) effect rows.  This is
# the THIN VC-discharge facade over effects::is_subrow_v, the
# canonical CRUCIBLE_PRE-callable spelling at every payload-row
# admission boundary (Stage::CtxFitsStage, Pipeline::CtxFitsPipeline,
# mint_endpoint substrate-row check, Cipher federation row
# intersection).  Two distinct mismatch classes — DIFFERENT bug
# families that map to NON-OVERLAPPING buggy implementations:
#
#   * neg_decide_row_subset_extra_effect — fires on
#     `(Row<Alloc, IO>, Row<Alloc>)`.  |Payload| > |Ctx|, atoms
#     partially overlap.  Production consequence: a kernel
#     declaring it may emit IO is admitted into an alloc-only
#     ExecCtx; runtime drift-attribution mis-classifies the I/O
#     latency spike.  Catches the "direction-reversed" and
#     "structural-equality" bug classes.
#   * neg_decide_row_subset_disjoint_axis — fires on
#     `(Row<Block>, Row<Alloc>)`.  |Payload| == |Ctx|, atoms
#     disjoint.  Production consequence: blocking syscall
#     embedded in a hot-path Stage; throughput collapse with no
#     row-level signal.  Catches the "cardinality-only" and
#     "any-overlap-passes" bug classes — which extra-effect
#     would NOT catch (size-only correctly rejects when
#     |Payload| > |Ctx|).
#
# A correct implementation passes both; a buggy size-only impl
# passes #1 and fails #2; a buggy direction-reversed impl passes
# #2 and fails #1.  Together the two fixtures cover the bug-class
# space.
crucible_neg_compile_test(neg_decide_row_subset_extra_effect
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_row_subset_disjoint_axis
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-072: crucible::decide::fmix_preserves_non_zero — xxHash /
# Murmur fmix64 finalizer non-zero-preservation invariant.  TWO HS14
# fixtures pinning ORTHOGONAL bug-class buckets:
#
#   * neg_decide_fmix_preserves_non_zero_zero_seed — fires on
#     `(seed = 0, mix_output = 0)`.  The canonical seed-zero bug:
#     a content_hash of 0 collides with the "not yet computed"
#     sentinel in KernelCache and Cipher head_, leading to false
#     cache hits on subsequent lookups.  Catches OUTPUT-ONLY-style
#     buggy implementations (`pre (mix != 0)` would also reject
#     this case but miss the companion fixture).
#   * neg_decide_fmix_preserves_non_zero_mix_zero — fires on
#     `(seed = 0xDEADBEEF, mix_output = 0)`.  A hypothetical
#     bijection-violating hash family that collapses non-zero
#     inputs to zero; the predicate's second clause catches the
#     collision at the publish gate.  Catches SEED-ONLY-style
#     buggy implementations (`pre (seed != 0)` would also reject
#     the companion but miss this case).
#
# A correct implementation passes both; a buggy seed-only impl
# passes #1 fails #2; a buggy mix-only impl passes #2 fails #1.
crucible_neg_compile_test(neg_decide_fmix_preserves_non_zero_zero_seed
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_fmix_preserves_non_zero_mix_zero
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-080: crucible::decide::conjunction + ::disjunction —
# span-based fold predicates over `bool` clauses.  FOUR HS14
# fixtures, TWO per procedure, each pair pinning ORTHOGONAL bug-
# class buckets:
#
# Conjunction (∧, identity = true on empty):
#   * neg_decide_conjunction_single_false — fires on `xs = {false}`.
#     Single-element-false witness.  Catches "always-true" / no-op-
#     impl bug class (a buggy `return true;` would COMPILE this
#     fixture; the correct impl rejects).  This is the smallest
#     possible witness.
#   * neg_decide_conjunction_or_instead_of_and — fires on
#     `xs = {true, true, true, false}`.  Last-element-false in a
#     multi-element span.  Catches "OR-instead-of-AND" bug class
#     (a buggy `any_of`/disjunction-fold short-circuits on the
#     first true and WRONGLY accepts; the correct impl rejects).
#     Also catches "first-element-only" bugs (`return xs[0]`).
#
# Disjunction (∨, identity = false on empty):
#   * neg_decide_disjunction_empty_span — fires on `xs = {}`.
#     Empty-span witness.  Catches "vacuous-truth" / WRONG-
#     IDENTITY bug class (a buggy impl that returns `true` on
#     empty input would COMPILE this fixture; the correct impl
#     rejects per `∨ ∅ = ⊥`).
#   * neg_decide_disjunction_all_false_multi — fires on
#     `xs = {false, false, false, false}`.  Multi-element all-
#     false witness.  Catches "always-true" / "non-empty-as-true"
#     bug class (a buggy `return true;` or `return !xs.empty();`
#     would COMPILE this fixture; the correct impl rejects).
#
# A correct implementation passes all four; each buggy variant
# fails at least one fixture in its procedure's pair, with the
# pair-mate catching the orthogonal bug class.  Distinct mismatch
# classes per HS14.
crucible_neg_compile_test(neg_decide_conjunction_single_false
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_conjunction_or_instead_of_and
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_disjunction_empty_span
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_disjunction_all_false_multi
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-081: crucible::decide::implies + ::aligned_in_range —
# material implication + composed bounds-and-alignment predicate.
# FOUR HS14 fixtures, TWO per procedure, each pair pinning
# ORTHOGONAL bug-class buckets:
#
# Material implication (p → q ≡ ¬p ∨ q):
#   * neg_decide_implies_true_false — fires on `(true, false)`.
#     The unique rejecting cell of the implication truth table.
#     Catches "always-true" / wrong-direction (converse) /
#     constant-true bug classes.
#   * neg_decide_implies_computed_antecedent — fires on
#     `(decide::is_power_of_two_le(8u, 64u), false)`.  Antecedent
#     is computed from another `decide::*` predicate; pins
#     compositional VC discharge (the canonical production-cite
#     shape) in addition to the rejecting-direction.
#
# Composed bounds + alignment (CLOSED interval [low, high] AND
# multiple of alignment):
#   * neg_decide_aligned_in_range_misaligned — fires on
#     `(value=100, low=0, high=200, alignment=8)`.  Value in
#     range but 100 % 8 = 4 ≠ 0.  Catches BOUNDS-ONLY-style
#     buggy impls that drop the alignment clause.
#   * neg_decide_aligned_in_range_out_of_range — fires on
#     `(value=512, low=0, high=256, alignment=64)`.  Value
#     correctly aligned (512 % 64 == 0) but exceeds high (=256).
#     Catches ALIGNMENT-ONLY-style buggy impls that drop the
#     upper-bound clause.
#
# Pair-by-pair orthogonality:
#   - implies pair: (T, F) literal vs (computed_T, F).  Both
#     reject under correct impl; an always-true impl accepts
#     both.  A pure-literal-only evaluator (pathological) would
#     pass #1 fail #2.
#   - aligned_in_range pair: in-range/misaligned vs aligned/
#     out-of-range.  A bounds-only impl passes #2 fails #1; an
#     alignment-only impl passes #1 fails #2.  Distinct mismatch
#     classes per HS14.
crucible_neg_compile_test(neg_decide_implies_true_false
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_implies_computed_antecedent
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_aligned_in_range_misaligned
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_aligned_in_range_out_of_range
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-100: crucible::decide::is_non_zero — structural-non-zero
# predicate over any type with `operator!=`.  TWO HS14 fixtures
# pinning ORTHOGONAL bug-class buckets:
#
#   * neg_decide_is_non_zero_integer_zero — witness
#     `std::uint64_t{0}` (the canonical built-in integer zero).
#     Catches ALWAYS-ACCEPT / INVERTED-SENSE buggy impls that
#     would silently admit the reserved zero sentinel at every
#     production cite (KernelCache slot-empty marker, Cipher
#     log-head sentinel, MerkleDag uninitialized RegionNode).
#   * neg_decide_is_non_zero_aggregate_zero — witness
#     `Aggregate{0, 0}` (mirror of cog::Uuid layout: two
#     defaulted-uint64 fields with operator<=>).  Catches
#     FIELD-MYOPIC / aggregate-comparison-broken impls that
#     handle the integer case correctly but drop fields on
#     multi-field aggregates (admits `Uuid{0, k}` for k≠0,
#     causing all such half-uninitialized Cogs to route to the
#     same KernelCache slot).
#
# Pair-by-pair orthogonality:
#   - integer-zero catches always-accept (`return true;`) and
#     inverted-sense (`return x == T{};`); a field-myopic impl
#     would also be caught (T{0} fails on the first field).
#   - aggregate-zero catches field-myopic (`return u.hi != 0;`)
#     and conjunction-of-fields (`return u.hi != 0 && u.lo != 0;`).
#     An always-accept impl would also be caught.
#   - The COMPANION positive `is_non_zero(AggregateZero{0, 1}) ==
#     true` static_assert in test_decide.cpp pins the OTHER
#     direction (half-zero MUST accept), catching a field-myopic
#     impl that correctly rejects {0,0} but wrongly rejects {0,1}.
crucible_neg_compile_test(neg_decide_is_non_zero_integer_zero
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_is_non_zero_aggregate_zero
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-102: crucible::decide::in_range — scalar closed-interval
# bounds-check predicate.  TWO HS14 fixtures pinning ORTHOGONAL
# bound-clause bug-class buckets:
#
#   * neg_decide_in_range_above_high — witness `(11, 0, 10)`.
#     Catches ALWAYS-ACCEPT / IGNORED-HI (drop-upper-clause) /
#     DISJUNCTION (wrong logical connective) impls that would
#     silently admit out-of-range values past the upper endpoint.
#   * neg_decide_in_range_below_low — witness `(0, 1, 10)` with
#     deliberate non-zero `lo`.  Catches ALWAYS-ACCEPT (already
#     covered) / IGNORED-LO (drop-lower-clause) / WRONG-DEFAULT-LOW
#     (hardcodes `lo=0`, ignoring the parameter) / DISJUNCTION
#     (already covered).  IGNORED-LO and WRONG-DEFAULT-LOW are
#     UNIQUELY caught by this fixture; the companion fixture's
#     witness uses lo=0 and so cannot detect a hardcoded-zero
#     impl.
#
# Pair-by-pair orthogonality:
#   - above-high catches drop-high + always-accept + disjunction.
#   - below-low catches drop-low + always-accept (covered) +
#     hardcoded-low + disjunction (covered).
#   - The COMPANION positive static_asserts in test_decide.cpp
#     (`in_range(10, 0, 10) == true`, `in_range(0, 0, 10) == true`)
#     pin the closed-interval endpoints, catching off-by-one
#     impls (`x < hi` instead of `x <= hi`) that this fixture
#     pair does not catch directly.
crucible_neg_compile_test(neg_decide_in_range_above_high
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_in_range_below_low
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-103: crucible::decide::positive — strict-positive lower-
# bound predicate (T{0} < x).  TWO HS14 fixtures pinning the FOUR
# named bug-class buckets via ORTHOGONAL witnesses:
#
#   * neg_decide_positive_zero — witness `0u` for unsigned T.
#     Catches ALWAYS-ACCEPT (returns true) / OFF-BY-ONE-INCLUSIVE
#     (`>= 0` admits zero) / INVERTED-SENSE (`<= 0` admits zero,
#     and every negative for signed T).  All three impls would
#     silently admit the canonical "no progress possible" sentinel
#     at every Arena cite (block_size=0 / n=0 / nbytes=0) and
#     Graph::reduction_body (nred=0).
#   * neg_decide_positive_negative — witness `int32_t{-1}` for
#     signed T.  UNIQUELY catches WRONG-CITE-TO-IS_NON_ZERO
#     (`x != T{0}` admits -1; agrees with positive on unsigned but
#     diverges on signed-negative inputs).  This is the most
#     insidious of the bug classes: a CONTRACT-* refactor that
#     accidentally renames the cite from `positive` to `is_non_zero`
#     passes the zero fixture, passes every unsigned production
#     cite at runtime, and silently admits negatives at signed
#     cites.
#
# Pair-by-pair orthogonality:
#   - zero fixture catches always-accept + off-by-one-inclusive +
#     inverted-sense (3 of 4 buckets); cannot detect wrong-cite
#     because is_non_zero(0) and positive(0) agree.
#   - negative fixture catches wrong-cite-to-is_non_zero (the 4th
#     bucket) + inverted-sense (covered).
#   - Together the two fixtures span all four named bug-class
#     buckets.  Two-fixture discipline is mandatory because a
#     single witness cannot distinguish wrong-cite from off-by-
#     one-inclusive (both behaviors agree on -1 OR on 0, never
#     on both).
#
# Companion positive static_asserts in test_decide.cpp pin the
# strict-zero rejection via runtime smoke-test routing through
# volatile sinks (zero-int / int-min-of-int8 / int-min-of-int16 /
# explicit-negative for safety::Tagged on signed T).
crucible_neg_compile_test(neg_decide_positive_zero
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_positive_negative
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-054: crucible::decide::non_negative — non-strict-positive
# lower-bound predicate (T{0} <= x).  TWO HS14 fixtures pinning the
# OVER-ADMISSION bug classes via ORTHOGONAL signed-negative witnesses:
#
#   * neg_decide_non_negative_minus_one — witness `int32_t{-1}`,
#     small-magnitude signed negative.  Catches ALWAYS-ACCEPT
#     (returns true) / INVERTED-SENSE (`T{0} >= x` admits negatives
#     and zero) / STRICT-INVERSE (`x < T{0}` admits negatives only) /
#     WRONG-CITE-TO-IS-NON-ZERO (`x != T{0}` admits -1 since
#     -1 != 0 is true).  All four impls silently admit the canonical
#     "caller error" sentinel at signed-int cites (Topology::
#     select_warm_cpus(count<0) → size_t underflow → SIGSEGV;
#     Monotonic::bump_by(delta<0) → counter steps backwards →
#     monotonicity invariant broken).
#   * neg_decide_non_negative_int_min — witness `INT32_MIN`,
#     boundary-magnitude signed negative.  UNIQUELY catches abs-
#     based-confusion (`std::abs(x) == x` admits INT_MIN due to
#     negation overflow), width-narrowing-truncation
#     (`uint8_t(x) <= INT8_MAX` admits INT_MIN due to low-byte zero),
#     and similar boundary-only bug shapes.  Defense-in-depth on
#     the bugs the minus_one fixture also catches (always-accept,
#     strict-inverse).
#
# Pair-by-pair orthogonality:
#   - minus_one fixture catches always-accept + inverted-sense +
#     strict-inverse + wrong-cite-to-is_non_zero (small-magnitude
#     family); cannot detect abs-based confusion (abs(-1)=1, !=-1,
#     correct rejection) or width-narrowing truncation (uint8_t(-1)
#     =255, > 127, correct rejection).
#   - int_min fixture catches abs-based + width-narrowing
#     (boundary-magnitude family) + always-accept + strict-inverse
#     (covered).
#   - Together the two fixtures span both small-magnitude and
#     boundary-magnitude over-admission classes.  Two-fixture
#     discipline is mandatory: a buggy abs-based impl behaves
#     correctly on -1 (rejecting it) but admits INT_MIN; the
#     boundary witness is the only way to catch this at consteval.
#
# Note: HS14 fixtures only catch OVER-admission (predicate admits
# something it should reject).  The strict-positive bug class
# (`positive` collapse, `> 0` instead of `>= 0` rejecting zero) is
# UNDER-admission and is caught at production call sites
# (Topology::select_warm_cpus(count=0) starts spuriously failing the
# pre-clause), not at this fixture pair.  Companion non_negative
# static_asserts in test_decide.cpp pin the zero-inclusive admission
# explicitly so the under-admission case is structurally rejected
# at consteval too.
crucible_neg_compile_test(neg_decide_non_negative_minus_one
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_non_negative_int_min
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# CONTRACT-055: crucible::decide::valid_span — span-shape disjunction
# predicate (count == C{0} || ptr != nullptr).  TWO HS14 fixtures
# pinning the OVER-ADMISSION bug classes via ORTHOGONAL (count, ptr)
# witnesses spanning both small-magnitude and width-narrowing:
#
#   * neg_decide_valid_span_count_only — witness (uint32_t{1},
#     nullptr).  Catches ALWAYS-ACCEPT (returns true) /
#     INVERTED-SENSE (`count != 0 && ptr == nullptr` admits exactly
#     the UB tuples this predicate exists to reject) /
#     OFF-BY-ONE-IN-COUNT-CLAUSE (`count <= 1 || ptr != nullptr`
#     admits (1, nullptr)) / OR-WITH-WRONG-COUNT-CLAUSE
#     (`count != 0 || ptr != nullptr` admits everything except the
#     legitimate empty-span case (0, nullptr)).  All four impls
#     silently admit the canonical "non-empty span backed by null
#     storage" UB shape at every count-and-pointer cite (TraceRing::
#     drain(out=nullptr, max_count=N>0) → SIGSEGV on first store;
#     MerkleDag region builder with (num_ops>0, ops=nullptr) →
#     arena corruption that persists past the SIGSEGV).
#   * neg_decide_valid_span_truncation — witness (uint32_t{256},
#     nullptr).  UNIQUELY catches WIDTH-NARROWING-TRUNCATION
#     (`uint8_t(count) == 0 || ptr != nullptr` admits (256, nullptr)
#     because uint8_t(256) = 0) and BIT-MASK-INSTEAD-OF-EQUALITY
#     (`(count & 0xFF) == 0 || ptr != nullptr` same root cause).
#     Defense-in-depth on always-accept and inverted-sense (covered).
#
# Pair-by-pair orthogonality:
#   - count_only fixture catches always-accept + inverted-sense +
#     off-by-one + or-with-wrong-count (small-magnitude family);
#     cannot detect uint8_t-truncation because uint8_t(1) = 1
#     (non-zero, doesn't trigger the truncation short-circuit).
#   - truncation fixture catches uint8_t-truncation + bit-mask
#     (low-byte-zero family) + always-accept + inverted-sense
#     (covered).
#   - Together the two fixtures span both small-magnitude and
#     low-byte-zero over-admission classes.  Two-fixture discipline
#     is mandatory: a buggy uint8_t-truncated impl behaves
#     correctly on count=1 (admits with non-null ptr, rejects with
#     null ptr) but admits (256, nullptr); the low-byte-zero
#     witness is the only way to catch this at consteval.
#
# Note: HS14 fixtures only catch OVER-admission (predicate admits
# something it should reject).  The over-restrictive bug class
# (`ptr != nullptr` only — drops the count==0 admission clause) is
# UNDER-admission and is caught at production call sites
# (TraceRing::drain(nullptr, 0) starts spuriously failing the
# pre-clause), not at this fixture pair.  Companion valid_span
# static_asserts in test_decide.cpp pin the empty-span (count=0)
# admission explicitly so the under-admission case is structurally
# rejected at consteval too.
crucible_neg_compile_test(neg_decide_valid_span_count_only
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_decide_valid_span_truncation
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

# ─────────────────────────────────────────────────────────────────
# fixy-A1-032 — OneShotFlag::reset_in_quiescent_context passkey gate
# ─────────────────────────────────────────────────────────────────
#
# Lifts the prior `reset_unsafe()` precondition (producer + consumer
# both quiescent at reset time) from doc-comment to type system via
# the `OneShotFlag::QuiescenceProof` passkey.  Two fixtures pin the
# rename's structural promise:
#
#   * neg_reset_in_quiescent_context_without_proof — primary.
#     Witness: zero-argument call.  Catches refactor-induced loss of
#     the parameter entirely; legacy `reset_unsafe()` was a no-arg
#     surface, so a mechanical rename without reading the new
#     signature dies here.
#
#   * neg_reset_in_quiescent_context_braced_init — companion.
#     Witness: `{}` implicit-conversion call.  Catches the bypass
#     that would defeat the audit-discoverability claim — `grep
#     "QuiescenceProof{}"` works only when every call site spells
#     the type literally.  The `explicit QuiescenceProof()` ctor
#     blocks `{}`; this fixture pins that property.
#
# Together the two cover the two shapes of "forgot the passkey"
# refactor mistake.  Diagnostic family naming is broad to absorb
# GCC dialect drift across the two shapes.
crucible_neg_compile_test(neg_reset_in_quiescent_context_without_proof
    "too few arguments|no matching function|candidate function not viable|expects 1 argument|expected.*argument")
crucible_neg_compile_test(neg_reset_in_quiescent_context_braced_init
    "explicit constructor|explicit.*conversion|no matching function|could not convert|cannot convert|not a valid template argument")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-001 — StopG<Peer, CrashClass C> projection threading
# ─────────────────────────────────────────────────────────────────
#
# Pre-fix, the global-level `StopG<Peer>` carried no CrashClass NTTP,
# so every projection unconditionally yielded the bare `Stop` alias
# (= `Stop_g<CrashClass::Abort>`).  The BSYZ22 four-tier crash
# lattice (Abort > Throw > ErrorReturn > NoThrow) collapsed to a
# single tier at the projection point — implementations could not
# distinguish "best-effort `Throw` peer crashed" from "BITEXACT_TC
# `Abort` peer crashed".  fixy-A2-001 threads `CrashClass C` (default
# Abort, preserving every pre-existing call site) through:
#
#   * StopG<Peer, C> primary template + crash_class accessor
#   * is_stop_g / flat_roles / is_global_well_formed /
#     has_interaction_between specializations
#   * ProjectImpl<StopG<Peer, C>, Peer, RootG>     (Peer's own)
#   * ProjectImpl<StopG<Peer, C>, Role, RootG>     (interacting peer)
#
# These two HS14 fixtures witness the property end-to-end:
#
#   * neg_stop_g_crashclass_dropped_in_peer_projection — primary.
#     Witness: `project_t<StopG<Bob, Throw>, Bob>` produces
#     `Send<Ping, Stop_g<Throw>>` post-fix, NOT `Send<Ping, Stop>`
#     (= `Send<Ping, Stop_g<Abort>>`).  Asserting the buggy collapse
#     to `Stop` fails post-fix; the file no longer compiles.
#
#   * neg_stop_g_crashclass_dropped_in_role_projection — companion.
#     Witness: `project_t<G, Alice>` for an interacting non-Peer
#     produces `Recv<Ping, Stop_g<Throw>>` post-fix, NOT
#     `Recv<Ping, Stop>`.  Covers the [GR-✂] crash-propagation arm
#     (has_interaction_between_v<RootG, Role, Peer> == true).
#
# Together the pair pins fixy-A2-001's promise that CrashClass
# propagates END-TO-END through projection — both on the crashed
# participant's side AND on every surviving role's side.  Diagnostic
# family naming is broad to absorb GCC dialect drift in the
# "static assertion failed" wording.
crucible_neg_compile_session_test(neg_stop_g_crashclass_dropped_in_peer_projection
    "static assertion failed|static assert|fixy-A2-001")
crucible_neg_compile_session_test(neg_stop_g_crashclass_dropped_in_role_projection
    "static assertion failed|static assert|fixy-A2-001")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-002 — Accept-of-Stop_g<C> composition bottom-preservation
# ─────────────────────────────────────────────────────────────────
#
# Pre-fix, SessionDelegate.h shipped four bottom-preservation compose
# rules on the Delegate side (Delegate<Stop_g<C>, K> and
# EpochedDelegate<Stop_g<C>, K, E, G>, plus the End-substitution
# reverse direction for each) but the Accept-side parallels were
# missing — the general `compose<Accept<T, K>, Q>` and
# `compose<EpochedAccept<T, K, E, G>, Q>` rules fired even when T was
# `Stop_g<C>`, producing structurally wrong types like
# `Accept<Stop_g<Throw>, compose_t<...>>` (claiming the recipient
# still advances K past an already-crashed delegated endpoint).
#
# The asymmetry broke a load-bearing algebraic property:
# `dual_of(Delegate<T, K>) = Accept<T, dual(K)>`, so duality and
# composition stopped commuting on the Accept arm — a property the
# framework documents at Session.h:730-732.
#
# fixy-A2-002 ships four symmetric Accept-side specializations:
#
#   * compose<Accept<Stop_g<C>, K>, Q>             -> Stop_g<C>
#   * compose<End, Accept<Stop_g<C>, K>>           -> Stop_g<C>
#   * compose<EpochedAccept<Stop_g<C>, K, E, G>, Q> -> Stop_g<C>
#   * compose<End, EpochedAccept<Stop_g<C>, K, E, G>> -> Stop_g<C>
#
# These two HS14 fixtures witness the property on BOTH Accept shapes:
#
#   * neg_compose_accept_stop_g_missing_bottom — primary.
#     Witness: `compose_t<Accept<Stop_g<Throw>, K>, Q>` pre-fix
#     produces `Accept<Stop_g<Throw>, compose_t<K, Q>>` (general
#     specialization fires).  Post-fix the new bottom-preservation
#     rule wins partial-ordering and the type collapses to
#     `Stop_g<Throw>`; the static_assert fires and the file no longer
#     compiles.
#
#   * neg_compose_epoched_accept_stop_g_missing_bottom — companion.
#     Witness: `compose_t<EpochedAccept<Stop_g<Throw>, K, 5, 3>, Q>`
#     pre-fix produces
#     `EpochedAccept<Stop_g<Throw>, compose_t<K, Q>, 5, 3>` —
#     phantom-preserving the MinEpoch/MinGen NTTPs on a crashed
#     channel.  Post-fix the bottom-preservation rule wins, drops the
#     epoch/generation NTTPs, and produces `Stop_g<Throw>`; the
#     static_assert fires.
#
# Together the pair pins fixy-A2-002's promise that BOTH Accept
# shapes collapse to Stop_g<C> under composition — restoring the
# duality identity end-to-end across the plain and Epoched paths.
# Diagnostic family naming matches the fixy-A2-001 pattern.
crucible_neg_compile_session_test(neg_compose_accept_stop_g_missing_bottom
    "static assertion failed|static assert|fixy-A2-002")
crucible_neg_compile_session_test(neg_compose_epoched_accept_stop_g_missing_bottom
    "static assertion failed|static assert|fixy-A2-002")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-028 — protocol_effect_row bottom-preservation
# ─────────────────────────────────────────────────────────────────
#
# `compose<Delegate<Stop_g<C>, K>, Q> = Stop_g<C>` was already
# shipped (GAPS-048 + fixy-A2-002) — the carrier collapses to
# bottom when the delegated channel is crashed at handoff.  But
# `protocol_effect_row` for the STANDALONE protocol still inherited
# from the general Delegate / Accept rule and walked K, surfacing
# K's effect row.  That asymmetry was a ROW NARROWING UNDER
# COMPOSITION soundness hole: composing the protocol weakened its
# Ctx admission requirement — a user writing
# `Delegate<Stop, Recv<Bg-bearing payload>>` claimed Bg admission
# standalone, but Stop_g<C> after compose admitted from any row.
#
# fixy-A2-028 ships bottom-preservation specializations for the
# row trait so it agrees with compose:
#
#   protocol_effect_row<Delegate<Stop_g<C>, K>>       -> Row<>
#   protocol_effect_row<Accept<Stop_g<C>, K>>         -> Row<>
#   (EpochedDelegate / EpochedAccept inherit via existing forward.)
#
# HS14 floor: two negative-compile fixtures, one per arm, each
# witnessing the pre-fix narrowing (K's row leaks through the
# Stop_g<C> carrier) so the post-fix bottom-preservation
# specialization's victory fails the static_assert.  Different
# CrashClass (Abort vs Throw) on each fixture pins that the
# bottom-preservation rule is CrashClass-agnostic.
crucible_neg_compile_session_test(neg_protocol_effect_row_delegate_stop_g_narrows_k
    "static assertion failed|static assert|fixy-A2-028")
crucible_neg_compile_session_test(neg_protocol_effect_row_accept_stop_g_narrows_k
    "static assertion failed|static assert|fixy-A2-028")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-029 — is_terminal_state<CheckpointedSession<B, R>> AND-fold
# ─────────────────────────────────────────────────────────────────
#
# Session.h:1132 declares the primary `is_terminal_state<P>` template
# as `std::bool_constant<std::is_same_v<P, End>>` — End is the only
# unconditionally-terminal protocol.  Branch-fold combinators carry
# the responsibility for distributing the trait over their components:
# `is_terminal_state<VendorPinned<V, P>>` recurses through P
# (Session.h:1135), `is_terminal_state<Stop_g<C>>` is hard-coded true
# (SessionCrash.h:183).  Pre-fix `CheckpointedSession<B, R>` had NO
# specialization — the primary fired and ALL CheckpointedSession<...>
# uniformly classified as non-terminal regardless of branch contents.
#
# fixy-A2-029 ships the parameterised AND-fold:
#   is_terminal_state<CheckpointedSession<B, R>>
#       : std::bool_constant<is_terminal_state<B>::value
#                            && is_terminal_state<R>::value>
#
# Twin-terminal cases now drop cleanly; asymmetric cases (one branch
# carries non-trivial work) remain rejected on abandonment.  The pair
# of fixtures below witnesses both arms of the AND-fold: fixture #1
# pins BASE-branch non-terminality (Send<int, End>), fixture #2 pins
# ROLLBACK-branch non-terminality (Recv<int, End>).  Different inner
# combinator on each fixture (Send vs Recv) to pin that the rejection
# is direction-agnostic.
crucible_neg_compile_session_test(neg_checkpointed_session_terminal_base_nonterminal
    "static assertion failed|static assert|fixy-A2-029")
crucible_neg_compile_session_test(neg_checkpointed_session_terminal_rollback_nonterminal
    "static assertion failed|static assert|fixy-A2-029")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-003 — is_dual_involutive specialization distribution
# ─────────────────────────────────────────────────────────────────
#
# Session.h:687 declares the primary template
# `is_dual_involutive<P> : std::true_type` so every unspecialized P
# classifies as involutive by default.  The trait-distribution
# convention is that each combinator's home header owns the
# specializations for its own types (Send/Recv/Select/Offer/Loop/
# VendorPinned at Session.h:689-713, fixy-CR-11's Sender-annotated
# Offer at Session.h:706-707).  Pre-fix Delegate / Accept /
# EpochedDelegate / EpochedAccept / CheckpointedSession / Stop_g
# all relied on the primary template's silent-true default — a
# bug because `is_dual_involutive` MUST distribute over the
# component protocols (conservative T ∧ K conjunction matching
# Session.h:680-684's policy on Sender-annotated Offer).  Without
# the specializations, `Delegate<Offer<Sender<R>, ...>, End>`
# silently passed `refines_self_and_double_dual_v`
# (SessionPatterns.h:944-1007) — admitting a protocol that drops
# the role tag through the duality round-trip, the exact algebraic
# flaw fixy-CR-11 records.
#
# fixy-A2-003 ships five new specializations distributed to home
# headers:
#
#   SessionDelegate.h:
#     * is_dual_involutive<Delegate<T, K>>
#     * is_dual_involutive<Accept<T, K>>
#     * is_dual_involutive<EpochedDelegate<T, K, E, G>>
#     * is_dual_involutive<EpochedAccept<T, K, E, G>>
#       — all bool_constant<is_dual_involutive<T> &&
#                           is_dual_involutive<K>>
#
#   SessionCheckpoint.h:
#     * is_dual_involutive<CheckpointedSession<B, R>>
#       — bool_constant<is_dual_involutive<B> &&
#                       is_dual_involutive<R>>
#
#   SessionCrash.h:
#     * is_dual_involutive<Stop_g<C>>
#       — std::true_type (Stop_g is self-dual, definitionally
#         involutive — explicit specialization for discoverability)
#
# HS14 floor: the pair `neg_is_dual_involutive_{delegate,
# checkpointed}_wraps_sender_offer.cpp` witnesses the pre-fix
# BUGGY classification by asserting the wrapping combinator is
# classified as involutive when it wraps `Offer<Sender<RoleA>,
# ...>` (which itself is non-involutive per fixy-CR-11).  Pre-fix
# the primary fires and the static_assert holds; post-fix the new
# specialization fires, the conjunction returns false, and the
# static_assert fires — both files fail to compile.
#
# Together the pair pins fixy-A2-003's promise that EVERY non-
# spec'd combinator distributes is_dual_involutive over its
# component protocols, closing the silent-true-for-unspecialized
# hole that admitted Sender-Offer-wrapped non-involutive protocols
# past the refines_self_and_double_dual_v gate.  Stop_g is covered
# by positive sentinel only (its specialization preserves the
# primary's behavior); the Delegate-family and CheckpointedSession
# negatives are sufficient HS14 coverage because Accept /
# EpochedDelegate / EpochedAccept share the same structural shape
# (`<T, K, ...>` with T ∧ K conjunction) as Delegate.
crucible_neg_compile_session_test(neg_is_dual_involutive_delegate_wraps_sender_offer
    "static assertion failed|static assert|fixy-A2-003")
crucible_neg_compile_session_test(neg_is_dual_involutive_checkpointed_wraps_sender_offer
    "static assertion failed|static assert|fixy-A2-003")

# ─────────────────────────────────────────────────────────────────
# fixy-A2-004 — is_empty_choice<CheckpointedSession<B, R>> recursion
# ─────────────────────────────────────────────────────────────────
#
# Symmetric to SessionDelegate.h:927-957's recursive specializations
# for Delegate/Accept/Epoched* (fixy-CR-14).  Pre-fix the primary
# template at Session.h:534 (`is_empty_choice<P>:std::false_type`)
# silently returned FALSE for every CheckpointedSession<B, R> — so
# `CheckpointedSession<Select<>, End>` and the symmetric recovery-
# branch defect passed the `is_empty_choice_v` mint gates at
# Session.h:2396 and PermissionedSession.h:1923 even though the
# wrapped branch was ill-formed.
#
# Post-fix `is_empty_choice<CheckpointedSession<B, R>>` distributes
# disjunctively over B and R: empty Choice in EITHER reachable arm
# (Try(B) | RollbackTo(R)) is a defect because both arms execute at
# runtime via rollback/abandon.  These two fixtures witness BOTH
# placement defects (empty Select in base, empty Offer in recovery)
# using DIFFERENT empty-Choice variants (Select<> vs Offer<>) — a
# regression that re-introduced the silent admission on either side
# would surface independently.
crucible_neg_compile_session_test(neg_is_empty_choice_checkpointed_base_empty_select
    "static assertion failed|static assert|fixy-A2-004")
crucible_neg_compile_session_test(neg_is_empty_choice_checkpointed_recovery_empty_offer
    "static assertion failed|static assert|fixy-A2-004")

# ─────────────────────────────────────────────────────────────────
# FIXY-GAP-B — Agent B round 2
# ─────────────────────────────────────────────────────────────────
#
# Theory.h §30.14 corpus expansion (G6/G7) — adds two literature-
# backed unsoundness detectors:
#
#   staleness_secret_without_declassify  — Hunt-Sands 2008 primary
#     ("Just Forget It" formalizes erasure as the dual axis of
#     staleness); Askarov-Hunt-Sabelfeld-Sands 2008 supports the
#     timing/replay-channel formalization; Sabelfeld-Sands 2009
#     survey-cite for the "when"-dimension conceptual frame (fixy-
#     H-17 demoted SS09 from primary to survey; fixy-CR-16 replaced
#     an earlier Andrysco-2015 attribution that did not back the
#     claim).
#     Pattern: as_secret + stale_to<N> + no declassify<Policy>.
#
#   ghost_runtime_observable             — Filliâtre-Gondelman-
#     Paskevich 2014 "The Spirit of Ghost Code" / Leino 2010
#     "Dafny" ghost-vs-runtime discipline (fixy-CR-17 replaced an
#     earlier Müller-Schwerhoff-Summers 2016 "Viper" attribution
#     that did not back the discipline-formalising claim; FGP 2014
#     is the canonical reference).
#     Pattern: ghost (Usage) + with<E...> containing Alloc/IO/
#     Block/Bg.  Distinct from R006 (collision marks_runtime_-
#     ghost_use) — this catches at type-row level without opt-in.
#
# Both detectors reject through NotInTheoryCorpus; fixy-H-16 added
# the matched-entry struct name to the diagnostic surface — regex
# requires BOTH the concept-fail trace AND the entry identifier so a
# weakened diagnostic immediately reddens the build.
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret
    "NotInTheoryCorpus|staleness_secret_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_ghost_observable
    "NotInTheoryCorpus|ghost_runtime_observable")

# fixy-M-14 — HS14 ≥2 floor for the ghost_runtime_observable corpus
# entry.  First fixture uses with_io to demonstrate the IO branch.
# This second fixture uses with_alloc to demonstrate the runtime-
# presence rule fires on a structurally distinct effect (heap, not
# external side effect).  Together they show the rule generalizes
# across the runtime-observable effect set; a future regression that
# accidentally narrowed the matcher to IO-only would still fail the
# first fixture but pass this one, naming the gap precisely.
crucible_neg_compile_fixy_test(neg_fixy_theory_ghost_observable_alloc
    "NotInTheoryCorpus|ghost_runtime_observable")

# fixy-CR-01 — staleness-secret strict-default closure.
# Second fixture mirroring the classified_io / classified_bg pair
# above, this time anchored on the staleness_secret_without_declassify
# corpus entry.  See those fixtures' commentary for the bypass shape.
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret_strict_default
    "NotInTheoryCorpus|staleness_secret_without_declassify")

# fixy-A4-015 — per-axis declassify discipline (Hunt-Sands 2008 /
# Sabelfeld-Myers 2003).  Pre-A4-015 the matcher accepted ANY
# declassify<Policy> as a universal silencer — the canonical "wrong
# policy authorizes the wrong axis" footgun.  Post-A4-015 the matcher
# consults `axes_discharged_of<Policy>` and silences ONLY when the
# policy's mask carries `DischargeAxis::Staleness` (currently only
# `AuthorizedReplay`).  Two fixtures (HS14 ≥2 floor) cover distinct
# wrong-axis policies (AuditedLogging — IO; WireSerialize — IO).
# Both regex on either NotInTheoryCorpus (concept-fail trace) OR the
# corpus entry name (corpus_full_diagnostic_v surface).
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret_wrong_policy_audited
    "NotInTheoryCorpus|staleness_secret_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret_wrong_policy_wire
    "NotInTheoryCorpus|staleness_secret_without_declassify")

# fixy-H-18 — internal_io_without_declassify corpus entry.
# Two fixtures (HS14 ≥2 floor):
#   neg_fixy_theory_internal_io          — minimal direct trigger
#     (as_internal + with_io alone) — anchors the new entry's matches()
#     against the Internal-tier no-write-down shape.
#   neg_fixy_theory_internal_io_multi_effect — proves the IO detection
#     fires when IO is one of several effects in a with<...> pack, not
#     just standalone — regression-guards is_io_effect_grant's pack-
#     contains semantics against future accidental narrowing.
# Both regex on either NotInTheoryCorpus (concept-fail trace) OR the
# new entry's struct name (corpus_full_diagnostic_v surface) so a
# diagnostic regression on EITHER surface reddens the build.
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_io
    "NotInTheoryCorpus|internal_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_io_multi_effect
    "NotInTheoryCorpus|internal_io_without_declassify")

# fixy-A4-008 — internal_bg_without_declassify corpus entry.
# Concurrent-channel dual of H-18: Smith-Volpano 1998 + Sabelfeld-
# Myers 2003 show sequential IFC type systems are UNSOUND under
# concurrency — a Bg-engaged Internal-tier binding declassifies
# through scheduler-observable progress / liveness regardless of any
# explicit I/O.  Two fixtures matching the HS14 ≥2 floor used for the
# sibling H-18 internal_io pair:
#   neg_fixy_theory_internal_bg          — minimal direct trigger
#     (as_internal + with_bg alone) — anchors matches() against the
#     Internal-tier concurrent no-write-down shape.
#   neg_fixy_theory_internal_bg_multi_effect — proves the Bg detection
#     fires when Bg is one of several effects in a with<...> pack, not
#     just standalone — regression-guards is_bg_effect_grant's pack-
#     contains semantics against future accidental narrowing.
# Both regex on either NotInTheoryCorpus (concept-fail trace) OR the
# new entry's struct name (corpus_full_diagnostic_v surface) so a
# diagnostic regression on EITHER surface reddens the build.
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_bg
    "NotInTheoryCorpus|internal_bg_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_bg_multi_effect
    "NotInTheoryCorpus|internal_bg_without_declassify")

# ── FIXY-ALGEBRA round 2 — concept-gate fixture depth ───────────────
# Existing Algebra.h coverage pins 2 fixtures (#1 non_lattice / #2
# non_graded); this round adds 3 more across distinct rejection
# classes within the Graded<M, L, T> + Lattice / Semiring / Bounded /
# GradedWrapper concept lattice:
#   #3 semiring_missing_zero — Semiring concept rejects a structure
#     with add/mul/one() but no zero() additive identity.
#   #4 bounded_missing_top   — BoundedLattice concept rejects a
#     half-bounded lattice (has bottom(), missing top()).
#   #5 gw_missing_name       — GradedWrapper concept rejects a
#     wrapper missing the value_type_name() / lattice_name() consteval
#     forwarders (CHEAT-3 diagnostic-surface gate).
crucible_neg_compile_fixy_test(neg_fixy_algebra_semiring_missing_zero
    "Semiring|associated constraints|constraints not satisfied|zero")
crucible_neg_compile_fixy_test(neg_fixy_algebra_bounded_missing_top
    "BoundedLattice|BoundedAboveLattice|associated constraints|constraints not satisfied|top")
crucible_neg_compile_fixy_test(neg_fixy_algebra_gw_missing_name
    "GradedWrapper|associated constraints|constraints not satisfied|value_type_name")

# ══════════════════════════════════════════════════════════════════════
# FIXY-GAP-A — Agent A round 2
# ══════════════════════════════════════════════════════════════════════
#
# Coverage extension shipped during the parallel FIXY-AUDIT sweep.
# Adds fixy-surface fixtures for the remaining mint factories on
# Perm.h (share, inherit) and Contract.h (restore, persisted_session),
# plus widens Is.h concept-gate coverage from 3 to 9 wrappers.

# ── FIXY-MINT-Perm round 2 — share + inherit through fixy::perm ─────
# Existing perm coverage pins root + fork + split/combine; this round
# closes the remaining two mints.  Four fixtures across distinct
# rejection classes:
#   - share_ctx_row_mismatch — ctx-bound share rejected when
#     Ctx::row does not admit permission_row<Tag> (NetworkBufferTag
#     needs IO; HotFgCtx has Row<>).  Pins CtxAdmitsPermission gate.
#   - share_bare_row_bearing — bare-form share rejected on
#     row-bearing tag (static_assert permission_row_empty_v<Tag>).
#   - inherit_without_specialization — fixy-routed equivalent of the
#     substrate fixture neg_mint_permission_inherit_without_specialization.
#     Confirms the re-export preserves the inherits_from gate.
#   - inherit_self_cycle — fixy-routed equivalent of the substrate
#     fixture neg_mint_permission_inherit_self_cycle.  Confirms the
#     re-export preserves the no-self-inheritance static_assert.
crucible_neg_compile_fixy_test(neg_fixy_perm_share_ctx_row_mismatch
    "CtxAdmitsPermission|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_perm_share_bare_row_bearing
    "permission_row|Row<>|static assertion failed")
crucible_neg_compile_fixy_test(neg_fixy_perm_inherit_without_specialization
    "mint_permission_inherit requires inherits_from|static assertion failed")
crucible_neg_compile_fixy_test(neg_fixy_perm_inherit_self_cycle
    "mint_permission_inherit forbids circular inheritance|static assertion failed")

# ── FIXY-U-014 HS14 floor — mint_read_view + mint_grid_permissions ──
# Eight using-decls added to fixy/Perm.h surface the
# Pool/Guard/ReadView/Fair/Grid machinery.  Two of those are §XXI
# mints — `mint_read_view` and `mint_grid_permissions` — so HS14
# requires ≥2 fixy_neg fixtures per mint demonstrating the
# substrate's gates fire IDENTICALLY through the fixy::perm:: alias.
# Mirrors the safety_neg companions for the substrate symbols.
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_read_view_non_permission_arg
    "mint_read_view|Permission|deduction|no matching|substitution failure")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_read_view_tag_mismatch
    "mint_read_view|Permission|no matching|cannot bind|conversion")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_grid_permissions_zero_m
    "can_split_grid|mint_grid_permissions|constraints not satisfied|no matching")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_grid_permissions_zero_n
    "can_split_grid|mint_grid_permissions|constraints not satisfied|no matching")

# ── FIXY-MINT-Contract round 2 — restore + persisted_session ────────
# Existing contract coverage pins promote/demote chain-direction; this
# round closes the remaining two mints.  Four fixtures:
#   - restore_unmovable — RestorableTier requires move_constructible,
#     fixy-routed equivalent of neg_cipher_tier_restore_unmovable_payload.
#   - restore_type_mismatch — cold handle T must match explicit
#     mint_restore<T>, fixy-routed equivalent of
#     neg_cipher_tier_restore_wrong_value_type.
#   - persisted_session_no_ctx — deleted overload with the structured
#     tag [PersistedSession_CtxRequired]; the bare no-Ctx form.
#   - persisted_session_no_openview — deleted overload with the
#     structured tag [PersistedSession_OpenViewRequired]; the
#     missing-OpenView form.
crucible_neg_compile_fixy_test(neg_fixy_contract_restore_unmovable
    "RestorableTier|move_constructible|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_contract_restore_type_mismatch
    "no matching function|could not convert|Cold")
# fixy-CR-10 fixture: production-shape payload (movable, no
# content_hash_projection specialization) must red at the
# RestorableHashed gate when routed through the fixy:: alias —
# mirrors substrate fixture neg_cipher_tier_restore_no_hash_projection.
crucible_neg_compile_fixy_test(neg_fixy_contract_restore_no_hash_projection
    "RestorableTier|RestorableHashed|content_hash_projection|constraints not satisfied|associated constraints are not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_contract_persisted_session_no_ctx
    "PersistedSession_CtxRequired|use of deleted function|deleted")
crucible_neg_compile_fixy_test(neg_fixy_contract_persisted_session_no_openview
    "PersistedSession_OpenViewRequired|use of deleted function|deleted")

# ── FIXY-IS round 2 — concept-gate coverage extension ───────────────
# Existing Is.h coverage pins 3 wrappers (Linear, Secret, SessionHandle);
# this round adds 6 more, covering canonical wrapper-nesting order from
# CLAUDE.md §XVI (HotPath, DetSafe, Stale, Tagged, Refined, Permission).
# Each fixture instantiates the IsX concept-as-requires-clause and
# confirms a plain unrelated type is rejected — alias identity check.
crucible_neg_compile_fixy_test(neg_fixy_is_tagged_rejects_plain
    "constraints not satisfied|associated constraints|IsTagged|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_refined_rejects_plain
    "constraints not satisfied|associated constraints|IsRefined|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_hotpath_rejects_plain
    "constraints not satisfied|associated constraints|IsHotPath|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_detsafe_rejects_plain
    "constraints not satisfied|associated constraints|IsDetSafe|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_stale_rejects_plain
    "constraints not satisfied|associated constraints|IsStale|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_is_permission_rejects_plain
    "constraints not satisfied|associated constraints|IsPermission|no matching function")

# ── FIXY-AUDIT-A3 — resolver totality probe ─────────────────────────
# The 19-axis Grants→Fn projector (fixy/Fn.h::detail::resolve) is
# ARCHITECTURALLY TOTAL over all IsGrantTag inputs: every axis has a
# strict default via accept_default_strict_for<D>, and `find_grant_t`
# falls back to that marker when no grant engages an axis.  There is
# NO per-axis projection-rejection class accessible through normal
# usage — engagement / collision rules are enforced earlier (Reject.h,
# §6.8 ValidComposition).
#
# The one structural rejection that DOES surface is malformed grants:
# `IsGrantTag` is purely structural (inherits grant_base + final), so a
# user-authored evil_grant satisfying IsGrantTag but lacking a
# `which_dim` specialization causes Reject.h's AllDimsEngaged fold to
# hit the undefined primary template.  This fixture pins that gate:
crucible_neg_compile_fixy_test(neg_fixy_grant_missing_which_dim
    "which_dim|incomplete type|no member named .value.|primary template")

# ── FIXY-AUDIT-A4 — Profile.h strict toggle witness ─────────────────
# `fixy::IsAcceptedActive` aliases `IsAccepted` under STRICT mode and
# `IsAcceptedSketch` (always-true) under SKETCH.  The default preset
# pins `CRUCIBLE_FIXY_STRICT=1` (CMakeLists.txt §367-374), so this
# fixture confirms `IsAcceptedActive` is wired through to the strict
# gate identically to `IsAccepted`.
#
# Architectural finding (round-2 report): `IsAcceptedActive` is shipped
# but UNCONSUMED today — `fixy::fn`'s class-body static_assert still
# routes through `IsAccepted` (strict-only) — post fixy-H-05 rename.
# The B2 ship explicitly
# defers the production rewire to a follow-up commit (Profile.h
# §"Scope note (B2 ship)").  The fixture below pins the infrastructure
# behavior so the eventual rewire cannot regress silently.
crucible_neg_compile_fixy_test(neg_fixy_profile_active_strict_rejects
    "static assertion failed|FixyNotEngaged|IsAcceptedActive|constraints not satisfied")

# ─────────────────────────────────────────────────────────────────
# FIXY-GAP-B — Agent B round 2 (B3 — Cap.h rejection-class depth)
# ─────────────────────────────────────────────────────────────────
#
# Existing Cap.h coverage pins 2 fixtures (mint_from_fg /
# mint_from_ctx_wrong_effect) — BOTH reject on the SECOND clause
# (row containment) of CanMintCap / CtxCanMint.  This round adds 2
# more rejecting on the FIRST (structural) clause:
#   - mint_plain_source — plain int as Source; CanMintCap rejects
#     on is_cap_type_v<int> == false.  Distinct from mint_from_fg,
#     which uses a valid Fg cap_type and fails on row.
#   - mint_from_non_ctx — non-ExecCtx struct passed to
#     mint_from_ctx; CtxCanMint rejects on IsExecCtx == false.
#     Distinct from mint_from_ctx_wrong_effect, which uses a valid
#     HotFgCtx and fails on row.
#
# The two-clause structure of CanMintCap / CtxCanMint is now
# saturated: structural gate × row-containment gate = 2 × 2 = 4
# rejection cells, one fixture per cell.
crucible_neg_compile_fixy_test(neg_fixy_cap_mint_plain_source
    "CanMintCap|is_cap_type|associated constraints|constraints not satisfied")
crucible_neg_compile_fixy_test(neg_fixy_cap_mint_from_non_ctx
    "CtxCanMint|IsExecCtx|associated constraints|constraints not satisfied")

# ── FIXY-FEDERATION round 2 — cross-org isolation + handshake gate ──
# Existing Source.h federation coverage pins 2 fixtures (wrong_org /
# missing_proof) — both reject at the mint_federation_admittance
# call site on the PROOF parameter.  This round adds 2 more covering
# the consumer-side and handshake-side gates:
#   - cross_org_transfer — Permission<FederatedPeer<OrgA>> consumed
#     by a federation operation typed for OrgB.  Tag-mismatch at the
#     consumer call site (deserialize_federation_entry<OrgB>).  This
#     is the type-system-level "every org has its own permission
#     universe" security gate.
#   - handshake_wrong_type — non-FederationHandshake POD passed in
#     the handshake slot of mint_federation_admittance.  The substrate
#     takes the handshake BY VALUE of the exact type; a different
#     struct type cannot bind even if layout-compatible (C++ has no
#     structural typing for distinct class types).
#
# Together with the 2 existing fixtures: the federation boundary is
# saturated across both parameter axes (proof × handshake) plus
# consumer-side tag-mismatch.
crucible_neg_compile_fixy_test(neg_fixy_federation_cross_org_transfer
    "FederatedPeer|cannot convert|cannot bind|no matching function")
crucible_neg_compile_fixy_test(neg_fixy_federation_handshake_wrong_type
    "FederationHandshake|cannot convert|no matching function")

# fixy-CR-05 — defensive splits_into partial in FederationPermission.h
# rejects accidental cross-org splits (no malicious user-side
# specialization in scope).  The binary and N-ary fixtures together
# satisfy the HS14 ≥2 floor for fixy-CR-05's defensive partial.
crucible_neg_compile_fixy_test(neg_fixy_federation_cross_org_split
    "splits_into|requires|static assertion")
crucible_neg_compile_fixy_test(neg_fixy_federation_cross_org_split_n
    "splits_into_pack|requires|static assertion")

# fixy-CR-06 — once-per-program federation root-mint gap closed.
# Both `mint_permission_root<FederatedPeer<Org>>()` and the ctx-bound
# `mint_permission_root<FederatedPeer<Org>>(ctx)` overloads in
# `crucible::safety` are concept-deleted for any tag satisfying
# `is_federated_peer_tag_v<Tag>`.  The only legitimate path is
# `mint_federation_admittance<Org, Policy>(local_cipher, handshake)`.
# The no-ctx and ctx fixtures together satisfy the HS14 ≥2 floor.
crucible_neg_compile_fixy_test(neg_fixy_federation_root_mint_disallowed
    "deleted|use of deleted function|mint_permission_root")
crucible_neg_compile_fixy_test(neg_fixy_federation_root_mint_disallowed_ctx
    "deleted|use of deleted function|mint_permission_root")

# fixy-CR-07 — federation session mints require admittance witness.
# `mint_sender<Org, KeyTag>(ctx, endpoint, admittance)` (and its
# receiver / coord / channel siblings in
# `crucible::safety::proto::federation`) take a
# `Permission<FederatedPeer<Org>> const&` as the load-bearing soundness
# rail.  Two distinct failure classes prove the gate:
#
#   no_admittance — 3-arg call site (ctx, endpoint) omits the witness;
#                   arity check refuses (no overload takes 2 args).
#   wrong_org     — admittance witness is `Permission<FederatedPeer<OrgA>>`
#                   but the session is minted for `OrgB`; the phantom
#                   tags do not bind across orgs.
crucible_neg_compile_fixy_test(neg_fixy_federation_session_no_admittance
    "no matching function|too few arguments|requires 3 arguments|candidate")
crucible_neg_compile_fixy_test(neg_fixy_federation_session_wrong_org
    "FederatedPeer|cannot bind|no known conversion|no matching function")

# fixy-A2-016 — 12 negative-compile fixtures across 6 bridges/* mint
# factories (≥2 per factory).  Each fixture's expected diagnostic
# regex is wide enough to tolerate GCC phrasing drift across patch
# levels but specific enough to fail when the underlying gate goes
# missing.  Driver: scripts/neg_compile_driver.py.
crucible_neg_compile_bridges_test(neg_mint_recording_session_non_handle
    "no matching function for call to|could not match")
crucible_neg_compile_bridges_test(neg_mint_recording_session_wrong_log_type
    "no matching function for call to|cannot bind|could not convert")
crucible_neg_compile_bridges_test(neg_mint_recording_session_crash_watched_non_handle
    "no matching function for call to|could not match")
crucible_neg_compile_bridges_test(neg_mint_recording_session_crash_watched_wrong_log_type
    "no matching function for call to|could not match|could not convert")
crucible_neg_compile_bridges_test(neg_mint_recording_endpoint_non_endpoint
    "no matching function for call to|could not match|cannot bind rvalue")
crucible_neg_compile_bridges_test(neg_mint_recording_endpoint_wrong_log_type
    "no matching function for call to|could not match|template argument deduction")
crucible_neg_compile_bridges_test(neg_mint_crash_watched_endpoint_non_endpoint
    "no matching function for call to|could not match|cannot bind rvalue")
crucible_neg_compile_bridges_test(neg_mint_crash_watched_endpoint_missing_peer_tag
    "no matching function for call to|couldn't infer|could not deduce|PeerTag")
crucible_neg_compile_bridges_test(neg_mint_persisted_session_ctx_required
    "PersistedSession_CtxRequired|use of deleted function")
crucible_neg_compile_bridges_test(neg_mint_persisted_session_ctx_row_insufficient
    "constraints not satisfied|CtxAdmits|no matching function")
crucible_neg_compile_bridges_test(neg_mint_persisted_session_handle_open_view_required
    "PersistedSession_OpenViewRequired|use of deleted function")
crucible_neg_compile_bridges_test(neg_mint_persisted_session_handle_ctx_row_insufficient
    "constraints not satisfied|CtxAdmits|no matching function")

# fixy-A2-006b — HS14 floor for the Delegate<DelegatedSession<P, IPS>, K>
# specialization on RecordingPermissionedSessionHandle.  The class spec
# adds .delegate() and .delegate_local() methods.  The .delegate() method
# binds the delegated handle to PSH<InnerProto, ...> (InnerProto = class-
# fixed, NOT deducible) and carries a two-conjunct requires-clause:
#   !is_stop_v<InnerProto>                                  (1st conjunct)
#   std::is_invocable_v<Transport, Resource&, DelegatedResource&&>
#                                                            (2nd conjunct)
# Each fixture witnesses one mismatch class:
#   wrong_inner_proto         — delegated handle's protocol != InnerProto
#                                rejects parameter binding (signature gate)
#   transport_not_invocable   — Transport = int rejects 2nd requires conjunct
# Distinct layers: #1 = signature shape, #2 = requires-clause body.
# Mirrors the bare-handle Delegate spec at RecordingSessionHandle.h:916.
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_delegate_wrong_inner_proto
    "no matching function|cannot convert|PermissionedSessionHandle|delegate")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_delegate_transport_not_invocable
    "constraints not satisfied|is_invocable|no matching function|delegate")

# fixy-A2-006c — HS14 floor for the Accept<DelegatedSession<P, IPS>, K>
# specialization on RecordingPermissionedSessionHandle.  The class spec
# adds .accept() and .accept_with() methods.  The .accept() method
# carries a single-conjunct requires-clause:
#   std::is_invocable_v<Transport, Resource&>
# Each fixture witnesses one mismatch class at a distinct layer:
#   transport_not_invocable   — Transport = int rejects the requires
#                                clause (REQUIRES-CLAUSE layer)
#   transport_returns_void    — Transport callable but returns void;
#                                requires passes but DelegatedResource
#                                deduces to void, body instantiation
#                                of inner_.accept(...) fails to declare
#                                a void variable (BODY layer)
# Distinct layers: #1 = requires-clause body, #2 = body instantiation.
# Mirrors the bare-handle Accept spec at RecordingSessionHandle.h:994.
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_accept_transport_not_invocable
    "constraints not satisfied|is_invocable|no matching function|accept")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_accept_transport_returns_void
    "incomplete type|void|cannot declare|no matching function|accept")

# fixy-A2-006d — HS14 floor for the EpochedDelegate<DelegatedSession<P, IPS>,
# K, MinEpoch, MinGeneration> specialization on RecordingPSH.  Mirror of the
# Delegate spec (006b) at the EpochedDelegate level — same .delegate() /
# .delegate_local() API, same two-conjunct requires-clause.  Epoch gate is
# enforced upstream by the inner PSH at carrier-construction time; the
# wrapper inherits it transparently.  Each fixture witnesses one mismatch
# class:
#   epoched_wrong_inner_proto       — signature-shape (InnerProto mismatch)
#   epoched_transport_not_invocable — requires-clause (is_invocable_v=false)
# Distinct layers: #1 = signature shape, #2 = requires-clause body.
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_delegate_wrong_inner_proto
    "no matching function|cannot convert|PermissionedSessionHandle|delegate")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_delegate_transport_not_invocable
    "constraints not satisfied|is_invocable|no matching function|delegate")

# fixy-A2-006e — HS14 floor for the EpochedAccept<DelegatedSession<P, IPS>,
# K, MinEpoch, MinGeneration> specialization on RecordingPSH.  Closes the
# A2-006 delegation family.  Mirror of the Accept spec (006c) at the
# EpochedAccept level — same .accept() / .accept_with() API, same
# requires-clause.  Recipient-side epoch gate (satisfies-relation >=) is
# enforced upstream by the inner PSH at carrier-construction time; the
# wrapper inherits it transparently.  Each fixture witnesses one mismatch
# class:
#   epoched_transport_not_invocable — requires-clause (is_invocable_v=false)
#   epoched_transport_returns_void  — body-instantiation (void delegated_res)
# Distinct layers: #1 = requires-clause body, #2 = body instantiation.
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_accept_transport_not_invocable
    "constraints not satisfied|is_invocable|no matching function|accept")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_accept_transport_returns_void
    "incomplete type|void|cannot declare|no matching function|accept")

# fixy-A2-019 — HS14 floor for the renamed mint_atomic_session<Proto>(cell).
# Two fixtures witness the two-conjunct requires-clause:
#   AtomicMachineCell<Cell>          — non-cell argument fails the concept
#   safety::proto::is_well_formed_v<Proto>  — free Continue at top level fails
crucible_neg_compile_bridges_test(neg_mint_atomic_session_non_atomic_machine_cell
    "no matching function for call to|constraints not satisfied|AtomicMachineCell")
crucible_neg_compile_bridges_test(neg_mint_atomic_session_malformed_proto
    "no matching function for call to|constraints not satisfied|is_well_formed")

# fixy-A2-021 — HS14 floor for the WrapCrashReturnKey pass-key idiom.
# Two fixtures witness that external code cannot mint or use the key:
#   neg_wrap_crash_return_key_external_construction — bare `Key{}` rejected
#   neg_crash_event_external_construction          — `CrashEvent<P,R>{}` rejected
# The non-template `detail::WrapCrashReturnAuthorizer` friend is immune to
# parameter-pack drift on `wrap_crash_return` (template-friend brittleness).
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_external_construction
    "is private within this context|private|WrapCrashReturnKey")
crucible_neg_compile_bridges_test(neg_crash_event_external_construction
    "is private within this context|private|WrapCrashReturnKey")

# FIXY-V-013 — HS14 fixture for §XXI Universal Mint Pattern compliance.
# Closes a different bypass class than the two above: a caller who
# legitimately mints a `WrapCrashReturnKey` through the public
# `detail::WrapCrashReturnAuthorizer::mint()` surface, then attempts to
# feed it to `CrashEvent<P,R>{key, r, perms}` directly, bypassing
# `wrap_crash_return`'s `inner.detach(reason_tag)` side effect.  V-013
# moves the ctor to `private:` with friendship on the authorizer; the
# access check fires here, leaving `mint_event<>` as the sole
# grep-discoverable construction site for CrashEvent in the tree.
crucible_neg_compile_bridges_test(neg_crash_event_ctor_public_bypass
    "is private within this context|private")

# fixy-A2-032 — HS14 floor for the WrapCrashReturnKey non-fungibility
# property.  Two fixtures witness that the passkey cannot be duplicated
# even when held by reference: the copy-ctor and move-ctor are both
# `= delete("fixy-A2-032: ...")`.  C++17 mandatory copy-elision keeps
# the production path (mint() prvalue → CrashEvent value parameter)
# intact, so the deletion is defense-in-depth at zero call-site cost.
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_copy_construct
    "use of deleted function|passkey cannot be copied|fixy-A2-032|deleted")
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_move_construct
    "use of deleted function|passkey cannot be moved|fixy-A2-032|deleted")
