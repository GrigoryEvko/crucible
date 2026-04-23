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

# Tier 1 compiler firings — each has a distinct diagnostic.
crucible_neg_compile_test(neg_assignment
  "ScopedView is single-binding; assignment hides state transitions")
crucible_neg_compile_test(neg_direct_construct
  "is private within this context")
crucible_neg_compile_test(neg_default_construct
  "no matching function for call to.*ScopedView.*::ScopedView")
crucible_neg_compile_test(neg_heap_allocate
  "ScopedView must live on the stack; heap allocation defeats")

# State-discipline: SealedView cannot satisfy the MutableView parameter
# of register_name / register_op — the only typed registration path
# requires proof that the table is in the Mutable phase.
crucible_neg_compile_test(neg_schema_register_with_sealed_view
  "no matching function for call to.*register_name")
crucible_neg_compile_test(neg_ckernel_register_with_sealed_view
  "no matching function for call to.*register_op")

# Cross-carrier discipline: Cipher::store(OpenView, ...) must reject a
# ScopedView parametrized on a different Carrier+Tag (SchemaTable's
# SealedView).  Proves the type-state lock is per-carrier.
crucible_neg_compile_test(neg_cipher_wrong_tag
  "no matching function for call to.*store")
