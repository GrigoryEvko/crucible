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
endfunction()

crucible_test(test_safety)
crucible_test(test_simd)
crucible_test(test_types)
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
crucible_test(test_atomic_snapshot)
crucible_test(test_chase_lev_deque)
crucible_test(test_mpsc_ring)
crucible_test(test_storage_nbytes_simd)
crucible_test(test_trace_ring_pop_batch)
crucible_test(test_expr_pool_fast_symbol)
crucible_test(test_sharded_grid)
crucible_test(test_queue_facade)
crucible_test(test_permission_shared)
crucible_test(test_owned_region)
crucible_test(test_read_view)
crucible_test(test_topology)
crucible_test(test_parallelism_cost_model)
crucible_test(test_permissioned_snapshot)
crucible_test(test_concurrency_collision_fuzzer)
crucible_test(test_region_cache)
crucible_test(test_numerical_recipe)
crucible_test(test_recipe_pool)
crucible_test(test_recipe_registry)
crucible_test(test_recipe_integration)
crucible_test(test_recipe_coverage)

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
