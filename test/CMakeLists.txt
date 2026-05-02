# ═══════════════════════════════════════════════════════════════
# Test targets — inherit 200+ warning/hardening flags from the
# crucible INTERFACE library, plus sanitizers below.
# ═══════════════════════════════════════════════════════════════

option(CRUCIBLE_SANITIZE "Enable sanitizers in Debug test builds" ON)
option(CRUCIBLE_TSAN "Enable ThreadSanitizer (AxRaceFree, mutually exclusive with CRUCIBLE_SANITIZE)" OFF)
option(CRUCIBLE_ANALYZER "Enable GCC -fanalyzer (slow, thorough)" OFF)

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
# ═══════════════════════════════════════════════════════════════
# SECTION 10: GCC static analyzer (opt-in, 30+ checks)
#
#   273. -fanalyzer                    interprocedural analysis
#   274. -Wanalyzer-double-free        double free
#   275. -Wanalyzer-double-fclose      double fclose
#   276. -Wanalyzer-free-of-non-heap   free(stack_ptr)
#   277. -Wanalyzer-null-dereference   null deref paths
#   278. -Wanalyzer-possible-null-dereference
#   279. -Wanalyzer-null-argument      null arg
#   280. -Wanalyzer-possible-null-argument
#   281. -Wanalyzer-use-after-free     use-after-free
#   282. -Wanalyzer-malloc-leak        malloc without free
#   283. -Wanalyzer-file-leak          FILE* leak
#   284. -Wanalyzer-fd-leak            fd leak
#   285. -Wanalyzer-fd-use-after-close fd use after close
#   286. -Wanalyzer-fd-double-close    double close fd
#   287. -Wanalyzer-fd-access-mode-mismatch  wrong mode
#   288. -Wanalyzer-fd-type-mismatch   wrong fd type
#   289. -Wanalyzer-fd-phase-mismatch  socket phase error
#   290. -Wanalyzer-fd-use-without-check  fd without check
#   291. -Wanalyzer-tainted-array-index    tainted index
#   292. -Wanalyzer-tainted-allocation-size tainted alloc
#   293. -Wanalyzer-tainted-divisor        tainted divisor
#   294. -Wanalyzer-tainted-offset         tainted offset
#   295. -Wanalyzer-tainted-size           tainted size
#   296. -Wanalyzer-tainted-assertion      tainted assert
#   297. -Wanalyzer-shift-count-negative   neg shift
#   298. -Wanalyzer-shift-count-overflow   shift overflow
#   299. -Wanalyzer-deref-before-check     deref then check
#   300. -Wanalyzer-undefined-behavior-ptrdiff  UB ptrdiff
#   301. -Wanalyzer-infinite-loop          infinite loop
#   302. -Wanalyzer-infinite-recursion     infinite recursion
#   303. -Wanalyzer-va-arg-type-mismatch   va_arg type
#   304. -Wanalyzer-va-list-exhausted      va_list done
#   305. -Wanalyzer-va-list-leak           va_list leak
#   306. -Wanalyzer-va-list-use-after-va-end  va_list UAF
#   307. -Wanalyzer-out-of-bounds          OOB access
#   308. -Wanalyzer-overlapping-buffers    overlap
#   309. -Wanalyzer-exposure-through-output-file  info leak
#   310. -Wanalyzer-exposure-through-uninit-copy  uninit copy
#   311. -Wanalyzer-write-to-const         write to const
#   312. -Wanalyzer-write-to-string-literal string write
#   313. -Wanalyzer-use-of-uninitialized-value  uninit
#   314. -Wanalyzer-use-of-pointer-in-stale-stack-frame  stale
#   315. -Wanalyzer-mismatching-deallocation  wrong free
#   316. -Wanalyzer-allocation-size        alloc size mismatch
#   317. -Wanalyzer-stale-setjmp-buffer    stale setjmp
#   318. -Wanalyzer-jump-through-null      null function call
#   319. -Wanalyzer-putenv-of-auto-var     putenv(local)
#   320. -Wanalyzer-throw-of-unexpected-type  bad throw
#   321. -Wanalyzer-unsafe-call-within-signal-handler
#   322. -Wanalyzer-imprecise-fp-arithmetic  fp precision
#   323. -Wanalyzer-symbol-too-complex     analysis limit
#   324. -Wanalyzer-undefined-behavior-strtok  strtok UB
#
# ═══════════════════════════════════════════════════════════════

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

# GCC static analyzer (opt-in).
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CRUCIBLE_ANALYZER)
  set(CRUCIBLE_ANALYZER_FLAGS
    -fanalyzer
    -Wanalyzer-double-free
    -Wanalyzer-double-fclose
    -Wanalyzer-free-of-non-heap
    -Wanalyzer-null-dereference
    -Wanalyzer-possible-null-dereference
    -Wanalyzer-null-argument
    -Wanalyzer-possible-null-argument
    -Wanalyzer-use-after-free
    -Wanalyzer-malloc-leak
    -Wanalyzer-file-leak
    -Wanalyzer-fd-leak
    -Wanalyzer-fd-use-after-close
    -Wanalyzer-fd-double-close
    -Wanalyzer-fd-access-mode-mismatch
    -Wanalyzer-fd-type-mismatch
    -Wanalyzer-fd-phase-mismatch
    -Wanalyzer-fd-use-without-check
    -Wanalyzer-tainted-array-index
    -Wanalyzer-tainted-allocation-size
    -Wanalyzer-tainted-divisor
    -Wanalyzer-tainted-offset
    -Wanalyzer-tainted-size
    -Wanalyzer-tainted-assertion
    -Wanalyzer-shift-count-negative
    -Wanalyzer-shift-count-overflow
    -Wanalyzer-deref-before-check
    -Wanalyzer-undefined-behavior-ptrdiff
    -Wanalyzer-infinite-loop
    -Wanalyzer-infinite-recursion
    -Wanalyzer-va-arg-type-mismatch
    -Wanalyzer-va-list-exhausted
    -Wanalyzer-va-list-leak
    -Wanalyzer-va-list-use-after-va-end
    -Wanalyzer-out-of-bounds
    -Wanalyzer-overlapping-buffers
    -Wanalyzer-exposure-through-output-file
    -Wanalyzer-exposure-through-uninit-copy
    -Wanalyzer-write-to-const
    -Wanalyzer-write-to-string-literal
    -Wanalyzer-use-of-uninitialized-value
    -Wanalyzer-use-of-pointer-in-stale-stack-frame
    -Wanalyzer-mismatching-deallocation
    -Wanalyzer-allocation-size
    -Wanalyzer-stale-setjmp-buffer
    -Wanalyzer-jump-through-null
    -Wanalyzer-putenv-of-auto-var
    -Wanalyzer-throw-of-unexpected-type
    -Wanalyzer-unsafe-call-within-signal-handler
    -Wanalyzer-imprecise-fp-arithmetic
    -Wanalyzer-undefined-behavior-strtok
  )
endif()

# ── Test target factory ──────────────────────────────────────
function(crucible_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)

  # Sanitizers — test targets only (not the shared library).
  if(CRUCIBLE_SAN_COMPILE_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_SAN_COMPILE_FLAGS})
    target_link_options(${name} PRIVATE ${CRUCIBLE_SAN_LINK_FLAGS})
  endif()

  # GCC static analyzer.
  if(CRUCIBLE_ANALYZER_FLAGS)
    target_compile_options(${name} PRIVATE ${CRUCIBLE_ANALYZER_FLAGS})
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
crucible_test(test_simd)
crucible_test(test_types)
crucible_test(test_kernel_cache_key)
crucible_test(test_row_hash_fold)
crucible_test(test_cost_model)
crucible_test(test_saturate)
crucible_test(test_checked)
crucible_test(test_call_site_table)
crucible_test(test_swiss_table)
crucible_test(test_meta_log)
crucible_test(test_symbol_table)
crucible_test(test_trace_graph)
crucible_test(test_trace_loader)
crucible_test(test_schema_table)
crucible_test(test_effects)
crucible_test(test_iteration_detector)
crucible_test(test_arena)
crucible_test(test_trace_ring)
crucible_test(test_merkle_dag)
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
crucible_test(test_permissions_compile)
crucible_test(test_handles_compile)
crucible_test(test_sessions_compile)
crucible_test(test_bridges_compile)
crucible_test(test_effects_compile)
crucible_test(test_concurrent_compile)
crucible_test(test_diagnostic_compile)
crucible_test(test_stable_name_compile)
crucible_test(test_signature_traits)
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
crucible_test(test_graded_wrapper_gate)
crucible_test(test_storage_nbytes_simd)
crucible_test(test_trace_ring_pop_batch)
crucible_test(test_expr_pool_fast_symbol)
crucible_test(test_sharded_grid)
crucible_test(test_queue_facade)
crucible_test(test_permission_shared)
crucible_test(test_fair_permission_shared)
crucible_test(test_owned_region)
crucible_test(test_permission_tree_generator)
crucible_test(test_permission_grid_generator)
crucible_test(test_read_view)
crucible_test(test_topology)
crucible_test(test_parallelism_rule)
crucible_test(test_permissioned_snapshot)
crucible_test(test_permissioned_mpsc_channel)
crucible_test(test_permissioned_mpmc_channel)
crucible_test(test_permissioned_sharded_grid)
crucible_test(test_permissioned_chase_lev_deque)
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

# bench harness unit tests — include bench/ for bench_harness.h + bpf_senses.h.
# CRUCIBLE_BENCH_HAVE_BPF is intentionally unset: the test exercises only
# the non-BPF statistical primitives plus Snapshot::operator- (which is
# a header-only inline using crucible::sat::sub_sat from Saturate.h).
# Use SYSTEM include to silence -Werror diagnostics from the headers we
# don't own (other agents' territory); our .cpp still honours -Werror.
crucible_test(test_bench_harness)
# Exercises the crucible::rt module — realtime policy rails
# (SCHED_FIFO/RR priority clamp, mlock, CPU-pin helpers). Header-only,
# no special capabilities required at test time (the smoke checks the
# policy arithmetic / guard object semantics, not live scheduler calls).
crucible_test(test_rt_policy)
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

# ContentAddressed<T> quotient combinator (SEPLOG-L1, task #361,
# session_types.md Appendix D.5) — payload wrapper marking a value
# type as eligible for wire-level dedup at the transport layer.
# Mutual is_subsort integration makes Send<ContentAddressed<T>, K>
# and Send<T, K> interchangeable at the session-type level.
# Supports Cipher publication, KernelCache broadcast, PagedKVCache
# prefix sharing, replay trace dedup.
crucible_test(test_session_content_addressed)

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

# Dedicated TU re-enabling every session-type self-test namespace
# (#372 SEPLOG-PERF-2).  Each *_self_test namespace across 17 session
# headers is gated on `CRUCIBLE_SESSION_SELF_TESTS`; this TU defines
# the macro before including the headers, instantiating ~1000
# cumulative static_asserts in one place rather than per-consumer.
crucible_test(test_session_self_tests)

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
# a runtime wire: CrashWatchedHandle<Proto, R, PeerTag> wraps a bare
# SessionHandle with a OneShotFlag& and returns std::expected<
# NextHandle, CrashEvent<PeerTag, R>> from every consumer method.
# On-crash the per-op peek routes into the unexpected branch;
# happy-path overhead is one relaxed atomic load.  Validates happy
# path, crash-before-send, crash-mid-protocol, cross-thread crash
# detection (jthread producer + spinning consumer), multi-peer
# independent watches, and the CNTP request/reply + retry pattern.
crucible_test(test_crash_transport)

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
  # via the ctest harness below.
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
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target ${name} --config $<CONFIG>)
  set_tests_properties(${name} PROPERTIES
    LABELS "safety_neg"
    PASS_REGULAR_EXPRESSION "${expected_diag}")
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
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target ${name} --config $<CONFIG>)
  set_tests_properties(${name} PROPERTIES
    LABELS "sessions_neg"
    PASS_REGULAR_EXPRESSION "${expected_diag}")
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
    COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR}
            --target ${name} --config $<CONFIG>)
  set_tests_properties(${name} PROPERTIES
    LABELS "effects_neg"
    PASS_REGULAR_EXPRESSION "${expected_diag}")
endfunction()

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
    "static assertion failed|cap_permitted_row|Subrow")
crucible_neg_compile_effects_test(neg_exec_ctx_heat_resid_incoherent
    "static assertion failed|heat_resid_coherent|Heat × Resid")
crucible_neg_compile_effects_test(neg_exec_ctx_pinned_negative_node
    "static assertion failed|Pinned.*requires N >= 0|Node >= 0")

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
    "is private within this context|private member|non-public member|is private")

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

# ─── P1-AUDIT-5 (#856): wrapper-lift + tier-discrimination gates ───
#
# Two fixtures: AllocClassFromCtx rejects Unbound (no wrapper
# analogue); IsL1ResidentCtx rejects non-L1 contexts.
crucible_neg_compile_effects_test(neg_alloc_class_from_unbound_ctx
    "static assertion failed|AllocClassFromCtx|Unbound")
crucible_neg_compile_effects_test(neg_ctx_dispatch_wrong_tier
    "associated constraints are not satisfied|IsL1ResidentCtx")

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
    "associated constraints are not satisfied|IsBridgeableDirection")
crucible_neg_compile_effects_test(neg_endpoint_non_exec_ctx
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|IsExecCtx")
crucible_neg_compile_effects_test(neg_substrate_session_unfit_residency
    "associated constraints are not satisfied|SubstrateFitsCtxResidency|fits_in_tier")
crucible_neg_compile_effects_test(neg_substrate_session_non_bridgeable
    "associated constraints are not satisfied|IsBridgeableDirection")

# ─── Tier 3 commit 1 (#863, HS14): mint_stage gate fixtures ──────────
#
# Two fixtures pin the CtxFitsStage<FnPtr, Ctx> conjunction at the
# mint_stage<auto FnPtr>(ctx, in, out) boundary:
#   (a) FnPtr that is not a PipelineStage (wrong arity here) →
#       PipelineStage<FnPtr> conjunct fires.
#   (b) Ctx that is not an ExecCtx (bare int) →
#       IsExecCtx<Ctx> conjunct fires.
crucible_neg_compile_effects_test(neg_mint_stage_non_pipeline_stage
    "associated constraints are not satisfied|CtxFitsStage|PipelineStage")
crucible_neg_compile_effects_test(neg_mint_stage_non_exec_ctx
    "associated constraints are not satisfied|CtxFitsStage|IsExecCtx")

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
# Three fixtures pin the CtxFitsPipeline<Ctx, Stages...> conjunction
# at the mint_pipeline boundary:
#   (a) pipeline_chain mismatch — adjacent (i, i+1) stages have
#       incompatible payload types (output_i ≠ input_{i+1}).
#   (b) Pack contains a non-Stage element — IsStage<E> fails for
#       some E in Stages...
#   (c) Ctx is not an IsExecCtx — bare int.
crucible_neg_compile_effects_test(neg_mint_pipeline_chain_mismatch
    "associated constraints are not satisfied|CtxFitsPipeline|pipeline_chain")
crucible_neg_compile_effects_test(neg_mint_pipeline_non_stage
    "associated constraints are not satisfied|CtxFitsPipeline|pipeline_chain|IsStage")
crucible_neg_compile_effects_test(neg_mint_pipeline_non_exec_ctx
    "associated constraints are not satisfied|CtxFitsPipeline|IsExecCtx")

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
crucible_neg_compile_test(neg_sealed_refined_into_call
  "has no member named")
crucible_neg_compile_test(neg_graded_concept_void_substrate
  "constraints not satisfied")
crucible_neg_compile_test(neg_graded_concept_no_lattice_type
  "constraints not satisfied")
crucible_neg_compile_test(neg_graded_concept_bare_int
  "constraints not satisfied")

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
crucible_neg_compile_test(neg_fixed_array_zero_sized
  "constraints not satisfied|associated constraints|no matching|no type named")
crucible_neg_compile_test(neg_fixed_array_in_place_wrong_arity
  "no matching function|constraints not satisfied|associated constraints|cannot convert")

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
# iteration publish path, breaking Augur recommendations-engine
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
# legacy path and `neg_computation_cache_in_row_non_row_param` covers
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

# ─── FOUND-G51 ResidencyHeat negative-compile fixtures ──────────────
#
# Pins the ResidencyHeat wrapper's tier-pin discipline AND the
# underlying ResidencyHeatLattice's per-At<T> nested-struct
# identity.  Closes the load-bearing claim-stronger-than-source
# path that would otherwise let a Cold-tier (L3-resident) value
# silently flow into the KernelCache L1 fast-path admission gate,
# breaking the per-call shape budget (~10× cache-miss penalty).
# Type-fences CRUCIBLE.md §L2 KernelCache L1/L2/L3 + §L15 Augur
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
# compatibility.  Today caught only by the Keeper noticing process
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
  "contract|pre.*core|condition.*kMaxCore|core <= kMaxCore|in.*constant.*expression")

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
# [GCC-WRAPPER-TEXT] Production-side overload-set rejection.  Converting
# requires refactoring schema/ckernel registration to use a
# concept-checked dispatch that emits a framework static_assert; out of
# scope for #371.  Accept the GCC-text dependency; revisit if the
# framework gains a routed-error helper for production overload sets.
crucible_neg_compile_test(neg_schema_register_with_sealed_view
  "no matching function for call to.*register_name")
crucible_neg_compile_test(neg_ckernel_register_with_sealed_view
  "no matching function for call to.*register_op")

# [GCC-WRAPPER-TEXT] Same class as schema/ckernel above — production-
# side overload-set rejection in Cipher.  Same accepted-intrinsic
# rationale.
crucible_neg_compile_test(neg_cipher_wrong_tag
  "no matching function for call to.*store")

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

# Empty Select<> / Offer<> at the handle boundary (#364) — type-level
# uses (Gay-Hole 2005 minimum-subtype rule, SessionSubtype.h's tests)
# stay legitimate, but constructing a runnable handle is rejected.
crucible_neg_compile_test(neg_session_empty_select
    "Empty_Choice_Combinator")

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

# ── A8-P2 #146 — Permission<Tag> linearity enforcement ──────────
# Foundational CSL frame-rule token (O'Hearn 2007).  Duplicating a
# Permission<Tag> would create two simultaneous owners of the same
# named region, breaking CSL's frame rule.  Copy ctor is = delete
# with reason mentioning "linear — duplicating creates two
# simultaneous owners".
crucible_neg_compile_test(neg_permission_copy_attempt
    "linear")

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
# auto_split_n / auto_split_grid / split_grid / permission_split_n's
# own static_assert messages, not GCC phrasing.

# 1D (FOUND-A21): N=0 split is structurally meaningless.
crucible_neg_compile_test(neg_auto_split_n_zero
    "N must be greater than zero")

# 2D (FOUND-A22): zero producers / zero consumers in the descriptor.
crucible_neg_compile_test(neg_auto_split_grid_zero_m
    "M .producer count. must be > 0")
crucible_neg_compile_test(neg_auto_split_grid_zero_n
    "N .consumer count. must be > 0")

# 2D (FOUND-A22): the factory call site re-checks independently of
# the type-level descriptor — caller mistakes get their own pinpoint.
crucible_neg_compile_test(neg_split_grid_call_zero
    "M and N must both be greater than zero")

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
crucible_neg_compile_test(neg_grid_producer_try_recv
    "has no member named.*try_recv")

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
#   - Each fixture's assert verified to come from the EXPECTED line
#     in the EXPECTED header (PermissionedSession.h:184 / :319 / :474
#     or PermSet.h:248).  No fixture accidentally fires from a
#     different framework site.
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
