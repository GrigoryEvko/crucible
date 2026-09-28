# ═══════════════════════════════════════════════════════════════
# Test targets — inherit 200+ warning/hardening flags from the
# crucible INTERFACE library, plus sanitizers below.
# ═══════════════════════════════════════════════════════════════

# ═══════════════════════════════════════════════════════════════
# SECTION 8: Sanitizers
#
# The root CMakeLists.txt declares CRUCIBLE_SANITIZE, CRUCIBLE_TSAN and
# CRUCIBLE_UBSAN_STRICT, and it puts ASan, ThreadSanitizer or the strict
# UBSan set on every target through crucible_dialect.  The factories below
# add UBSan to the test executables of the default preset, and nothing more.
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
    -fsanitize=undefined,bounds-strict
    -fno-sanitize-recover=all         # abort on first violation
  )
  set(CRUCIBLE_SAN_LINK_FLAGS
    -fsanitize=undefined,bounds-strict
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
  # libstdc++'s std::string/allocator max_size() defensive branch (PTRDIFF_MAX)
  # constant-folds into operator new at -O1+ (release/bench), tripping the global
  # -Werror=alloc-size-larger-than on any test TU that uses std::string. It is a
  # library-internal branch that never executes. Suppress on the test sources in
  # Release config only — source-file COMPILE_OPTIONS sort after the inherited
  # -Walloc-size-larger-than, so this wins.
  set_source_files_properties(${CRUCIBLE_TEST_SOURCES} PROPERTIES
    COMPILE_OPTIONS "$<$<CONFIG:Release>:-Wno-alloc-size-larger-than>")
  # Test binaries must keep their assertions in Release too.  CMake's
  # Release defaults inject -DNDEBUG, and some standard headers may
  # redefine assert() after test_assert.h in a TU.  Force the test TU
  # itself back into assertion-enabled mode while linked libraries
  # still use the selected build type.
  target_compile_options(${name} PRIVATE -UNDEBUG)

  # UBSan on the test executable.  ASan and ThreadSanitizer come from
  # crucible_dialect on every target.
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

# A regression test for the constant-evaluator defect that the patch
# series in toolchain/gcc fixes: memchr, strchr, strrchr and strstr from
# an offset pointer.  A compiler without the fix cannot compile it.
crucible_test(test_gcc_memchr_fold)

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
crucible_test(test_fixy_perm)
crucible_test(test_fixy_handle)  # FIXY-U-016 — per-namespace test_fixy_handle sentinel TU
# The chain concepts and the two mints of the old pipeline headers.
crucible_test(test_fixy_pipe_chain)

# The cache-size constants of concurrent/_TopologyConstexpr.h.  This build
# defines no CRUCIBLE_L*_BYTES macro, so each constant must equal its
# conservative floor.
crucible_test(test_fixy_pipe_topology_constexpr)

# The same constants with the three CRUCIBLE_L*_BYTES macros defined on
# the command line.  Each constant must equal its macro.
crucible_test(test_fixy_pipe_topology_override)
target_compile_definitions(test_fixy_pipe_topology_override PRIVATE
    CRUCIBLE_L1D_PER_CORE_BYTES=65536      # 64 KiB
    CRUCIBLE_L2_PER_CORE_BYTES=1048576     #  1 MiB
    CRUCIBLE_L3_TOTAL_BYTES=33554432       # 32 MiB
)
crucible_test(test_fixy_mach_transitions)  # FIXY-AUDIT-C1
crucible_test(test_fixy_wrap)
crucible_test(test_fixy_struct)
crucible_test(test_fixy_is)

# ── FIXY-DIAG / SOURCE — Phase D surfaces ─────────────────────────
#
# Diag: Category + Catalog + tag_of_t / category_of_v bijection +
#   28 tag classes + stable_name / type_id / function_id +
#   canonicalize_pack + insight_provider + row_hash_contribution.
# Source: source::* / trust::* / access::* / version::* /
#   vessel_trust::* / secret_policy::* / hash_family::* tag
#   namespaces — pure phantom-type aliases.
crucible_test(test_fixy_diag)
crucible_test(test_fixy_diag_catalog)  # FIXY-AUDIT-C8 reconciliation
crucible_test(test_fixy_insights)      # fixy-H-14 insight_provider sentinel
crucible_test(test_fixy_a3_007_insights)  # fixy-A3-007 substrate-tag insight closure
crucible_test(test_fixy_a3_008_synchronization)  # fixy-A3-008 Wait + MemOrder → Synchronization
crucible_test(test_fixy_a3_009_regime)  # fixy-A3-009 HotPath → Regime (Progress stays on Complexity)
crucible_test(test_fixy_v_088_fp_mode)  # FIXY-V-088 DimensionAxis::FpMode + 11 sub-axis enum scaffolding
crucible_test(test_fixy_v_089_fp_lattices)  # FIXY-V-089 11 FP-mode ChainLattice algebras + umbrella registration
crucible_test(test_fixy_v_090_fp_wrappers)  # FIXY-V-090 11 safety/FpMode.h wrappers + composite + row_hash + DimensionTraits
crucible_test(test_fixy_v_091_collision_fp_mode)  # FIXY-V-091 CollisionCatalog F101-F105 — FP-mode cross-axis rules
crucible_test(test_fixy_v_093_fp_canonicalize)  # FIXY-V-093 fixy::fp::canonicalize — merkle-hash-safe FP canonicalization
crucible_test(test_fp_strict_floor)  # the crucible_fp_strict floor reaches a crucible consumer: compile definition and runtime witness
crucible_test(test_philox_box_muller_polynomial)  # Philox::box_muller_polynomial_det runtime witness and tier
crucible_test(test_fixy_v_096_welford_variance)  # FIXY-V-096 bench_harness Welford-on-double variance — type-shape + ill-conditioned stability witness
target_include_directories(test_fixy_v_096_welford_variance SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/bench)
crucible_test(test_fixy_v_097_syscall_family)  # FIXY-V-097 DimensionAxis::SyscallSurface + SyscallFamilyLattice 9-tier chain
crucible_test(test_fixy_v_239_control_flow)    # FIXY-V-239 DimensionAxis::ControlFlow + ControlFlowLattice 5-tier escape chain
crucible_test(test_fixy_v_240_call_shape)      # FIXY-V-240 DimensionAxis::CallShape + CallShapeLattice 5-tier dispatch chain
crucible_test(test_fixy_v_241_stack_global_stdio) # FIXY-V-241 StackUse/GlobalState/Stdio — 3 hazard-axis 4-tier chains
crucible_test(test_fixy_v_242_hazard_wrappers)    # FIXY-V-242 5 safety/* Graded carriers (ControlFlow/CallShape/StackUse/GlobalState/Stdio)
crucible_test(test_fixy_v_243_control_flow_collisions)  # FIXY-V-243 CollisionCatalog C001/D001/D002/G001/L006/P003/S001/S004 hazard-axis cross-axis rules
crucible_test(test_fixy_v_260_hw_collisions)  # FIXY-V-260 CollisionCatalog V001/V002/V101/V102/V201/V202/V203/V301 hardware-axis cross-axis rules
crucible_test(test_fixy_v_249_thread_local_atomic)      # FIXY-V-249 CollisionCatalog G002 thread_local × atomic (Scenario E)
crucible_test(test_fixy_v_250_simd_isa)                 # FIXY-V-250 SimdIsaLattice — non-distributive x86×ARM ISA partial order
crucible_test(test_fixy_v_251_hw_instruction)           # FIXY-V-251 HwInstructionLattice — 5-tier hw-instruction capability chain
crucible_test(test_fixy_v_252_barrier_strength)         # FIXY-V-252 BarrierStrengthLattice — 7-tier memory-fence strength chain
crucible_test(test_fixy_v_181_suspend_behavior)         # FIXY-V-181 SuspendBehaviorLattice — 3-element clock-pause-on-suspend chain (Agent 6 clock-axis foundation)
crucible_test(test_fixy_v_182_pinning_requirement)      # FIXY-V-182 PinningRequirementLattice — 4-element CPU-coherence-domain chain (rdtsc-affinity, Agent 6 clock-axis foundation)
crucible_test(test_fixy_v_183_scheduler_policy)         # FIXY-V-183 SchedulerPolicyLattice — 6-element Linux scheduler-class preemption chain (Agent 6 sched-axis foundation)
crucible_test(test_fixy_v_184_clock_source)             # FIXY-V-184 ClockSourceLattice — DetSafe×Suspend×Pinning product composite + 9-source projection (Agent 6 §3.1)
crucible_test(test_fixy_v_185_clock_source)             # FIXY-V-185 safety/ClockSource.h — regime-1 Graded provenance carrier + per-source aliases + IsClockSource + row_hash (Agent 6 §3.3)
crucible_test(test_fixy_v_186_sched_class)              # FIXY-V-186 safety/SchedClass.h — regime-1 Graded scheduler-class carrier + pool-fit + DEADLINE CBS budget + IsSchedClass + row_hash (Agent 6 §3.3)
crucible_test(test_fixy_v_187_cpu_pinned)               # FIXY-V-187 safety/CpuPinned.h — move-only AffinityMask×PinningPosture pin proof + singleton/posture gates + IsCpuPinned + row_hash 0x32 (Agent 6 §3.3)
crucible_test(test_fixy_v_188_suspend_behavior)         # FIXY-V-188 safety/SuspendBehavior.h — regime-1 pause-on-suspend witness + KeepsTicking watchdog gate + IsSuspendBehavior + row_hash 0x33 (Agent 6 §3.3)
crucible_test(test_fixy_v_189_thread_name)              # FIXY-V-189 safety/ThreadName.h — TASK_COMM_LEN-bounded literal + ThreadNamed witness + Init-row mint_thread_name (Agent 6 §3.3 item 6)
crucible_test(test_fixy_v_265_memory_scope)             # FIXY-V-265 MemoryScopeLattice — non-distributive accel×ARM visibility-scope partial order (WMEM keystone)
crucible_test(test_fixy_v_267_scoped_fence)             # FIXY-V-267 safety/ScopedFence.h — partial-order provider Graded carrier for MemoryScope axis + IsScopedFence + row_hash
crucible_test(test_fixy_v_268_scope_collisions)         # FIXY-V-268 CollisionCatalog V401 (scope⊒Gpu × strength⊏AcqRel) + V402 (scope-trunk × host-arch) dual-wired
crucible_test(test_fixy_v_190_time)                     # FIXY-V-190 fixy/Time.h — grant::time::{clock_read,tsc_read,sleep} + mint_clock_reader / mint_tsc_reader (single-core pin) / mint_bounded_sleep (Agent 6 §3.4)
crucible_test(test_fixy_v_191_sched)                    # FIXY-V-191 fixy/Sched.h — grant::sched::{affinity,scheduler_policy,priority,thread_name} + mint_affinity (→CpuPinned) / mint_scheduler_policy (→SchedClass, CBS) / mint_priority (Agent 6 §3.5)
crucible_test(test_fixy_v_272_fence)                    # FIXY-V-272 mimic/Fence.h — lower_fence<Strength,Scope,Arch>() consteval lowering table (V401/V402 gated) + fence_mnemonic
crucible_test(test_fixy_v_273_async_pipeline_session)   # FIXY-V-273 sessions/AsyncPipelineSession.h — binary producer/consumer mbarrier protocol + Linear<SmemSlot> + 2 ctx-bound mints
crucible_test(test_fixy_v_203_spawn_join_policy)        # FIXY-V-203 fixy/spawn/JoinPolicy.h — spawn-MECHANISM phantom tag tree (AutoJoin/ManualJoin/Detached/Cloned/Forked/PosixSpawn) + IsJoinMechanismTag identity allowlist (Agent 7 §fixy::spawn::; orthogonal to V-078 strictness lattice)
crucible_test(test_hardening_syscall_atoms) # warden/Hardening.h: the syscall atoms of the apply path, their ordinals and the rows they lift to
crucible_test(test_perf_syscall_atoms) # perf/*.h: the gate row of each of the seven hub mints covers the rows of the calls its load makes
crucible_test(test_fixy_v_004_wrapper_for)              # FIXY-V-004 safety/DimensionTraits.h + fixy/Dim.h — WrapperKind enum (33 entries, append-only) + wrapper_kind_to_axis switch + count_wrappers_on_axis + wrapper_for<DimensionAxis>() reverse-lookup metafunction (one-to-many inverse of wrapper_dimension<W>::value, reflection-driven via template for over enumerators_of)
crucible_test(test_fixy_v_254_hw_wrapper)               # FIXY-V-254 safety/Hw.h — Graded<Absolute,HwInstructionLattice::At<Tier>,P> regime-1 wrapper + IsHw + row_hash
crucible_test(test_fixy_v_255_barrier_guarded)          # FIXY-V-255 safety/BarrierGuarded.h — floor-semantics Graded carrier + IsBarrierGuarded + row_hash + Tagged composition
crucible_test(test_fixy_v_256_simd_width_pinned)        # FIXY-V-256 safety/SimdWidthPinned.h — partial-order provider Graded carrier + IsSimdWidthPinned + row_hash
crucible_test(test_fixy_v_257_hw_grants)                # FIXY-V-257 fixy/Hw.h — 10 grant::hw::* families (cache/barrier/tsc/rng/cpuid/msr/port_io/asm_/simd_width/vendor_intrinsic) + 5 §XXI mints
crucible_test(test_fixy_v_261_arch_pinned)              # FIXY-V-261 safety/source/_Arch.h — source::ArchPinned<Arch> tag + ArchComposable cross-arch gate + Portable→concrete retag admittance
crucible_test(test_hardware_axis_pins)  # the hot headers pin their compile-time hardware constructs as fixy atoms
crucible_test(test_fixy_v_244_ctrl_grants)  # FIXY-V-244 fixy/grant/Ctrl.h — 6 grant::ctrl::* grants + V-087 throws re-home
crucible_test(test_fixy_v_234_release_aware_permission)  # FIXY-V-234 advise_release_aware<DontNeed, RegionTag> + SharedPermission composition — Agent 9 Bug 5 HIGH
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

# ── The zero-cost sentinel for CRUCIBLE_PRE under the ignore semantic ─
#
# Special test target: built with `-O3` and
# CRUCIBLE_CONTRACT_IGNORE_OPTIONS instead of the preset's `-Og -g`.
# Only the ignore arm of CRUCIBLE_PRE claims zero cost, and the define
# in the list selects that arm.
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
target_compile_options(test_pre_post_cost PRIVATE ${CRUCIBLE_CONTRACT_IGNORE_OPTIONS})
add_test(NAME test_pre_post_cost COMMAND test_pre_post_cost)
crucible_dump_asm(test_pre_post_cost)

crucible_test(test_types)
crucible_test(test_kernel_cache_key)
crucible_test(test_row_hash_fold)
crucible_test(test_row_hash_contribution_sentinel)
crucible_test(test_row_hash_distinctness)
# An old-tree row hash is a function of the type alone, so two
# translation units of one build compute one hash for one type.  The
# second unit declares generic closures before its includes.
crucible_test(test_row_hash_translation_units_safety
  test_row_hash_translation_units_safety.cpp
  test_row_hash_translation_units_safety_shifted.cpp)
# fix-14 — SchedClass SCHED_DEADLINE budget NTTPs were folded with a
# collidable shift-XOR pre-mix; now per-field combine_ids.  Witnesses an
# old-collision triple pair now produces distinct row_hash.
crucible_test(test_row_hash_sched_class_collision)
crucible_test(test_canonical_order)              # FIXY-FOUND-048
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
# test_cntp_fec and test_cntp_fountain instantiate the `#if __AVX2__` kernels of
# cntp/Fec.h, which call std::start_lifetime_as<const __m256i>.  The writable-asm
# barrier of libstdc++ 16 rejects that call under -march=native (release/bench).
# CRUCIBLE_FEC_CONST_SIMD_OK, probed in the top-level CMakeLists, is TRUE on
# default/tsan/analyzer, where the AVX2 arm is compiled out and the scalar path runs.
# It is FALSE under release/bench until cntp/Fec.h adopts a const-safe load.
if(CRUCIBLE_FEC_CONST_SIMD_OK)
crucible_test(test_cntp_fec)
crucible_test(test_cntp_fountain)
endif()
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
# The DetSafe gate CLAUDE.md §XIII names first.  Labelled so it can be run
# on its own against a build whose codegen changed — a profile-guided build
# is the case that motivated it — and so that the reason a red line matters
# is attached to the test rather than remembered.  Exit codes, no assertion
# macro, for the same reason test_serialize_release_gate uses them.
crucible_test(test_bit_exact_replay_invariant)
set_tests_properties(test_bit_exact_replay_invariant PROPERTIES
  LABELS "determinism;axiom-detsafe")
# The two serialize tests carry the checkpoint_format_stability gate of
# CLAUDE.md §XIII.  Measured 2026-09-20: no TrainingCheckpoint type exists
# in the code, so the DAG wire format that serialize_region writes is the
# only checkpoint format there is.  test_serialize pins the byte count and
# a golden hash in source, which is the cross-process leg of the gate, and
# test_serialize_release_gate pins the field offsets under NDEBUG.  The
# cross-backend leg waits behind the same wall as cross_vendor_step_invariant:
# a second backend.
crucible_test(test_serialize)
# fix-11 — release-mode (NDEBUG) witness: malformed wire bytes are rejected
# by Serialize.h's hard read_gated branch even when the Refined ctor pre()
# collapses to [[assume]].  Uses exit codes, not assert() (a no-op under NDEBUG).
crucible_test(test_serialize_release_gate)
set_tests_properties(test_serialize test_serialize_release_gate PROPERTIES
  LABELS "determinism;axiom-detsafe")
crucible_test(test_cipher)
crucible_test(test_transaction)
# The executable has its own clock_gettime, so a case can make a clock read fail.
crucible_test(test_clock_failed_read)
crucible_test(test_permissioned_moved_from_handles)
crucible_test(test_transaction_owner)
crucible_test(test_vigil)
crucible_test(test_ckernel)
# Each process-wide object, written in one shared library and read in
# another.  The test loads the two libraries with dlopen and RTLD_LOCAL, as
# crucible_native.py loads the two vessel libraries.
foreach(_process_wide_library process_wide_writer process_wide_reader)
  add_library(test_${_process_wide_library} SHARED ${_process_wide_library}.cpp process_wide_objects.cpp)
  target_link_libraries(test_${_process_wide_library} PRIVATE crucible)
endforeach()
crucible_test(test_process_wide_across_libraries)
add_dependencies(test_process_wide_across_libraries test_process_wide_writer test_process_wide_reader)
target_compile_definitions(test_process_wide_across_libraries PRIVATE
  PROCESS_WIDE_WRITER_PATH="$<TARGET_FILE:test_process_wide_writer>"
  PROCESS_WIDE_READER_PATH="$<TARGET_FILE:test_process_wide_reader>")
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
crucible_test(test_kernel_cache_levels)
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
# The two properties -fno-exceptions and -fno-rtti stood for, held on the
# artifact of THIS build rather than asserted by flags neither preset has
# ever carried.  The self-test needs the project compiler, because the
# system g++ may be too old for the fixtures.
add_test(NAME no_throw_no_rtti_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-throw-no-rtti.sh --self-test)
set_tests_properties(no_throw_no_rtti_self_test PROPERTIES
  ENVIRONMENT "CRUCIBLE_CXX=${CMAKE_CXX_COMPILER}"
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
add_test(NAME no_throw_no_rtti
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-no-throw-no-rtti.sh
          $<TARGET_FILE:crucible> $<TARGET_FILE:foundation>)
set_tests_properties(no_throw_no_rtti PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The gate reads the output of the optimizer.  In C++26, each libstdc++
# allocate holds an inline `throw bad_alloc()`, after a length check that
# makes the throw unreachable.  An optimized build finds that and deletes
# the throw.  The strict UBSan checks stop that proof, and the throw stays
# in the artifact.  The release, default, tsan and verify presets run the
# gate.
if(CRUCIBLE_UBSAN_STRICT)
  set_tests_properties(no_throw_no_rtti PROPERTIES DISABLED TRUE)
endif()

# Every configure refuses a compiler that does not have each fix in
# toolchain/gcc/patches.  This test runs the probes on the compiler of the
# build, which must pass, and on stub compilers, which must each get the
# result that their behaviour calls for.  It resolves a ccache link to the
# compiler that ccache runs, and it configures the tree with a compiler that
# lacks a fix, which must stop.
add_test(NAME patched_gcc_probe_self_test
  COMMAND ${CMAKE_COMMAND} -DCXX=${CRUCIBLE_REAL_CXX}
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/patched_gcc_probe_self_test
          -DSOURCE=${CMAKE_SOURCE_DIR}
          -P ${CMAKE_SOURCE_DIR}/cmake/probes/self_test.cmake)
set_tests_properties(patched_gcc_probe_self_test PROPERTIES LABELS "ci_guard")

# Every site that claims a lifetime bound carries the deleted overload
# that enforces it.  CRUCIBLE_LIFETIMEBOUND expands to nothing on the
# only compiler this tree builds with, so the claim is checked here
# rather than by an attribute no compiler reads.
add_test(NAME lifetime_twin_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-lifetime-twin.py --self-test)
add_test(NAME lifetime_twin
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-lifetime-twin.py --quiet)
set_tests_properties(lifetime_twin lifetime_twin_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)

# A substrate trait or a fail-closed relation is specialized only beside
# the declarations it names.  scripts/check-trait-injection.py reads each
# specialization and each reopening of a relation namespace from the parse
# tree.  Exit 3 when the kit is absent reports a skip.
add_test(NAME trait_guard_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-trait-injection.py
          --self-test)
add_test(NAME trait_guard
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-trait-injection.py)
set_tests_properties(trait_guard trait_guard_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)

# The coarse, tree-wide net for the escape surface: every public
# function that hands out a raw reference or pointer is a sanctioned
# accessor, mint, discouraged hatch, or a ledgered exception.  The
# unbreakable member-function half is the compiled reflection probe
# test_escape_reflection; this script covers the free-function residue
# reflection cannot walk.
add_test(NAME escape_doors_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-escape-doors.py --self-test)
add_test(NAME escape_doors
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-escape-doors.py)
set_tests_properties(escape_doors escape_doors_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The contract cites of include/ and src/, counted from the parse tree.
# --check compares the live counts with the committed baseline.  A counter
# that rises passes, and a counter that falls fails the test.  After a
# deliberate removal, write the baseline again with
# `python3 scripts/audit-pre-callsite-count.py --baseline
# scripts/contracts-baseline.json` and give the reason in the commit.
add_test(NAME contract_cite_regression_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/audit-pre-callsite-count.py
          --self-test)
add_test(NAME contract_cite_regression
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/audit-pre-callsite-count.py
          --check ${CMAKE_SOURCE_DIR}/scripts/contracts-baseline.json)
set_tests_properties(contract_cite_regression contract_cite_regression_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# The cite-ratio policy of the decide:: catalog.  Each procedure needs at
# least 2 production cites within 180 days of the day its definition
# entered Decide.h.  The day comes from the parse of every committed
# version of Decide.h, so a comment that names a procedure dates nothing.
# A procedure inside the window is informational.  A procedure past it
# with too few cites fails the test.
add_test(NAME contract_decide_cite_ratio_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/audit-decide-cite-ratio.py
          --self-test)
add_test(NAME contract_decide_cite_ratio
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/audit-decide-cite-ratio.py)
set_tests_properties(contract_decide_cite_ratio contract_decide_cite_ratio_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# A Refined or SealedRefined parameter carries the proof of its predicate.
# A precondition in the same function that cites the decide:: procedure of
# that predicate on that parameter re-tests a proved fact, and the guard
# refuses it.  The guard reads parameters, aliases, CRUCIBLE_PRE calls and
# P2900 pre clauses from the parse tree.
add_test(NAME refined_pre_subsumption_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-refined-pre-subsumption.py
          --self-test)
add_test(NAME refined_pre_subsumption
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-refined-pre-subsumption.py)
set_tests_properties(refined_pre_subsumption refined_pre_subsumption_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# The fixy discipline.  scripts/check-fixy-discipline.py reads the parse tree
# of each file under the directories of scripts/fixy-only-paths.txt, and fails
# when one names crucible::safety::fn::Fn or a Tier-1 crucible::safety wrapper
# by any route: a qualified name, a namespace alias, a using-declaration or a
# using-directive.  Exit 3 when the kit is absent reports a skip.
add_test(NAME fixy_discipline_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-discipline.py --self-test)
add_test(NAME fixy_discipline
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-discipline.py)
set_tests_properties(fixy_discipline fixy_discipline_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# FIXY-U-003 §XXI Universal Mint Pattern compliance scanner.  Today's
# Phase 1 check: every mint_<noun>( factory definition MUST carry the
# [[nodiscard]] attribute on the same line or the immediately
# preceding line.  Phase 2 (deferred) will extend to constexpr /
# noexcept / requires-clause checks.  Self-test mode plants both a
# canonical mint and a drift case to guard against regex regression.
# The gate reads each axis off the parse tree rather than off a line window.
# The window it replaced failed open: it decided whether the requires axis
# applied by walking fifteen lines up for the word `template`, and in
# FederationPermission.h the parameter list sits seventeen lines above the
# signature, so a templated mint with no clause passed unreported.
add_test(NAME mint_pattern_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-mint-pattern.py
          --self-test)
add_test(NAME mint_pattern
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-mint-pattern.py)
set_tests_properties(mint_pattern mint_pattern_self_test PROPERTIES
  LABELS "ci_guard;ast")
set_tests_properties(mint_pattern mint_pattern_self_test PROPERTIES SKIP_RETURN_CODE 3)
# The witness roster.  Every type that attests to a fact it cannot see
# — a pin, a mapping, a descriptor, a permission, a context — is named
# in scripts/witness-roster.txt with the expression a forger would
# write, and test/fixy/CMakeLists.txt stands one generated fixture on
# each closed line.  --check walks include/fixy and include/foundation
# and fails when a class showing a door shape — a mint_ friend, a
# passkey, a static factory, a named friend or a friend type beside a
# closed constructor — is absent from the roster, and fails when a
# fixture on disk is not what its roster line produces.  The walk reads
# the parse tree.  The self-test compiles a planted public-constructor
# witness with this build's flags and must see it COMPILE, which is the
# red the guard exists to raise.  Exit 3 when the kit is absent reports a
# skip.
add_test(NAME witness_roster_self_test
  COMMAND ${CMAKE_COMMAND} -E env WITNESS_BUILD_DIR=${CMAKE_BINARY_DIR}
          python3 ${CMAKE_SOURCE_DIR}/scripts/check-witness-roster.py --self-test)
add_test(NAME witness_roster
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-witness-roster.py --check)
set_tests_properties(witness_roster witness_roster_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# A Permission held as a class member is the [[no_unique_address]]
# member of a handle (CLAUDE.md §IX).  Without the mark, the holder can
# be aliased across threads and the type system does not see it.
# scripts/check-permission-storage.py reads each member from the parse
# tree, so an initializer, two declarators, a member over two lines, an
# array and an alias do not hide one.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME permission_storage_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-permission-storage.py
          --self-test)
add_test(NAME permission_storage
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-permission-storage.py)
set_tests_properties(permission_storage permission_storage_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# Each object with static storage duration is one per process, through
# CRUCIBLE_PROCESS_WIDE, or scripts/process-wide-roster.txt says why one copy
# in each shared library is correct.  The build hides symbols, so an inline
# object that two shared libraries compile has two copies without the marker.
# scripts/check-process-wide-state.py reads each object from the parse tree.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME process_wide_state_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-process-wide-state.py --self-test)
add_test(NAME process_wide_state
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-process-wide-state.py)
set_tests_properties(process_wide_state process_wide_state_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The reinterpret_cast ban (CLAUDE.md §III), the std::vector::reserve ban
# (CLAUDE.md §IV), the file-stream ban of the fixy-only band-3 directories
# and the process-spawn ban of production code (CLAUDE.md §IX), read from
# the parse tree of the pinned kit.  Each ban has a content-keyed allowlist:
# scripts/no-reinterpret-allowlist.txt, scripts/no-reserve-allowlist.txt,
# scripts/no-file-stream-allowlist.txt and
# scripts/no-spawn-process-allowlist.txt.  The self-test plants each shape
# that a line regex misses or misreads.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME banned_calls_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-banned-calls.py
          --self-test)
add_test(NAME banned_calls
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-banned-calls.py)
set_tests_properties(banned_calls banned_calls_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The hardware-axis gate has two halves.  test_hardware_axis_pins.cpp holds
# one row for each block, and the compiler walks the block by reflection:
# the row fails when an axis of the row has no atom alias, or an atom alias
# has an axis that the row does not list.  scripts/check-fixy-hw-discipline.py
# reads the rows from the parse tree and requires a row for every `*_hw`
# namespace under include/ and src/, with its block outside every
# preprocessor conditional.  Exit 3 when the kit is absent reports a skip.
add_test(NAME fixy_hw_discipline_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-hw-discipline.py
          --self-test)
add_test(NAME fixy_hw_discipline
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-hw-discipline.py)
set_tests_properties(fixy_hw_discipline fixy_hw_discipline_self_test PROPERTIES
    LABELS "ci_guard;ast"
    SKIP_RETURN_CODE 3)
# The hardening grant set.  scripts/check-syscall-grant-coverage.py
# derives the system calls that the hardening apply path issues from the
# parse tree, and fails when that set and the declared
# hardening_syscall_atoms tuple disagree.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME syscall_grant_coverage_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-syscall-grant-coverage.py --self-test)
add_test(NAME syscall_grant_coverage
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-syscall-grant-coverage.py)
set_tests_properties(syscall_grant_coverage syscall_grant_coverage_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# A script that writes to a git repository names it and clears the
# caller's repository variables.  scripts/check-git-isolation.py refuses a
# script under scripts/, tools/ or toolchain/ that runs git init, commit,
# update-ref, reset, checkout, fetch or apply without
# scripts/throwaway_repo.py, or, in shell, without the unset and the
# --absolute-git-dir check.  It reads shell with the pinned ast-grep, and
# exit 3 when that binary is absent reports a skip.
add_test(NAME git_isolation_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-git-isolation.py --self-test)
add_test(NAME git_isolation
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-git-isolation.py)
set_tests_properties(git_isolation git_isolation_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# No script reads C++ source as text.  scripts/guard-engines.txt gives the
# engine of each script under scripts/, test/, tools/mutation/ and the
# session oracle emitter, and scripts/check-guard-engines.py holds each row
# to the code: no regex or string method on the text of a tsast node, no
# grep, rg, awk or sed over C++ in a shell script, and no cxx_lex.  It reads
# shell with the pinned ast-grep, and exit 3 when that binary is absent
# reports a skip.
add_test(NAME guard_engines_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-guard-engines.py --self-test)
add_test(NAME guard_engines
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-guard-engines.py)
set_tests_properties(guard_engines guard_engines_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# A spelling of a branded type that names no brand is the erased
# identity.  scripts/check-brand-drain.py reads those spellings from the
# parse tree, and scripts/brand-drain.txt holds one count per file.  A
# count can only decrease.  Exit 3 when the kit is absent reports a skip.
add_test(NAME brand_drain_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-brand-drain.py
          --self-test)
add_test(NAME brand_drain
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-brand-drain.py)
set_tests_properties(brand_drain brand_drain_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# std::start_lifetime_as over an array of proofs, over an aggregate that
# holds one, and std::start_lifetime_as_array over proofs each give a
# pointer to a proof whose lifetime never started, and no property of a
# type refuses them.  scripts/check-start-lifetime.py refuses each use of
# the two library names outside a reviewed and counted list.  The guard
# reads the compile database of this build, so a generated source and a
# source with an unusual suffix are in scope.  The self-test plants each
# route and each known bypass.  Exit 3 (the pinned tree-sitter kit is
# absent) is a skip.
add_test(NAME start_lifetime_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-start-lifetime.py --self-test)
add_test(NAME start_lifetime
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-start-lifetime.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(start_lifetime start_lifetime_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# A union, a cast of a void pointer, an allocator, an explicit
# instantiation and a specialization of a door each build a proof type
# with no call to its door, and the language refuses none of them.
# scripts/check-proof-routes.py refuses each one outside a reviewed and
# counted list.  It reads the names of the proof types from the witness
# roster and from the reflection walk of test_forgeable_proofs, and it
# reads the compile database, so a route that a macro forms is seen too.
add_test(NAME proof_routes_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-proof-routes.py --self-test)
add_test(NAME proof_routes
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-proof-routes.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json
          --proof-names-binary $<TARGET_FILE:test_forgeable_proofs>)
set_tests_properties(proof_routes proof_routes_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# scripts/preprocessed.py preprocesses each unit of the compile database
# one time for check-start-lifetime.py and check-proof-routes.py together.
# The self-test proves that a warm run reads the store, that a changed
# header makes its units stale, and that two runs at one time share work.
add_test(NAME preprocessed_store_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/preprocessed.py --self-test)
set_tests_properties(preprocessed_store_self_test PROPERTIES
    LABELS "ci_guard")
# A linear token is an empty class, so a moved-from token looks live, and
# a second std::move of one Permission gives two owners of one region.
# sizeof(Permission) stays 1, so no byte records the move.
# scripts/check-use-after-move.py walks each control-flow path of each
# function body and refuses a read of a name after a move spends it, unless
# an assignment or a reset builds the value again first.  The reviewed and
# counted exceptions are in scripts/use-after-move-allowlist.txt.  The
# self-test plants the uses that the guard must catch and the correct code
# that it must accept.
add_test(NAME use_after_move_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-use-after-move.py --self-test)
add_test(NAME use_after_move
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-use-after-move.py)
set_tests_properties(use_after_move use_after_move_self_test PROPERTIES
    LABELS "ci_guard")
# The uses after move that the guard does not read, one function each, on
# a shrink-only ledger.  A shape the guard learns turns this test red, and
# the tree-wide guard red with it, until the row leaves the ledger.
add_test(NAME use_after_move_attacks
  COMMAND python3 ${CMAKE_SOURCE_DIR}/test/guard_attacks/test_use_after_move_attacks.py ${CMAKE_SOURCE_DIR})
set_tests_properties(use_after_move_attacks PROPERTIES
    LABELS "ci_guard")
# A splice of a private data member has no access check, so
# access_context::unchecked() is the one door to a proof's private state.
# scripts/check-no-unchecked-access.py admits it only in the files that
# scripts/unchecked-access-allowlist.txt names, each with its reason.  A
# bare splice of a static data member has no door token, so the guard also
# refuses a writable private or protected static data member.  It reads the
# macro expansions of this build too, so a door that ## builds and a member
# that a macro declares are seen.  Exit 3 (the pinned tree-sitter kit is
# absent) is a skip.
add_test(NAME no_unchecked_access_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-unchecked-access.py --self-test)
add_test(NAME no_unchecked_access
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-unchecked-access.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(no_unchecked_access no_unchecked_access_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A function in a detail namespace does the work of a door with no gate of
# its own.  scripts/check-detail-namespace.py refuses a use of a detail
# namespace of foundation or fixy outside the two layers.  The reviewed uses
# in tests are in scripts/detail-namespace-allowlist.txt, keyed by file and
# namespace.  Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME detail_namespace_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-detail-namespace.py --self-test)
add_test(NAME detail_namespace
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-detail-namespace.py)
set_tests_properties(detail_namespace detail_namespace_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A class of the namespace foundation::effects::host is a door to a context
# key, because its first definition in any translation unit is legal C++.
# scripts/check-host-owners.py refuses a definition of one outside the file
# that scripts/host-owner-roster.txt names.  Exit 3 (the pinned tree-sitter
# kit is absent) is a skip.
add_test(NAME host_owners_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-host-owners.py --self-test)
add_test(NAME host_owners
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-host-owners.py)
set_tests_properties(host_owners host_owners_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# host::InitOwner::mint_init_context() is the one production door of the
# init context, and host::BackgroundOwner::mint_background_context() is the
# one production door of the background context.  Each context permits
# effects that the hot path must not have.  scripts/check-ctx-init-door.py
# rejects a use of either owner in production code outside the scopes of its
# allowlist (scripts/ctx-init-door-allowlist.txt, scripts/ctx-bg-door-allowlist.txt),
# and a stale row.  Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME ctx_init_door_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ctx-init-door.py --self-test)
add_test(NAME ctx_init_door
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ctx-init-door.py)
set_tests_properties(ctx_init_door ctx_init_door_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A write past the end of a C array member lands in the next member of the
# same object, where the address sanitizer sees valid memory, and no build
# checks the subscript.  scripts/check-no-raw-array-member.py refuses a C
# array data member and an alias of a C array type in include/foundation and
# include/fixy.  The reviewed exceptions are in
# scripts/raw-array-member-allowlist.txt, and a stale entry fails the gate.
# Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME raw_array_member_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-raw-array-member.py --self-test)
add_test(NAME raw_array_member
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-raw-array-member.py)
set_tests_properties(raw_array_member raw_array_member_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# The fast-math family is banned (CLAUDE.md §V, cmake/FpStrict.cmake is
# the floor).  scripts/check-no-ffast-math.py reads the compile flags of
# this build from its compile database, the link line of each target from
# the CMake file API, the flag variables of every configure preset, and each
# optimize attribute, GCC optimize pragma and _Pragma from the parse tree,
# macro bodies included.  The self-test configures a planted project, so a
# flag inside a generator expression is judged as the compiler receives it.
add_test(NAME no_ffast_math_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-ffast-math.py
          --self-test --cmake ${CMAKE_COMMAND} --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME no_ffast_math
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-ffast-math.py
          --build-dir ${CMAKE_BINARY_DIR})
set_tests_properties(no_ffast_math no_ffast_math_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# The certified-clean headers.  scripts/check-fixy-clean-headers.py reads the
# parse tree of each header on the positive registry
# scripts/fixy-clean-headers.txt, and fails when one names the `safety`
# namespace in code, in a namespace alias or a using-directive, or spells
# `safety::` in prose.  The registry only grows.  Exit 3 when the kit is
# absent reports a skip.
add_test(NAME fixy_clean_headers_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-clean-headers.py --self-test)
add_test(NAME fixy_clean_headers
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-clean-headers.py)
set_tests_properties(fixy_clean_headers fixy_clean_headers_self_test PROPERTIES
    LABELS "ci_guard;ast"
    SKIP_RETURN_CODE 3)
# The fixy doc-blocks name an axis by the enum spelling
# "DimensionAxis::<Name> = <ordinal>", because the retired FX numbering no
# longer matches the enum ordinals.  scripts/check-fixy-dim-prose.py reads the
# comments and string literals of include/fixy and include/crucible/fixy from
# the parse tree and refuses the FX spellings "Dim <N>" and "(dim <N>)".  Code
# is not prose.  Exit 3 when the kit is absent reports a skip.
add_test(NAME fixy_dim_prose_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-dim-prose.py --self-test)
add_test(NAME fixy_dim_prose
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-dim-prose.py)
set_tests_properties(fixy_dim_prose fixy_dim_prose_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# Each direct Linux syscall call in include/, src/ and vessel/ carries a
# `// SYSCALL-CAP-OK: <reason>` comment in its statement or an entry in
# scripts/syscall-capability-allowlist.txt with its effects::* capability
# claim.  scripts/check-syscall-capability.py reads the calls from the
# parse tree, and an entry that matches no live call is stale.  The guard
# also holds the prose of each entry to its sites: the syscall, the
# constants the file spells, and the function that holds each call.  Exit 3
# when the kit is absent reports a skip.
add_test(NAME syscall_capability_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-syscall-capability.py
          --self-test)
add_test(NAME syscall_capability
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-syscall-capability.py)
set_tests_properties(syscall_capability syscall_capability_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The fullness discipline.  `if (count == Capacity) return false;` reads as
# a bounds check, but it refuses only the exact bound: a count already past
# Capacity compares unequal, and the next push writes out of bounds.
# scripts/check-fullness-guard.py reads each `==` between a counter and a
# capacity bound from the parse tree.  A counter whose name ends in `_` is
# protected by a class invariant, by the house convention.  The content-keyed
# rows of scripts/fullness-guard-allowlist.txt are all not-found sentinels.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME fullness_guard_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-fullness-guard.py --self-test)
add_test(NAME fullness_guard
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-fullness-guard.py)
set_tests_properties(fullness_guard fullness_guard_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# DetSafe on the hashing path.  A ledger verdict is a property of one
# machine, so no header of content_hash, merkle_hash, the memory plan or
# the BITEXACT recipe choice may reach the ledger.
# scripts/check-detsafe-ledger.py walks the transitive include closure of
# the hashing-path roots on the parse tree, and it also catches a name of
# the ledger namespace without the include.  There is no allowlist.  Exit 3
# when the kit is absent reports a skip.
add_test(NAME detsafe_ledger_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-detsafe-ledger.py --self-test)
add_test(NAME detsafe_ledger
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-detsafe-ledger.py)
set_tests_properties(detsafe_ledger detsafe_ledger_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)

# Formatting is not gated, and the absence is deliberate.  A clang_format
# guard ran here and was removed.
#
# clang-format 23 needs glibc 2.44 against this host's 2.43, so it ran in a
# container that SELinux-relabels the bind mount on every invocation.
# Concurrent invocations race on that relabel.  A loser cannot read
# .clang-format, silently falls back to LLVM default style, and reports
# most of the tree as dirty.  The guard's output could not reveal this:
# the line that says so, "Error reading /w/.clang-format: Permission
# denied", does not match the shape it greps for, while every consequent
# "<path>.h: Permission denied" does and counts as drift.  Three runs over
# one unchanged tree reported 531, 912 and 2.
#
# Serializing the two tests against each other was not enough, because
# anything else invoking the formatter races them too.  The same mechanism
# reaches the opposite verdict just as easily: a batch producing no output
# reads as a clean scan.  A guard that can silently pass is worse than no
# guard, and style is not worth that.

# The testing door of either tree mints a context with no key, and a context
# is what every ctx-bound mint checks for.  scripts/check-ctx-testing-boundary.py
# counts each use of the door in the code that ships, from the parse tree and
# through the aliases of every file of the scan, and each file with a use needs
# a counted entry in scripts/ctx-testing-boundary-allowlist.txt.
add_test(NAME ctx_testing_boundary_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ctx-testing-boundary.py --self-test)
add_test(NAME ctx_testing_boundary
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ctx-testing-boundary.py)
set_tests_properties(ctx_testing_boundary ctx_testing_boundary_self_test
    PROPERTIES LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)

# A comment, a script or an allowlist in the new tree states what the code
# does, not which task, stage or tracker entry produced it.
# scripts/check-no-coordination-refs.py reads the prose of include/foundation,
# include/fixy, src/foundation, src/fixy, test/foundation, test/fixy, scripts,
# vessel, bench, the root CMakeLists.txt and ci.yml: the comments, strings and
# directive arguments of C++ from the parse tree, the comments and strings of
# Python from its tokens, and every line of any other file.  It has no
# allowlist.  The self-test plants one reference of each family and a file of
# clean look-alikes, and proves the first is reported and the second is not.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME no_coordination_refs_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-coordination-refs.py --self-test)
add_test(NAME no_coordination_refs
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-coordination-refs.py)
set_tests_properties(no_coordination_refs no_coordination_refs_self_test
    PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)

# Differential tests of the session relations against the projection
# oracle of Tirore, Bengtson and Carbone (ITP 2023).  The check emits the
# tests of test/session_oracle/ from golden.csv again and compares them
# with the committed files.  It needs no oracle and no compiler.  The
# self-test compiles the emitted tests, then a copy with one wrong row
# planted in each test, which must fail on that row.  The oracle itself
# runs only when a person regenerates golden.csv.
add_test(NAME session_oracle_check
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/session-oracle.sh --check)
add_test(NAME session_oracle_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/session-oracle.sh --self-test ${CMAKE_CXX_COMPILER})
set_tests_properties(session_oracle_check session_oracle_self_test
    PROPERTIES LABELS "ci_guard;session_oracle")

# The port completeness.  Each public symbol of each superseded `_Foo.h`
# under include/crucible/ must exist in include/foundation/ or
# include/fixy/, or have one sentence in scripts/port-drops.txt.  A
# reflection sentinel that the guard compiles at test time (so CXX is set)
# gives the old names, the preprocessor gives the macros, and the parse tree
# gives the using-declarations and decides which header declares each name.
# A new-tree name counts only when the new tree declares it, not when it
# only uses it.  A drop row is checked both ways, stale and obsolete, so the
# file drains.
#
# A trait can keep its name through the port and lose every specialisation,
# and then answer the same thing for every argument.  The guard counts the
# specialisations of classes and variable templates on both sides from the
# parse trees, and such a trait needs a sentence in scripts/port-folds.txt.
# The self-test plants each kind of miss, fold and bad row, and a layout
# that a line scan misreads.  Exit 3 when the kit is absent reports a skip.
add_test(NAME port_completeness_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-port-completeness.py --self-test)
add_test(NAME port_completeness
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-port-completeness.py --quiet)
set_tests_properties(port_completeness port_completeness_self_test
    PROPERTIES
  ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}"
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The row-membership discipline.  scripts/check-row-contains-discipline.py
# reads each row_contains_v whose first argument is the row of a context
# from the parse tree, with any qualifier, and requires the named lift
# (CtxOwnsCapability, CtxOwnsAnyOf or CtxOwnsAllOf) instead.  Exit 3 when
# the kit is absent reports a skip.
add_test(NAME row_contains_discipline_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-row-contains-discipline.py --self-test)
add_test(NAME row_contains_discipline
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-row-contains-discipline.py)
set_tests_properties(row_contains_discipline row_contains_discipline_self_test
    PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)
# The fixy::is parity.  scripts/check-isx-parity.py reads the parse tree of
# each safety/*Is*.h header.  Each public bool `_v` trait, `_t` alias and
# trait using-declaration directly in safety::extract must have a
# using-declaration in fixy::is in fixy/Is.h.  The namespace path decides
# what is public, so a declaration in extract::detail is not demanded.
add_test(NAME isx_parity_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-isx-parity.py --self-test)
add_test(NAME isx_parity
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-isx-parity.py)
set_tests_properties(isx_parity isx_parity_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# The stub discipline.  scripts/check-stub-discipline.py reads the parse tree
# of every header under include/ and pairs each honesty marker (a constexpr
# bool *_implemented, *_attached or *_ready that is false, at namespace scope
# or as a static member) with a `[[deprecated("CRUCIBLE_STUB:")]]` attribute
# in the same header, in the two directions.  Without the pair, a stub surface
# looks live and fails only at runtime, or code has no constant to test the
# stub by.  With the pair, each call site gets a -Wdeprecated-declarations
# warning at compile time.  A comment or a string that holds the same text is
# neither a marker nor a deprecation.  Exit 3 when the kit is absent reports a
# skip.
add_test(NAME stub_discipline_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-stub-discipline.py --self-test)
add_test(NAME stub_discipline
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-stub-discipline.py)
set_tests_properties(stub_discipline stub_discipline_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The lock-free discipline of the cross-thread atomics (CLAUDE.md §IX).
# scripts/check-lock-free-asserts.py reads each std::atomic, atomic_ref and
# standard atomic alias of canopy, cntp, topology and warden from the parse
# tree, and fails when the file has no sibling
# static_assert(std::atomic<T>::is_always_lock_free) for its type.  Without
# the assert, libatomic can put a mutex behind the atomic on another target
# with no warning.  Exit 3 when the kit is absent reports a skip.
add_test(NAME lock_free_asserts_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-lock-free-asserts.py --self-test)
add_test(NAME lock_free_asserts
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-lock-free-asserts.py)
set_tests_properties(lock_free_asserts lock_free_asserts_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The mint inventory: its self-test, the drift gate that fails when
# misc/mint-inventory.md was not regenerated after a mint changed, and the
# HS14 floor gate.  scripts/gen-mint-inventory.py holds the floor value.
# The generator reads the parse tree, so it skips (exit 3) where the pinned
# tree-sitter kit is absent.  CI installs the kit, so CI enforces.
add_test(NAME mint_inventory_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/gen-mint-inventory.py
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME mint_inventory_check
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/gen-mint-inventory.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME mint_inventory_check_floor
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/gen-mint-inventory.py
          --check-floor
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(mint_inventory_self_test
                     mint_inventory_check
                     mint_inventory_check_floor PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# One combine_ids body.  scripts/check-no-combine-ids-duplicate.py reads
# the parse tree and fails on a second definition named combine_ids, on a
# name with combine_ids and an affix, and on a body with the shape of
# combine_ids under any name.  Exit 3 when the kit is absent reports a skip.
add_test(NAME no_combine_ids_duplicate_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-combine-ids-duplicate.py --self-test)
add_test(NAME no_combine_ids_duplicate
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-no-combine-ids-duplicate.py)
set_tests_properties(no_combine_ids_duplicate
                     no_combine_ids_duplicate_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# ci_guard label-coverage gate.  `ctest -L ci_guard` is the LOCAL
# pre-merge check; a guard registered without the label is silently
# skipped by it, so the gate reports a pass having never run that
# guard.  That is exactly what had happened: 12 of the 42
# script-invoking entries were unlabelled, including fixy_discipline,
# mint_pattern, permission_storage and both contract audits.  CI
# invoked them directly, so only the local gate lied.
#
# This test makes the count self-guarding: a new guard script
# registered without the label fails HERE, so the gap cannot reopen
# silently.  The guard reads the tests that ctest itself lists for this
# build directory, so it sees every subdirectory and every label change.
add_test(NAME ci_guard_labels_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ci-guard-labels.py
          --self-test --cmake ${CMAKE_COMMAND} --ctest ${CMAKE_CTEST_COMMAND})
add_test(NAME ci_guard_labels
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-ci-guard-labels.py
          --build-dir ${CMAKE_BINARY_DIR} --ctest ${CMAKE_CTEST_COMMAND})
set_tests_properties(ci_guard_labels ci_guard_labels_self_test PROPERTIES
    LABELS "ci_guard")
# The cross-build row_hash determinism witness.  The test runs
# dump_row_hashes_foundation and diffs its output against the committed
# golden.  A mismatch says either that the fold changed on purpose, and
# the golden moves in the same commit, or that a TU-context-fragility
# regression in display_string_of moved stable_type_id<T> for some
# wrapper, which is a latent federation-cache bug.
#
# The fold replaced fifty-two per-wrapper salts with one fold over the
# graded shape, and the lattice identity is one of its three inputs, so
# every graded wrapper folds a reflected name. The multi-axis binding in
# fixy/Fn.h folds such an identity for every axis but two.
#
# Gated on x86_64: the payload lines fold display_string_of, so the
# absolute matrix is bit-stable only within one (arch, compiler, stdlib)
# tuple. The golden's own header prints the toolchain tag, so a diff says
# which of the two it was. The arch-independent invariants (distinctness
# and nesting-order sensitivity) run on every arch in their own tests.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  add_test(NAME row_hash_foundation_golden_diff
    COMMAND bash -c
      "set -e; \"$<TARGET_FILE:dump_row_hashes_foundation>\" > \"${CMAKE_BINARY_DIR}/row_hash_foundation_dump_current.txt\"; diff -u \"${CMAKE_SOURCE_DIR}/tools/row_hash_foundation_golden.txt\" \"${CMAKE_BINARY_DIR}/row_hash_foundation_dump_current.txt\""
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  set_tests_properties(row_hash_foundation_golden_diff PROPERTIES
      LABELS "ci_guard")
endif()
crucible_test(test_permissions_compile)
crucible_test(test_handles_compile)
crucible_test(test_sessions_compile)
crucible_test(test_bridges_compile)
crucible_test(test_effects_compile)
crucible_test(test_effects_ctx_forge_attacks)
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
crucible_test(test_ledger)
# The probes time through bench/bench_harness.h rather than carrying their
# own rig — the harness already reports the quantiles, the within-run
# coefficient of variation and the drift check a ledger evidence record
# needs.  Same include-path dependency crucible_hwprobe has, and an include
# path only: this is not a bench target and takes none of the bench tree's
# -O3 -march=native.
crucible_test(test_ledger_probes)
target_include_directories(test_ledger_probes PRIVATE ${CMAKE_SOURCE_DIR}/bench)
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

# Only the grant catalogs open namespace crucible::fixy::grant.
# scripts/check-fixy-grant-namespace-purity.py reads the parse tree of each C++
# file and fails on a namespace definition that reaches the namespace, or a
# declaration whose qualified name reaches into it.  An attack fixture or a
# cheat probe that reopens it on purpose carries a comment that holds "known
# residual gap".  Exit 3 when the kit is absent reports a skip.
add_test(NAME fixy_grant_namespace_purity_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-grant-namespace-purity.py --self-test)
add_test(NAME fixy_grant_namespace_purity
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-fixy-grant-namespace-purity.py)
set_tests_properties(fixy_grant_namespace_purity
                     fixy_grant_namespace_purity_self_test PROPERTIES
    LABELS "safety_attack;ci_guard;ast"
    SKIP_RETURN_CODE 3)

# A specialization of a split trait, or of one of its variables, is legal
# only in the file that defines every tag it names, or in an authoring
# location that scripts/check-splits-orphan.py lists with its reason.  C++
# has no orphan rule, so a foreign TU could otherwise forge cross-region
# authority (CLAUDE.md §IX).  The guard reads each specialization from the
# parse tree.  Exit 2 names an authoring location that no specialization
# needs.  Exit 3 when the kit is absent reports a skip.
add_test(NAME splits_orphan_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-splits-orphan.py --self-test)
add_test(NAME splits_orphan
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-splits-orphan.py)
set_tests_properties(splits_orphan splits_orphan_self_test PROPERTIES
    LABELS "safety_attack;ci_guard"
    SKIP_RETURN_CODE 3)

# FederationAdmission is the one friend of the key that builds a federation
# peer token.  The guard refuses, outside fixy/Federation.h, a definition or
# a specialization of it and a definition of one of its members, which would
# get the same friendship.  It reads the parse tree, each macro body, and the
# macro-expanded text of the compile database, so a macro of another file
# cannot hide a definition.  Exit 3 when the kit is absent reports a skip.
add_test(NAME federation_admission_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-federation-admission.py --self-test)
add_test(NAME federation_admission
         COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-federation-admission.py
                 --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(federation_admission federation_admission_self_test PROPERTIES
    LABELS "safety_attack;ci_guard;ast"
    SKIP_RETURN_CODE 3)

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

# ── Both substrate trees in one translation unit ──────────────
#
# test_two_tree_coexistence.cpp includes every header of both trees, so a
# macro or a declaration of one tree that collides with the other stops
# its build.  The frozen half comes from scripts/frozen-paths.txt and the
# new half is the glob of include/foundation and include/fixy, so a header
# added to either tree joins at the next configure.  Two frozen headers
# take a note.  crucible/bridges/_SessionPersistence.h uses the whole
# Cipher class in inline bodies and does not include crucible/Cipher.h,
# so the generated header includes Cipher.h first.
# crucible/fixy/spawn/_Spawn.h stops on -Werror=redundant-tags in its own
# self-test, and only negative fixtures include it, so it is left out.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/scripts/frozen-paths.txt)
file(STRINGS ${CMAKE_SOURCE_DIR}/scripts/frozen-paths.txt _two_tree_frozen_lines)
set(_two_tree_globs
  ${CMAKE_SOURCE_DIR}/include/foundation/*.h
  ${CMAKE_SOURCE_DIR}/include/fixy/*.h)
foreach(_two_tree_line IN LISTS _two_tree_frozen_lines)
  string(STRIP "${_two_tree_line}" _two_tree_line)
  if(_two_tree_line MATCHES "^include/.*/$")
    list(APPEND _two_tree_globs ${CMAKE_SOURCE_DIR}/${_two_tree_line}*.h)
  elseif(_two_tree_line MATCHES "^include/.*\\.h$")
    list(APPEND _two_tree_globs ${CMAKE_SOURCE_DIR}/${_two_tree_line})
  endif()
endforeach()
file(GLOB_RECURSE _two_tree_headers CONFIGURE_DEPENDS
  RELATIVE ${CMAKE_SOURCE_DIR}/include
  ${_two_tree_globs})
list(REMOVE_ITEM _two_tree_headers crucible/fixy/spawn/_Spawn.h)
list(SORT _two_tree_headers)
set(_two_tree_include_lines "#include <crucible/Cipher.h>\n")
foreach(_two_tree_header IN LISTS _two_tree_headers)
  string(APPEND _two_tree_include_lines "#include <${_two_tree_header}>\n")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/two_tree/every_substrate_header.h
  CONTENT "#pragma once\n${_two_tree_include_lines}")
crucible_test(test_two_tree_coexistence)
target_include_directories(test_two_tree_coexistence PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/two_tree)

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
crucible_test(test_permission_shared_xor_exclusive)  # fix-06 exploit-guard
crucible_test(test_topology)
crucible_test(test_parallelism_rule)
crucible_test(test_pipeline_dispatch)
crucible_test(test_pipeline_dag)
crucible_test(test_workload_budget_coherent)
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
crucible_test(test_mimic_semaphore)

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
# perf::WorkloadProfiler reads the SenseHub counters between calls and
# demotes a parallel ParallelismRule decision under scheduler stress.
# The smoke test checks that the profiler is move-only, passes the
# structural decision through with no Senses, skips the telemetry read
# for an L1-resident budget, clears its diagnostics on reset, never
# promotes, and dispatches a ProfiledDecision to the right arm.
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

# MetaLogSession.h — the typed-session facade over the production MetaLog
# TensorMeta side channel.  Covers the permissioned endpoints, a session
# round trip, and the handle that a session gives back at End.
crucible_test(test_metalog_session)

# SingletonInitGraph.h: the registry of runtime singletons and the gate
# that refuses a cycle in their lazy-initialization graph.
crucible_test(test_singleton_init_graph)

# The ten mints of the session substrates chaselev, metalog, swmr,
# calendar, sharded_grid and sharded_calendar.  The test calls each mint
# and pins its noexcept with a static_assert.  The pool mints (thief,
# swmr_reader) are not in the test, because an atomic refcount cannot be
# constant evaluated.
crucible_test(test_session_substrate_mint_constexpr)

# MpmcChannelSession.h (SEPLOG-H2 / #326) — typed-session facade over
# PermissionedMpmcChannel (Nikolaev SCQ).  Covers single-producer
# round-trip, multi-producer × multi-consumer concurrent stream
# (set-equality drain), immediate-detach, and endpoint forwarder
# helpers.  The fractional × fractional cell of the channel-permission
# family — last of the four cells to receive a session-typed facade.
crucible_test(test_mpmc_channel_session)

# sessions/_SwmrSession.h — typed-session facade for
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

# φ predicate family (FIXY-V-069, tasks #346/#348/#381) —
# FX paper §11.18's 7-level safety hierarchy (safe / df / term /
# nterm / live / live+ / live++) implemented as compile-time
# variable templates over the substrate protocol-tree universe.
# Each predicate uses structural refutation (find a deadlock /
# non-termination / dead-branch witness): SOUND (accepted ⇒ has
# the property) but conservatively incomplete (some FX-safe
# protocols may be rejected).  Lattice ordering verified via
# implication chain phi_live_pp ⇒ phi_live_plus ⇒ phi_live ⇒
# phi_df ⇒ phi_safe; phi_term ∧ phi_nterm proven mutually
# exclusive.
crucible_test(test_session_phi)

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

# Registration against the seal on the schema and kernel tables.  Writers
# register through a view minted before the seal, and a reader reads the
# sealed table with no lock.  Under the tsan preset, a write after the seal
# races the reader and stops the test.
crucible_test(test_registration_seal_race
  tsan/test_registration_seal_race.cpp)

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
  # via explicit developer build. The ctest harness below replays this
  # target's compile command from compile_commands.json into an isolated
  # object path so negative tests can run in parallel without nested
  # Ninja writers racing on the shared build tree.
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  # The driver passes the test iff the compiler's own diagnostics
  # contain the expected-diagnostic substring.  A "clean" build
  # (i.e., the discipline slipped and the .cpp compiled) produces no
  # such text → test fails.  This is strictly stronger than WILL_FAIL.
  #
  # The driver strips GCC's caret display before matching, so a regex
  # cannot be satisfied by the compiler quoting the fixture's own
  # source back at it.  It also reports, per fixture, how many
  # distinct lines of that fixture produced an error; set
  # CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 in the environment to turn
  # "rejects for more than one reason" into a failure and sweep the
  # corpus for fixtures asserting more than they document.
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp
            ${name}
            "${expected_diag}")
  set_tests_properties(${name} PROPERTIES
    LABELS "safety_neg")
endfunction()

# Conjunction form of the harness above.  `crucible_neg_compile_test`
# takes ONE regex, and a regex is satisfied by its weakest alternative,
# so a gate written as `A|B|C` proves only that the weakest of A, B, C
# appeared.  When the weakest alternative is boilerplate that ANY failing
# concept check emits — `constraints.*not satisfied`, `template constraint
# failure`, `associated constraints` — the fixture proves that SOMETHING
# in the TU failed a constraint, not that the invariant under test fired.
#
# The driver requires EVERY regex it is given (see the AND-semantics note
# in neg_compile_driver.py), so passing the boilerplate reason and the
# project-specific token as separate arguments pins both: the rejection
# happened AND it happened to the named thing.  The two pieces of
# evidence sit on different diagnostic lines, so `A.*B` cannot express
# this — `.` does not cross a newline.
function(crucible_neg_compile_multi_test name)
  set(expected_diags ${ARGN})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/safety_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "safety_neg")
endfunction()

# ── The two-regex floor ──────────────────────────────────────────────
#
# The driver requires EVERY regex it is handed, so two regexes are an
# AND: the first says a rejection happened, the second says it happened
# to the thing under test.  One regex cannot say both, because a regex
# is satisfied by its weakest alternative, and the weakest alternative
# of `SomeConcept|constraints.*not satisfied` is boilerplate that ANY
# failing concept check emits.
#
# Measured across the eleven sister harnesses at the time this landed:
# 962 registrations, every one of them passing exactly one regex; 748
# of those carry at least one generic alternative, so they are satisfied
# by boilerplate; and 33 carry nothing but generic alternatives, so they
# witness only that the translation unit failed to compile.  A fixture
# gated on `is private within this context` alone passes when any member
# of any class in the TU is touched from outside.
#
# The floor is staged per directory rather than switched on at once,
# because closing a directory means reading each of its fixtures and
# finding the token its compile output actually carries.  A directory
# named in CRUCIBLE_NEG_TWO_REGEX_FLOORED has been swept; every
# registration in it must pass two or more regexes from then on, so a
# fixture added later cannot quietly re-enter the single-regex form.  A
# directory not yet named keeps the single-regex form until its sweep
# lands.  Directories only ever move into this list.
#
# Write the generic half as `.*`-joined fragments of the wording, never
# as one exact phrase.  GCC says `associated constraints are not
# satisfied` in some rejections and `constraints not satisfied` in
# others, and neither is a substring of the other, so an alternation
# listing one of them stops matching the moment a gate's rejection path
# changes — while every fixture still rejects correctly.  The ctest
# summary for that looks identical to a real regression, and the sweep
# of this file found the dead phrasing on 11 sessions registrations, 50
# effects registrations and 2 topology GAPS codes that the compiler has
# never emitted.  `constraints.*not satisfied` covers both spellings and
# whatever sits between them.
#
# The specific half is the opposite discipline: it must be the exact
# token the fixture's own compile output carries.  Take it from the
# diagnostic, not from the fixture's name or its comment.
#
# The worked example for that rule is neg_fixy_reject_hotpath_misclassified
# and its effectrow sibling.  Both were gated on
# `static assertion failed|is_fixy_diag|HotPathViolation|fixy-A4-030`
# and both passed.  Neither reached its gate: the trait had moved into
# `fixy::diag::` and the fixtures still spelled `fixy::is_fixy_diag_v`,
# so the translation unit died on
# `'is_fixy_diag_v' is not a member of 'crucible::fixy'` — a message
# that contains the substring `is_fixy_diag`.  The fixture was matching
# its own broken reference.  A token drawn from the identifier a fixture
# USES rather than from the diagnostic it PRODUCES can be satisfied by
# the fixture failing to find that identifier at all, which is the
# purest form of a test that proves nothing.  Both are repaired and now
# cite only the assertion's own message.
set(CRUCIBLE_NEG_TWO_REGEX_FLOORED
  ledger_neg
  dataplane_neg
  warden_neg
  perf_neg
  topology_neg
  bridges_neg
  sessions_neg
  effects_neg
  fixy_neg)

# Called by each sister harness with the registration's regex list in
# ARGN.  Silent for a directory that has not been swept yet.
function(crucible_neg_require_two_regexes dir name)
  if(NOT "${dir}" IN_LIST CRUCIBLE_NEG_TWO_REGEX_FLOORED)
    return()
  endif()
  list(LENGTH ARGN regex_count)
  if(regex_count LESS 2)
    message(FATAL_ERROR
      "${name}: ${dir} is under the two-regex floor, so its registration must "
      "pass at least two regexes and this one passes ${regex_count}.  Split the "
      "single regex into the generic half that witnesses the rejection and the "
      "project-specific half that witnesses WHICH gate rejected — the driver "
      "requires both.  If the fixture has no honest second token, say so rather "
      "than inventing one: a fixture that cannot name what it proves is the "
      "defect the floor exists to surface.")
  endif()
endfunction()

# Sister harness for sessions_neg/.  Mirrors crucible_neg_compile_test
# pattern exactly — only the source directory and label differ.
# Sessions-side neg-compile tests (FOUND-C Phase 7, task #618) live
# in test/sessions_neg/ rather than test/safety_neg/ to keep the
# permission-flow witnesses adjacent to the PSH header they probe.
function(crucible_neg_compile_session_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(sessions_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/sessions_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/sessions_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "sessions_neg")
endfunction()

# Sister harness for effects_neg/.  Mirrors the safety_neg / sessions_neg
# pattern — only the source directory and label differ.  Effects-side
# neg-compile tests (FOUND-B13, task #602) probe the Subrow concept's
# substitution-principle gates on Computation.h: extract on non-empty
# row, weaken to disjoint row, lift with non-Effect template arg.
function(crucible_neg_compile_effects_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(effects_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/effects_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/effects_neg/${name}.cpp
            ${name}
            ${expected_diags})
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
function(crucible_neg_compile_cog_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(cog_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/cog_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/cog_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "cog_neg")
endfunction()

# Sister harness for ledger_neg/.  The hardware-capability ledger (#66)
# reads a cached measurement verdict from disk, so its mint has to refuse
# a context that cannot block on a file and a context that must not.
# These fixtures witness the CtxFitsLedgerStore clause firing on both.
function(crucible_neg_compile_ledger_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(ledger_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/ledger_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  # ledger/ProbeSupport.h includes <bench_harness.h> for bench::Report,
  # bench::Compare and bench::compare, so anything reaching RefreshDaemon.h
  # needs bench/ on its path.  Without it these fixtures failed to compile
  # on the missing include and the driver reported them as passing
  # neg-compile tests — failing for a reason that has nothing to do with
  # the constraint they exist to witness.  The include path is the
  # short-term answer; the layering itself wants the statistics surface
  # promoted out of bench/ into include/crucible/, the way SenseHub was.
  target_include_directories(${name} PRIVATE ${CMAKE_SOURCE_DIR}/bench)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/ledger_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "ledger_neg")
endfunction()

# Sister harness for warden_neg/. Warden-policy negative tests live here
# when they are not primarily probing the generic effects substrate.
function(crucible_neg_compile_warden_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(warden_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/warden_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/warden_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "warden_neg")
endfunction()

# Sister harness for perf_neg/.  The negative fixtures of the context
# gate of each perf hub mint live here.  A BPF program load waits on the
# kernel verifier, so the gate of each BPF hub mint rejects a context
# whose row has no Block.
function(crucible_neg_compile_perf_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(perf_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/perf_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/perf_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "perf_neg")
endfunction()

# Sister harness for dataplane_neg/. XDP/TC negative tests live beside the
# CNT-P dataplane ownership boundary instead of Warden.
function(crucible_neg_compile_dataplane_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(dataplane_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/dataplane_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/dataplane_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "dataplane_neg")
endfunction()

# Sister harness for topology_neg/.  Mirrors cog_neg / safety_neg /
# sessions_neg / effects_neg.  Topology-side neg-compile tests live in
# test/topology_neg/ so the per-graph soundness gates
# (CtxFitsTopologyGraph IsExecCtx conjunct, row-contains-Init conjunct,
# future link-layer partition assertions) sit adjacent to the topology/
# headers they probe.
function(crucible_neg_compile_topology_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(topology_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/topology_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/topology_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "topology_neg")
endfunction()

# HS14 floor for the four bridges/* mint factories:
#   mint_recording_session(SessionHandle, ...)
#   mint_recording_session(CrashWatchedHandle, ...)
#   mint_recording_endpoint(Endpoint&&, ...)
#   mint_crash_watched_endpoint<PeerTag>(Endpoint&&, ...)
# Each factory's requires-clause / parameter-shape gate is witnessed
# by ≥2 fixtures probing distinct mismatch classes (non-handle input,
# wrong companion-type, missing template arg, deleted overload via
# `= delete("reason")`).
function(crucible_neg_compile_bridges_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(bridges_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/bridges_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/bridges_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "bridges_neg")
endfunction()

# ── FIXY-CLEAN Phase A — neg-compile fixture harness ────────────────
#
# 12 per-axis engagement-gap fixtures live in test/fixy_neg/.  Each
# fixture instantiates the IsAccepted concept with a 19-engaged pack
# that omits exactly one axis; the named diagnostic
# `FixyNotEngaged_<Axis>` must appear in the compile output.

function(crucible_neg_compile_fixy_test name)
  set(expected_diags ${ARGN})
  crucible_neg_require_two_regexes(fixy_neg ${name} ${expected_diags})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "fixy_neg")
endfunction()

# ── Multi-cell fixy neg-compile harness (fix-19) ────────────────────
#
# A negative-compile TU verifies a soundness gate by FAILING to compile.
# A *fatal* compile error stops the compiler at the first failure, so N
# independent fatal-error cells cannot be verified independently from one
# compile — the first error masks the rest.  That is exactly why the
# one-file-one-assertion harness above exists, and why a blind merge of
# arbitrary neg-compile fixtures into one TU is UNSOUND.
#
# Consolidation is sound ONLY when every cell fails via a NON-FATAL
# diagnostic (a `static_assert` — GCC error-recovers and keeps compiling)
# AND each cell carries a UNIQUE grep token.  Then one TU emits all N
# distinct diagnostics, and we hand the driver all N regexes with
# AND-of-all semantics: every regex must appear or the test reddens
# naming the silent cell.  Per-cell independence is preserved; only the
# file + ctest-registration count drops.
#
# `expected_diags` is a CMake list (semicolon-separated); each element is
# forwarded to neg_compile_driver.py as a separate required regex.
function(crucible_neg_compile_fixy_multi_test name)
  set(expected_diags ${ARGN})
  add_executable(${name} EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            ${CMAKE_BINARY_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/fixy_neg/${name}.cpp
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES
    LABELS "fixy_neg")
endfunction()

# fix-19 pilot: 22 one-axis `neg_fixy_unengaged_<axis>` fixtures collapse
# into one TU (test/fixy_neg/neg_fixy_unengaged_all_axes.cpp).  Each axis
# remains an independently-verified cell because its diagnostic carries a
# unique `FixyNotEngaged_<Axis>:` token and all 22 regexes are required.
crucible_neg_compile_fixy_multi_test(neg_fixy_unengaged_all_axes
  "FixyNotEngaged_Usage:"
  "FixyNotEngaged_Effect:"
  "FixyNotEngaged_Security:"
  "FixyNotEngaged_Protocol:"
  "FixyNotEngaged_Lifetime:"
  "FixyNotEngaged_Provenance:"
  "FixyNotEngaged_Trust:"
  "FixyNotEngaged_Representation:"
  "FixyNotEngaged_Complexity:"
  "FixyNotEngaged_Precision:"
  "FixyNotEngaged_Mutation:"
  "FixyNotEngaged_Staleness:"
  "FixyNotEngaged_Type:"
  "FixyNotEngaged_Refinement:"
  "FixyNotEngaged_Observability:"
  "FixyNotEngaged_Space:"
  "FixyNotEngaged_Overflow:"
  "FixyNotEngaged_Reentrancy:"
  "FixyNotEngaged_Size:"
  "FixyNotEngaged_Version:"
  "FixyNotEngaged_Synchronization:"
  "FixyNotEngaged_Regime:")

# ─── FIXY-FOUND-026 project<G> structured-diagnostic primary template ──
#   Two HS14 fixtures, one per substrate-axis-value-discriminator arm:
#     A. type-valued axes  (Effect — `using type = effects::Row<...>`)
#     B. enum-valued axes  (Security — `::value` + `::value_type`)
#   Each fixture mints a per-domain grant tag (final + derives
#   grant_base + which_dim specialization on a substrate axis) WITHOUT
#   a `project<G>` specialization.  The FOUND-026 static_assert must
#   name the tag in the compiler's instantiation context.
crucible_neg_compile_fixy_test(neg_fixy_project_per_domain_type_axis
  "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "project<G> reached for a grant tag with no specialization|FIXY-FOUND-026|per-domain")
crucible_neg_compile_fixy_test(neg_fixy_project_per_domain_enum_axis
  "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "project<G> reached for a grant tag with no specialization|FIXY-FOUND-026|per-domain")

# ─── FIXY-V-190 fixy/Time.h neg-compiles (HS14, two per mint) ──────────
#   mint_clock_reader  : non-clock-backed source (TscRaw) · non-ExecCtx
#   mint_tsc_reader    : missing pin proof · non-singleton (multi-core) pin
#   mint_bounded_sleep : ctx without Block (Init) · MaxNanos == 0
crucible_neg_compile_fixy_test(neg_time_clock_reader_tsc_source
  "constraints.*not satisfied|no matching function"
    "ClockBacked|mint_clock_reader")
crucible_neg_compile_fixy_test(neg_time_clock_reader_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_clock_reader")
crucible_neg_compile_fixy_test(neg_time_tsc_reader_no_pin
  "constraints.*not satisfied|no matching function"
    "IsSingletonCpuPin|mint_tsc_reader")
crucible_neg_compile_fixy_test(neg_time_tsc_reader_multi_core
  "constraints.*not satisfied|no matching function"
    "IsSingletonCpuPin|mint_tsc_reader")
crucible_neg_compile_fixy_test(neg_time_bounded_sleep_no_block
  "constraints.*not satisfied|no matching function"
    "CtxCanMint|Block|mint_bounded_sleep")
crucible_neg_compile_fixy_test(neg_time_bounded_sleep_zero
  "constraints.*not satisfied|no matching function"
    "MaxNanos|mint_bounded_sleep")

# ─── FIXY-V-191 fixy/Sched.h neg-compiles (HS14, two per NEW mint) ─────
#   mint_affinity         : non-ExecCtx · Posture == NotPinned
#   mint_scheduler_policy : non-ExecCtx · Deadline CBS budget violation
#   mint_priority         : Nice out of [-20,19] · non-ExecCtx
crucible_neg_compile_fixy_test(neg_sched_affinity_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_affinity")
crucible_neg_compile_fixy_test(neg_sched_affinity_not_pinned
  "constraints.*not satisfied|no matching function"
    "CtxFitsAffinityMint|NotPinned|mint_affinity")
crucible_neg_compile_fixy_test(neg_sched_policy_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_scheduler_policy")
crucible_neg_compile_fixy_test(neg_sched_policy_deadline_budget
  "static assertion"
    "CBS admission|RuntimeNs|DeadlineNs")
crucible_neg_compile_fixy_test(neg_sched_priority_out_of_range
  "constraints.*not satisfied|no matching function"
    "CtxFitsPriorityMint|mint_priority")
crucible_neg_compile_fixy_test(neg_sched_priority_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_priority")

# ─── FIXY-V-192 apply_affinity_to_cpu neg-compiles (HS14 spirit) ───────
#   apply_affinity_to_cpu is the runtime ctx-gated sibling of mint_affinity:
#   it routes concurrent/Pipeline.h's pin_current_pipeline_thread_ through
#   a typed fixy surface.  Two distinct-mismatch-class fixtures prove the
#   CtxFitsRuntimeAffinity gate fires on the two ways a call site would
#   silently misfire — a HotFgCtx (row-content mismatch) and a non-ExecCtx
#   (ctx-type mismatch).
crucible_neg_compile_fixy_test(neg_sched_affinity_runtime_hot_ctx
  "constraints.*not satisfied|no matching function"
    "CtxFitsRuntimeAffinity|apply_affinity_to_cpu")
crucible_neg_compile_fixy_test(neg_sched_affinity_runtime_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|apply_affinity_to_cpu")

# ─── FIXY-V-193 MonotonicClock::now_ns neg-compiles (HS14) ─────────────
#   safety::MonotonicClock::now_ns is Bg/Init/Test-row-gated via
#   CtxFitsMonotonicClock — keeps a hot-foreground replay-bound thread
#   from sampling steady_clock (a bit-exact replay-breaking divergence).
#   Two distinct-mismatch-class fixtures: HotFgCtx (row-content) and a
#   bare `int` (ctx-type).
crucible_neg_compile_fixy_test(neg_monotonic_clock_hot_ctx
  "constraints.*not satisfied|no matching function"
    "CtxFitsMonotonicClock|now_ns")
crucible_neg_compile_fixy_test(neg_monotonic_clock_non_exec_ctx
  "constraints.*not satisfied|no matching function"
    "IsExecCtx|now_ns")

# ─── FIXY-V-194 DeadlineWatchdog::observe neg-compiles (HS14) ──────────
#   The Bg/Init/Test gate keeps a hot-foreground replay-bound thread
#   from polling the warden watchdog.  Internally the watchdog now
#   reads CLOCK_BOOTTIME (NOT CLOCK_MONOTONIC) so a host suspend
#   doesn't freeze the window.  Two distinct-mismatch-class fixtures:
#   HotFgCtx (row-content) and a bare `int` (ctx-type).
crucible_neg_compile_fixy_test(neg_deadline_watchdog_hot_ctx
  "no matching function for call to .crucible::warden::DeadlineWatchdog::observe\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsDeadlineWatchdog<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_fixy_test(neg_deadline_watchdog_non_exec_ctx
  "no matching function for call to .crucible::warden::DeadlineWatchdog::observe\\(int\\)."
    "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = int.")

# ─── FIXY-V-196 RdtscPinned witness neg-compiles (mint discipline) ─────
#   RdtscPinned is the bench-local move-only proof that the
#   measurement thread was pinned to a single core via
#   BenchHarness::pin_().  Two distinct mismatch classes prove the
#   forge-resistance:
#     (1) no public default ctor (custom-ctor suppression)
#     (2) private nested mint_tag (friend-only access)
#   Both fixtures live in safety_neg/ but need bench/ on the include
#   path; the per-target include directive below threads it in.
crucible_neg_compile_test(neg_bench_rdtsc_pinned_default_ctor
  "no matching|deleted|no default constructor|RdtscPinned|cannot bind|use of deleted")
target_include_directories(neg_bench_rdtsc_pinned_default_ctor
  SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/bench)
crucible_neg_compile_test(neg_bench_rdtsc_pinned_mint_tag_access
  "private|inaccessible|mint_tag|is private|protected member|within this context")
target_include_directories(neg_bench_rdtsc_pinned_mint_tag_access
  SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/bench)

# fix-19: `neg_fixy_unengaged_regime` folded into the consolidated
# `neg_fixy_unengaged_all_axes` multi-cell TU above (Regime cell).

# fix-08 — the federation publish boundary now gates on ArgsCanonicallyOrdered,
# making CanonicallyOrdered load-bearing: a non-canonical Args stack
# (Tagged<Stale<int>>) is rejected at federation_key, not silently mapped to
# a divergent cache slot.
crucible_neg_compile_test(neg_federation_key_noncanonical_args
  "ArgsCanonicallyOrdered|federation_key|CanonicallyOrdered")

# FIXY-AUDIT-D2 — HS14 floor for mint_fn factory.
crucible_neg_compile_fixy_test(neg_fixy_mint_fn_unengaged_axis   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "AllDimsEngaged")
crucible_neg_compile_fixy_test(neg_fixy_mint_fn_invalid_grant    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "AllGrantsWellFormed")

# fixy-A4-033 — IsGrantTag rejects cv-/ref-qualified grants.  Two
# fixtures cover orthogonal copy-paste-from-runtime bug classes
# (cv-qualification vs reference-qualification).
crucible_neg_compile_fixy_test(neg_fixy_grant_cv_qualified       "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "AllGrantsWellFormed")
crucible_neg_compile_fixy_test(neg_fixy_grant_reference          "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "AllGrantsWellFormed")

# fixy-M-09 — structural-validation concepts for parametric grant
# tags.  Each fixture demonstrates the requires-clause fires on its
# OWN gate's distinct rejection class.  Regex matches the named
# concept OR "constraints.*not satisfied" (GCC's surface for a
# requires-clause failure on the class template).
crucible_neg_compile_fixy_test(neg_fixy_grant_protocol_non_class
    "constraints.*not satisfied"
    "IsSessionProtocol|is_class")
crucible_neg_compile_fixy_test(neg_fixy_grant_refined_with_non_class
    "constraints.*not satisfied"
    "IsRefinementPredicate|is_class")
crucible_neg_compile_fixy_test(neg_fixy_grant_declassify_non_policy
    "constraints.*not satisfied"
    "DeclassificationPolicy|secret_policy_base")
crucible_neg_compile_fixy_test(neg_fixy_grant_from_source_non_empty
    "constraints.*not satisfied"
    "IsProvenanceSource|is_empty|is_class")

# FIXY-V-269 — HS14 floor for the fixy/Hw.h scope<Scope,Arch> grant mint
# (mint_scoped_fence).  Two fixtures cover orthogonal CtxFitsScopedFenceMint
# rejection classes:
#   1. BOTTOM-scope — mint_scoped_fence<Thread, Arch> names the lattice ⊥
#      (the spec's "Scope == None"); the `Scope != Thread` gate rejects (no
#      fence to emit for thread-local-only visibility).
#   2. CROSS-TRUNK — mint_scoped_fence<Gpu, Arm> pairs an accel scope with
#      the ARM fence dialect; scope_arch_trunk_consistent rejects (the
#      grant-mint mirror of CollisionCatalog V402).
crucible_neg_compile_fixy_test(neg_fixy_scope_bottom
    "constraints.*not satisfied"
    "CtxFitsScopedFenceMint")
crucible_neg_compile_fixy_test(neg_fixy_scope_cross_trunk
    "constraints.*not satisfied"
    "CtxFitsScopedFenceMint")

# FIXY-V-272 — lower_fence consistency gates.  Four fixtures cover the
# distinct failure classes the consteval lowering table refuses:
#   3 × V402 cross-trunk (X86×accel, Arm×accel, Gpu×ARM) + 1 × V401
#   device-or-wider scope guarded below AcqRel.
crucible_neg_compile_fixy_test(neg_fixy_v_272_x86_accel_scope
    "static assertion failed"
    "cross-trunk|V402")
crucible_neg_compile_fixy_test(neg_fixy_v_272_arm_accel_scope
    "static assertion failed"
    "cross-trunk|V402")
crucible_neg_compile_fixy_test(neg_fixy_v_272_gpu_arm_scope
    "static assertion failed"
    "cross-trunk|V402")
crucible_neg_compile_fixy_test(neg_fixy_v_272_gpu_weak_strength
    "static assertion failed"
    "AcqRel|V401")
crucible_neg_compile_fixy_test(neg_fixy_v_273_double_fill
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "no member named|has no member|send")
crucible_neg_compile_fixy_test(neg_fixy_v_273_wait_unarrived_phase
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "no member named|has no member|recv")
crucible_neg_compile_fixy_test(neg_fixy_v_273_expect_tx_mismatch
    "constraints.*not satisfied|no matching function"
    "CtxFitsAsyncPipeline")
crucible_neg_compile_fixy_test(neg_fixy_v_273_arm_scope_reject
    "constraints.*not satisfied|no matching function"
    "CtxFitsAsyncPipeline")
crucible_neg_compile_fixy_test(neg_fixy_v_273_unclosed_slot
    "constraints.*not satisfied|static assertion failed"
    "permission_flow_closes")

# HS14 floor for the signature_traits and GradedExtract surfaces.  Two
# fixtures cover orthogonal rejection classes, one per re-exported family:
#   1. signature_traits over a non-function NTTP — reflection of ^^int
#      has no parameters_of (the SignatureTraits surface).
#   2. GradedExtract metafunction over a non-GradedWrapper — the
#      IsGradedWrapper-constrained alias has no definition.
crucible_neg_compile_fixy_test(neg_fixy_v_178_signature_traits_nonfn
    "uncaught exception of type|std::meta::exception"
    "does not represent a function")
crucible_neg_compile_fixy_test(neg_fixy_v_178_graded_extract_nongraded
    "constraints.*not satisfied"
    "IsGradedWrapper|no type|template constraint")

# FIXY-V-244 — HS14 floor for the fixy/grant/Ctrl.h control-flow grants.
# Three fixtures cover orthogonal rejection classes around the
# LOAD-BEARING abort<Rationale> grant + the shared grant cv-purity gate:
#   1. abort missing-rationale ARITY — abort carries no default template
#      arg, so a bare abort<> is "wrong number of template arguments".
#      Witnesses that every abort site MUST declare a reason.
#   2. abort non-string CONVERSION — abort<42> can't deduce the rationale
#      fixed-string NTTP from an int; rejected at template-id formation.
#      Witnesses the rationale must be a string literal, not any value.
#   3. IsGrantTag CV-PURITY — a const-qualified ctrl grant tag is rejected
#      by IsGrantTag (fixy-A4-033 discipline), exercised from the ctrl side.
crucible_neg_compile_fixy_test(neg_fixy_ctrl_abort_requires_rationale
    "wrong number of template arguments"
    "grant::ctrl::abort")
crucible_neg_compile_fixy_test(neg_fixy_ctrl_abort_rationale_not_string
    "could not convert|cannot convert|no matching function"
    "deduction failed|rationale|conversion")
crucible_neg_compile_fixy_test(neg_fixy_ctrl_cv_qualified
    "static assertion"
    "IsGrantTag|cv-qualified")

# FIXY-V-248 — Scenario D: the singleton-init acyclicity proof is load-
# bearing.  A cyclic init-dependency graph fed to the S004 detector must
# fail the acyclicity static_assert (a real static-init-order fiasco would
# redden the build, not slip through).
crucible_neg_compile_fixy_test(neg_fixy_singleton_init_cycle
    "static assertion"
    "cycle|fiasco|S004")

# FIXY-V-093 — HS14 floor for fixy::fp::canonicalize.  Two fixtures
# cover orthogonal rejection classes the static_assert wall enforces:
#   1. Recipe-determinism-tier rejection — recipes with UNORDERED or
#      ORDERED determinism cannot promise the cross-vendor bit-
#      stability that merkle hashing requires.  The is_bitexact()
#      static_assert fires at template-instantiation time.
#   2. Recipe-rounding-mode rejection — recipes pinned to RZ/RM/RP
#      (non-default IEEE modes) lock content_hash to a rounding mode
#      no default kernel pipeline realizes.  The RoundingMode::RN
#      static_assert fires orthogonally to the determinism gate.
# Together they pin the recipe-gated canonicalize_for<R> surface from
# both axes the recipe-tier × rounding-mode product can fail.  The
# bad-recipe in fixture 2 satisfies the determinism gate (BITEXACT_
# STRICT) so ONLY the rounding-mode assert fires — proves the gates
# are independent, not accidentally co-covered.
crucible_neg_compile_fixy_test(neg_fixy_canonicalize_non_bitexact_recipe
    "static assertion failed"
    "BITEXACT_TC or BITEXACT_STRICT|is_bitexact")
crucible_neg_compile_fixy_test(neg_fixy_canonicalize_wrong_rounding_mode
    "static assertion failed"
    "RoundingMode::RN|round-to-nearest")

# The two Box-Muller tiers of Philox, each refused at the gate above it.
# Two fixtures pin orthogonal corners of the (source tier, gate tier)
# grid:
#   1. cipher_fence — the source is the library path (MonotonicClockRead)
#      and the gate is the Cipher write fence (PhiloxRng).  The source is
#      below the fence, so library sin/cos/log cannot reach a replay log.
#   2. wrong_tier_consume — the source is the polynomial path (PhiloxRng)
#      and the gate demands Pure.  The gate is above the source, which
#      pins the polynomial path exactly at PhiloxRng.
# Each pins the refused gate, the tier it demands and the source band.
crucible_neg_compile_fixy_test(neg_philox_box_muller_det_cipher_fence
    "constraints.*not satisfied"
    "admissible_at_cipher_fence<.*At<foundation::algebra::lattices::DetSafeTier::MonotonicClockRead>")
crucible_neg_compile_fixy_test(neg_philox_box_muller_polynomial_det_wrong_tier_consume
    "constraints.*not satisfied"
    "requires_pure_tier<.*At<foundation::algebra::lattices::DetSafeTier::PhiloxRng>")

# fixy-A4-018 — §XXI Universal-Mint-Pattern enforcement.  fixy::fn's
# value ctor is private; only the three canonical mint factories
# (mint_fn, mint_fn_for<UnaryStance>, mint_fn_for<BinaryStance>) are
# friended to construct.  Two fixtures cover orthogonal call-site
# shapes the reviewer might see in production:
#   - direct_ctor_bypass — raw `fixy::fn<T, all-strict-grants>{42}`
#   - stance_ctor_bypass — alias `fixy::stance::PureLinear<int>{42}`
# Both reject with "private within this context" / "declared private".
# A bare "private" is satisfied by ANY inaccessibility diagnostic the TU
# happens to emit, including the tier-3 `AllDimsEngaged` failure a short
# Grants pack produces — which is exactly how direct_ctor_bypass spent
# its life asserting two rejections while documenting one.  Name the
# rejected constructor instead, so the gate can only be met by the
# private-ctor rejection these fixtures exist to witness.
crucible_neg_compile_fixy_test(neg_fixy_fn_direct_ctor_bypass
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "crucible::fixy::fn<Type, Grants>::fn\\(Type\\).*is private within this context")
crucible_neg_compile_fixy_test(neg_fixy_fn_stance_ctor_bypass
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "crucible::fixy::fn<Type, Grants>::fn\\(Type\\).*is private within this context")

# FIXY-AUDIT-E10 + fixy-H-01 — HS14 floor for mint_fn_for stance-bound
# mint.  fixy-H-01 hardened mint_fn_for with explicit requires-clauses
# (`StanceForUnary` for 1-param stances, `StanceForBinary` for 2-param
# stances).  Four fixtures pin distinct Type-axis rejection causes per
# overload (array decay + const-qualified × unary + binary).
crucible_neg_compile_fixy_test(neg_fixy_fn_for_array_type        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForUnary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_const_type        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForUnary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_array_type "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForBinary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_const_type "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForBinary")
# fixy-A4-019: Policy-axis fixtures (void + reference) prove the
# extended StanceForBinary gate rejects bad-Policy shapes BEFORE
# Stance<Type, Policy> would instantiate.  Sibling to the existing
# Type-axis pair above.
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_void_policy "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForBinary")
crucible_neg_compile_fixy_test(neg_fixy_fn_for_binary_ref_policy  "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "StanceForBinary")

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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "fn_ctad_blocked_use_mint_fn|tier 0")
crucible_neg_compile_fixy_test(neg_fixy_fn_direct_sentinel_use
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "fn_ctad_blocked_use_mint_fn|tier 0")

# FIXY-AUDIT-D3 — HS14 floor for fixy::fn class-body static_assert.
# Distinct from D2 (mint_fn requires-clause): the class-body assert
# fires inside class-template instantiation, not overload resolution.
crucible_neg_compile_fixy_test(neg_fixy_fn_class_body_unengaged
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_linear_array       "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_linear_void        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_copy_ref           "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_pure_copy_function      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_iofunction_const        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_iofunction_volatile     "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_bgworker_rvalue_ref     "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_bgworker_void           "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_secret_consumer_array   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_secret_consumer_void    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_async_endpoint_ref      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_async_endpoint_array    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_ctcrypto_void           "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_publicemit_ref          "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")

# FIXY-U-041 — HS14 floor for the 4 new stance aliases.  2 fixtures
# per stance, each a distinct Type-axis rejection class (void vs
# array vs reference).
crucible_neg_compile_fixy_test(neg_fixy_stance_named_session_void      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_named_session_array     "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_sync_blocking_void      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_sync_blocking_array     "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_realtime_hot_void       "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_stance_realtime_hot_ref        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")

# fixy-A4-027 — [[nodiscard]] preservation regression guard for the
# `mint_federation_admittance` using-decl re-export.  Audit flagged a
# §XXI mint discipline risk; this fixture empirically witnesses that
# discarding the return triggers -Werror=unused-result.
crucible_neg_compile_fixy_test(neg_fixy_federation_admittance_nodiscard "ignoring return value|nodiscard"
    "federation")

# FIXY-AUDIT-A3 — HS14 floor for UniqueEngagementPerAxis.
crucible_neg_compile_fixy_test(neg_fixy_duplicate_engagement     "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "UniqueEngagementPerAxis")

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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "first_missing_axis_v|FixyNotEngaged_|Missing-axis diagnostic tag")
# FIXY-FOUND-130: tier-4 now routes through P2741R3 dynamic message that
# surfaces "Duplicate-axis diagnostic tag: FixyDuplicate_<Axis>" literally
# in the static_assert text.  Requiring the literal pins that dynamic
# routing remains live; a regression that drops the P2741R3 wiring would
# fall back to a generic message and fail this fixture.
crucible_neg_compile_fixy_test(neg_fixy_fn_static_assert_duplicate_axis
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "Duplicate-axis diagnostic tag|first_duplicate_axis_v|FixyDuplicate_")
# FIXY-FOUND-130: tier-2 now routes through P2741R3 dynamic message that
# surfaces "Malformed-grant position (0-based): N of M" literally in the
# static_assert text.  Requiring the literal pins that dynamic routing
# remains live; same regression defense as the tier-4 fixture above.
crucible_neg_compile_fixy_test(neg_fixy_fn_static_assert_malformed_grant
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "Malformed-grant position|FixyMalformedGrant|AllGrantsWellFormed")

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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "UniqueEngagementPerAxis|first_duplicate_axis_v|FixyDuplicate_")
crucible_neg_compile_fixy_test(neg_fixy_h05_mint_fn_explicit_type_marker
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "IsGrantTag|AllGrantsWellFormed|IsAccepted|which_dim")
crucible_neg_compile_fixy_test(neg_fixy_count_engagements_non_grant
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "IsGrantTag|AllGrantsWellFormed|IsAccepted|which_dim")

# FIXY-AUDIT-D5 — federation mint HS14 floor.
#
# `mint_federation_admittance(const LocalCipherPermission&, FederationHandshake)`
# is a constexpr token mint with a runtime expected<> error surface.
# The compile-time gates are the parameter types: the proof slot
# demands a `Permission<tag::LocalCipherTag>` reference, and the
# handshake slot demands the `FederationHandshake` POD.  Each
# fixture exercises one of those type-level rejections.
crucible_neg_compile_fixy_test(neg_fixy_federation_wrong_org
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "mint_federation_admittance")
crucible_neg_compile_fixy_test(neg_fixy_federation_missing_proof
    "cannot bind|no matching function"
    "mint_federation_admittance")

# mint_session_handle HS14 floor.
#
# `mint_session_handle<Proto>(resource)` (sessions/_Session.h)
# ships three internal static_asserts: (1) is_well_formed_v<Proto>,
# (2) !is_empty_choice_v<Proto>, (3) SessionResource<Resource>.
# Fixtures exercise (1) and (3) — the two most common author errors.
crucible_neg_compile_fixy_test(neg_fixy_session_handle_malformed_proto
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "Protocol_Ill_Formed|WellFormedRunnableProtocol|is_well_formed_v")
crucible_neg_compile_fixy_test(neg_fixy_session_handle_non_resource
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "SessionResource_NotPinned|SessionResource<")

# FIXY-AUDIT-B1 — HS14 floor for Theory.h §30.14 corpus.
# fixy-H-16: the tier-5 rejection diagnostic must literally name the
# matched corpus entry (struct identifier) AND surface the paper +
# year + remediation prose from cite().  Regex anchors require BOTH
# the concept-fail "NotInTheoryCorpus" trace AND the entry name —
# proves the doc-block §IsAccepted-composition claim ("names which
# corpus entry matched") is honored in code.
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_io
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|classified_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_bg
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|classified_bg_without_declassify")

# fixy-CR-01 — strict-default Security bypass closure.
# Each corpus entry gets a second fixture using `strict<D::Security>`
# (= accept_default_strict_for<Security>, resolves to Classified) to
# witness that the §30.14 detection covers BOTH spellings of the
# Security grant: explicit (as_secret) AND strict-default form.  Before
# Theory.h's second is_secret_grant specialization, the strict-default
# form silently bypassed the corpus despite semantic equivalence.
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_io_strict_default
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|classified_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_classified_bg_strict_default
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|classified_bg_without_declassify")

# ── FIXY-AUDIT-G4 — stance composition rejection.  Stance aliases are
# typedefs `fn<Type, ...full pack...>`; they cannot be supplied as
# grant tags, and a stance's implicit relaxation cannot be supplemented
# with a strict marker on the same axis without colliding on
# UniqueEngagementPerAxis.  Two fixtures pin both shapes:
crucible_neg_compile_fixy_test(neg_fixy_relaxation_plus_strict_same_axis
    "constraints.*not satisfied|constraints.*not satisfied"
    "UniqueEngagementPerAxis|IsAcceptedGrants")
crucible_neg_compile_fixy_test(neg_fixy_stance_as_grant
    "constraints.*not satisfied|constraints.*not satisfied"
    "AllGrantsWellFormed|IsGrantTag")

# FIXY-AUDIT-C8 — HS14 floor for FixyCatalog reconciliation surface.
#
# Two distinct rejection classes for `fixy::diag::is_fixy_diag_v<T>`:
#   - substrate Catalog tag (must NOT register as fixy diagnostic) —
#     the closed-set discipline that keeps Catalog ∩ FixyCatalog == ∅
#   - plain primitive type (catalog membership is NOT just tag_base
#     inheritance — it requires actual tuple presence)
crucible_neg_compile_fixy_test(neg_fixy_diag_catalog_substrate_tag
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "is_fixy_diag_v")
crucible_neg_compile_fixy_test(neg_fixy_diag_catalog_plain_type
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus")
crucible_neg_compile_fixy_test(neg_fixy_ctcrypto_unengaged_security
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "AllDimsEngaged")
crucible_neg_compile_fixy_test(neg_fixy_publicemit_with_secret
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus")
crucible_neg_compile_fixy_test(neg_fixy_publicemit_unengaged_effect
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
crucible_neg_compile_fixy_test(neg_fixy_fn_type_array      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_ref        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_const      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_volatile   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")
crucible_neg_compile_fixy_test(neg_fixy_fn_type_function   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "IsAccepted")

# ── FIXY-CLEAN-B4 — 12 collision-rule neg-compile fixtures ─────────
#
# Each fixture instantiates fixy::fn<int, all-strict-pack> after
# specialising one or more `marks_*` traits on the resolved Fn type,
# tripping exactly one §6.8 collision rule.  The named static_assert
# message from CollisionCatalog.h's validate() carries the rule's
# short code (e.g. "I002:") which the driver greps.
crucible_neg_compile_fixy_test(neg_fixy_rule_i002 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "I002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_l002 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "L002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_e044 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "E044:")
crucible_neg_compile_fixy_test(neg_fixy_rule_i003 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "I003:")
crucible_neg_compile_fixy_test(neg_fixy_rule_m012 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "M012:")
crucible_neg_compile_fixy_test(neg_fixy_rule_p002 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "P002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_i004 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "I004:")
crucible_neg_compile_fixy_test(neg_fixy_rule_n002 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "N002:")
crucible_neg_compile_fixy_test(neg_fixy_rule_l003 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "L003:")
crucible_neg_compile_fixy_test(neg_fixy_rule_m011 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "M011:")
crucible_neg_compile_fixy_test(neg_fixy_rule_s010 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "S010:")
crucible_neg_compile_fixy_test(neg_fixy_rule_s011 "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "S011:")

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
crucible_neg_compile_fixy_test(neg_fixy_lat_secret_wrong_axis   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_unclassified_only   "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_external_wrong_axis "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_tested_only         "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")

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
crucible_neg_compile_fixy_test(neg_fixy_lat_public_wrong_axis      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_internal_wrong_axis    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_classified_wrong_axis  "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Trust")
crucible_neg_compile_fixy_test(neg_fixy_lat_public_only            "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_internal_only          "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_classified_only        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_secret_only            "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_verified_wrong_axis    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_unverified_wrong_axis  "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_tested_wrong_axis      "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Security")
crucible_neg_compile_fixy_test(neg_fixy_lat_verified_only          "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_unverified_only        "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")
crucible_neg_compile_fixy_test(neg_fixy_lat_external_only          "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FixyNotEngaged_Type")

# ── FIXY-MINT-Perm — fixy/Perm.h re-export of permission token mints
# (mint_permission_root/split/combine/split_n/combine_n/share/fork/
# inherit).  Six HS14 fixtures pin the alias's gate — two per frame-rule
# direction (split/combine binary, split_n/combine_n N-ary) plus the
# original root + fork ctx fixtures.  Each fixture witnesses a distinct
# rejection class (CtxAdmitsPermission / CtxFitsPermissionFork /
# splits_into / splits_into_pack):
crucible_neg_compile_fixy_test(neg_fixy_perm_root_no_row_admission
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxAdmitsPermission")
crucible_neg_compile_fixy_test(neg_fixy_perm_root_non_exec_ctx
    "constraints.*not satisfied|constraints.*not satisfied|no matching function"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_perm_fork_ctx_no_bg
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsPermissionFork|row_contains_v")
crucible_neg_compile_fixy_test(neg_fixy_perm_fork_no_splits_into_pack
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsPermissionFork|splits_into_pack")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_undeclared
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_undeclared
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_n_undeclared
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_pack")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_n_undeclared
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_pack")
# fixy-M-29 HS14 floor: two fixtures pin the type-level companion
# witness traits (splits_into_authoring_witness +
# splits_into_pack_authoring_witness) — orphan splits_into / _pack
# specializations without the corresponding witness must fail at
# mint_permission_split / _n body-level static_assert.  Distinct
# mismatch classes: binary vs N-ary trait pair, distinct rejection
# points in Permission.h.
crucible_neg_compile_fixy_test(neg_fixy_perm_split_witness_missing
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_authoring_witness")
crucible_neg_compile_fixy_test(neg_fixy_perm_split_n_witness_missing
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_pack_authoring_witness")
# FIXY-U-074b HS14 floor: combine is the symmetric inverse of split and
# reads the SAME witness-trait pair (Permission.h:753 / :850), so the
# combine minters need the same two distinct mismatch classes split has.
# These two bring mint_permission_combine / _n from HS14:1 -> 2: the
# witness-missing class (splits_into[_pack] true but the M-29 authoring
# witness absent), companion to the existing *_undeclared fixtures.
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_witness_missing
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_authoring_witness")
crucible_neg_compile_fixy_test(neg_fixy_perm_combine_n_witness_missing
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "splits_into_pack_authoring_witness")

# The two overloads of the old proto::mint_session are deleted.  Each
# fixture calls one of them and reads the deletion message.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_session_deleted
    "use of deleted function"
    "is removed|mint_permissioned_session")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_session_bare_deleted
    "use of deleted function"
    "is removed|mint_permissioned_session")

# The HS14 fixtures of three context-bound mints of the old concurrent
# headers.  Each mint has a requires clause with more than one
# conjunct.  Two fixtures for each factory hold two different rejection
# classes:
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
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsPipelineDagMint|CtxFitsPipelineDag|IsStageGraph|pipeline_dag_mint_gate")
crucible_neg_compile_fixy_test(neg_fixy_pipe_pipeline_dag_stage_pack_mismatch
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsPipelineDagMint|stage_pack_type|pipeline_dag_mint_gate")
crucible_neg_compile_fixy_test(neg_fixy_pipe_mpmc_stage_non_endpoint
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsMpmcStageFromEndpoints|mpmc_stage_from_endpoints_gate|StageHandlesMatchEndpointsExtended")
crucible_neg_compile_fixy_test(neg_fixy_pipe_mpmc_stage_arity_mismatch
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsMpmcStageFromEndpoints|mpmc_stage_from_endpoints_gate|arity")
crucible_neg_compile_fixy_test(neg_fixy_pipe_swmr_stage_non_consumer_ep
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsSwmrStageFromEndpoint|swmr_stage_from_endpoint_gate|IsConsumerEndpoint")
crucible_neg_compile_fixy_test(neg_fixy_pipe_swmr_stage_non_writer
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsSwmrStageFromEndpoint|swmr_stage_from_endpoint_gate|is_swmr_writer")

# ── FIXY-V-216 — fixy::handle::EpochVersioned re-export of
# safety::EpochVersioned<T> (Canopy fleet epoch + per-Relay generation;
# FOUND-G68 substrate).  One mismatch axis is kept through the alias:
#   handle_axis_swap — Strong-typed Epoch/Generation distinction at
#       ctor parameter position; swapping the (Epoch, Generation) pair
#       fails because neither newtype is implicitly constructible from
#       the other.  Tests the AXIS rejection axis.
crucible_neg_compile_fixy_test(neg_fixy_handle_epoch_versioned_axis_swap
    "no matching function|cannot convert"
    "EpochVersioned|Epoch|Generation|no known conversion")

# FIXY-V-197 HS14 floor for fixy::sched::mint_priority (closes V-191 gap).
crucible_neg_compile_fixy_test(neg_fixy_sched_mint_priority_not_exec_ctx
    "no matching function|constraints.*not satisfied"
    "mint_priority|CtxFitsPriorityMint|IsExecCtx|is_exec_ctx_v|NotAnExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_sched_mint_priority_nice_out_of_range
    "no matching function|constraints.*not satisfied"
    "mint_priority|CtxFitsPriorityMint|Nice|-21|-20")

# dispatch_workload_decision has two gates, and each fixture reaches
# one.  hot_fg passes a well-formed ProfiledDecision under the hot
# foreground context, whose empty row lacks Bg.  raw_decision passes a
# context the gate admits with a decision written by hand, which the
# parameter type refuses.
crucible_neg_compile_fixy_test(neg_fixy_perf_workload_decision_hot_fg
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Bg>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadDecisionDispatch<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_fixy_test(neg_fixy_perf_workload_decision_raw_decision
    "no matching function for call to .dispatch_workload_decision\\(const fixy::BgDrainCtx&, const fixy::concurrent::ParallelismDecision&,"
    "cannot convert .raw_decision. \\(type .const fixy::concurrent::ParallelismDecision.\\) to type .const crucible::perf::ProfiledDecision&.")

# The per-role federation mints mint_sender, mint_receiver and
# mint_coord take (ctx, endpoint, admittance) and require
# CtxFitsFederation<Ctx>.  Each mint has one fixture per mismatch class:
#   non_ctx    an int is not an IsExecCtx;
#   no_row_fg  HotFgCtx is an IsExecCtx, but its Row<> has no IO and no
#              Block, so the row-subset check is false.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_non_ctx
    "constraints.*not satisfied"
    "CtxFitsFederation|IsExecCtx|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_no_row_fg
    "constraints.*not satisfied"
    "CtxFitsFederation|EffectRowMismatch|row_subset|federation_required_row|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_non_ctx
    "constraints.*not satisfied"
    "CtxFitsFederation|IsExecCtx|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_no_row_fg
    "constraints.*not satisfied"
    "CtxFitsFederation|EffectRowMismatch|row_subset|federation_required_row|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_non_ctx
    "constraints.*not satisfied"
    "CtxFitsFederation|IsExecCtx|mint_coord")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_no_row_fg
    "constraints.*not satisfied"
    "CtxFitsFederation|EffectRowMismatch|row_subset|federation_required_row|mint_coord")

# The third mismatch class per mint: TestRunnerCtx satisfies IsExecCtx
# and the row check, so only the admittance slot stops the call.  An int
# does not bind to SharedPermission<FederatedPeer<Org>>.
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_sender_wrong_admittance
    "no matching function|cannot convert"
    "SharedPermission|FederatedPeer|mint_sender")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_receiver_wrong_admittance
    "no matching function|cannot convert"
    "SharedPermission|FederatedPeer|mint_receiver")
crucible_neg_compile_fixy_test(neg_fixy_sess_mint_coord_wrong_admittance
    "no matching function|cannot convert"
    "SharedPermission|FederatedPeer|mint_coord")

# ── The per-substrate session mints in sessions/_SpscSession.h and
# sessions/_SwmrSession.h.  Two HS14 fixtures pin the
# producer/consumer + writer/reader role gates:
crucible_neg_compile_fixy_test(neg_fixy_substr_spsc_wrong_handle
    "cannot convert|no matching function"
    "ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")

# The typed-session surface of PermissionedSnapshot
# (sessions/_SnapshotSession.h).  Two HS14 fixtures pin distinct
# mismatch classes: role swap (writer-mint receives reader handle)
# and concept rejection (substrate fails SnapshotSessionSurface).
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_unfit_snap
    "constraints.*not satisfied|no matching function"
    "SnapshotSessionSurface")

# HS14 floor of mint_snapshot_reader_session (the writer_session
# variant is covered by snapshot_wrong_handle): role swap
# (WriterHandle->reader_session) and a non-ExecCtx context.
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_session_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# HS14 floor of the SWMR TOKEN mints (no ctx param, so the
# non-ExecCtx class does not apply).  mint_swmr_writer gates on
# SwmrSessionSurface + an exact-type Permission<writer_tag>&& proof;
# mint_swmr_reader gates on SwmrSessionSurface (1-arg) and takes a
# fractional SharedPermission<reader_tag> proof (2-arg).  Two bespoke
# distinct classes each: wrong-perm-tag vs non-surface (writer);
# non-surface vs exclusive-vs-shared permission mode (reader).
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_wrong_perm_tag
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "writer_tag|Permission")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_non_surface
    "constraints.*not satisfied|no matching function"
    "SwmrSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_non_surface
    "constraints.*not satisfied|no matching function"
    "SwmrSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_exclusive_perm
    "cannot convert|no matching function"
    "SharedPermission|reader_tag")

# HS14 floor of the SNAPSHOT TOKEN mints (no ctx param).
# mint_snapshot_writer gates on SnapshotSessionSurface + an exact-type
# Permission<writer_tag>&& proof → wrong-perm-tag vs non-surface.
# mint_snapshot_reader is single-overload surface-gated (no SharedPermission
# variant, unlike SWMR) → two surface-failure modes: bare non-surface (fails
# first nested-type clause) vs near-miss (reader() returns ReaderHandle not
# optional<ReaderHandle>, failing exactly one method-signature clause).
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_writer_wrong_perm_tag
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "writer_tag|Permission")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_writer_non_surface
    "constraints.*not satisfied|no matching function"
    "SnapshotSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_non_surface
    "constraints.*not satisfied|no matching function"
    "SnapshotSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_snapshot_reader_near_miss_surface
    "constraints.*not satisfied|no matching function"
    "SnapshotSessionSurface")

# HS14 floor of the MPMC ENDPOINT token mints (single-arg,
# surface-only gated on MpmcChannelSessionSurface, no permission param).
# Same shape as snapshot_reader → two surface-failure modes each: bare
# non-surface (fails first nested-type clause) vs near-miss (the relevant
# factory — producer()/consumer() — returns the handle directly instead of
# optional<Handle>, failing exactly one method-signature clause).
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_endpoint_non_surface
    "constraints.*not satisfied|no matching function"
    "MpmcChannelSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_endpoint_near_miss
    "constraints.*not satisfied|no matching function"
    "MpmcChannelSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_endpoint_non_surface
    "constraints.*not satisfied|no matching function"
    "MpmcChannelSessionSurface")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_endpoint_near_miss
    "constraints.*not satisfied|no matching function"
    "MpmcChannelSessionSurface")

# proto::mint_session_view carries `requires HandleIsAt<Handle, Tag>`.
# The fixture calls it with an int, which is not a SessionHandle.  The
# probe is a static_assert over a requires-expression, so no handle in a
# Send state is built at namespace scope.
crucible_neg_compile_fixy_test(neg_fixy_sess_session_view_non_handle
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "HandleIsAt|mint_session_view")

# FIXY-U-074n HS14 floor: fixy::source::federation::mint_self_signed_handshake
# gates via `requires FederationOrgTag<Org>` = is_class_v && is_empty_v.
# (Substrate-side equivalents are in test/safety_neg/, which gen-mint-inventory
# does not count toward the fixy umbrella.)  Two distinct classes: non-class
# Org (int → fails is_class_v) vs non-empty class Org (fails is_empty_v).
crucible_neg_compile_fixy_test(neg_fixy_source_mint_self_signed_non_class_org
    "constraints.*not satisfied|no matching function"
    "FederationOrgTag|is_class")
crucible_neg_compile_fixy_test(neg_fixy_source_mint_self_signed_non_empty_org
    "constraints.*not satisfied|no matching function"
    "FederationOrgTag|is_empty")

# concurrent::mint_stage_from_endpoints with a context that does not
# satisfy IsExecCtx and with correct endpoints.  The effects_neg fixture
# neg_mint_stage_from_endpoints_swapped_direction holds the endpoint axis.
crucible_neg_compile_fixy_test(neg_fixy_pipe_stage_from_endpoints_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# HS14 floor of concurrent::mint_substrate_session: two distinct
# rejection conjuncts of its requires-clause (IsBridgeableDirection ∧
# SubstrateFitsCtxResidency ∧ CtxFitsPermissionedProtocol ∧ IsExecCtx
# Ctx): non-bridgeable direction (Snapshot+Producer) and non-ExecCtx
# first argument.
crucible_neg_compile_fixy_test(neg_fixy_substr_substrate_session_non_bridgeable
    "constraints.*not satisfied|no matching function"
    "IsBridgeableDirection|handle_for")
crucible_neg_compile_fixy_test(neg_fixy_substr_substrate_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# HS14 floor of the SWMR session mints (mint_writer_session with
# swmr_wrong_handle above, and mint_reader_session).  Two distinct
# classes per ctx-bound mint: role mismatch (wrong handle type) and
# non-ExecCtx first arg.
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_session_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# HS14 floor of the SWMR runtime-session mints (writer/reader).  Same
# handle types + IsExecCtx constraint as the typed session variants in
# sessions/_SwmrSession.h, so the same two distinct classes apply
# (role-swap wrong-handle + non-ExecCtx).
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_runtime_session_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_writer_runtime_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_runtime_session_wrong_handle
    "cannot convert|no matching function"
    "WriterHandle|ReaderHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_swmr_reader_runtime_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# HS14 floor of the MPMC session mints (producer/consumer).  They are
# ctx-bound and take Channel::{Producer,Consumer}Handle&
# (sessions/_MpmcChannelSession.h), so the same two distinct classes
# apply (role-swap wrong-handle + non-ExecCtx).
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_session_wrong_handle
    "cannot convert|no matching function"
    "ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_producer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_session_wrong_handle
    "cannot convert|no matching function"
    "ProducerHandle|ConsumerHandle")
crucible_neg_compile_fixy_test(neg_fixy_substr_mpmc_consumer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx")

# The ChaseLevDeque session mints in sessions/_ChaseLevDequeSession.h
# (mint_chaselev_owner / mint_chaselev_thief / mint_owner_session /
# mint_thief_session).  Four mints × two distinct mismatch classes
# each = 8 fixtures.  The pairs split:
#   _non_surface  / _wrong_perm  — owner token mint
#   _non_surface  / _wrong_proof — thief token mint
#   _non_ctx      / _wrong_handle — owner ctx-bound session mint
#   _non_ctx      / _wrong_handle — thief ctx-bound session mint
#
# The fixtures witness the per-mint requires-clause AND the
# per-parameter binding shape independently across all four
# ChaseLev mints.
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_non_surface
    "constraints.*not satisfied|no matching function"
    "ChaseLevSessionSurface|mint_chaselev_owner")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_chaselev_owner")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_non_surface
    "constraints.*not satisfied|no matching function"
    "ChaseLevSessionSurface|mint_chaselev_thief")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_wrong_proof
    "no matching function|cannot convert"
    "SharedPermission|mint_chaselev_thief")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_owner_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_owner_session_wrong_handle
    "no matching function|cannot convert"
    "OwnerHandle|mint_owner_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_thief_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_chaselev_thief_session_wrong_handle
    "no matching function|cannot convert"
    "ThiefHandle|mint_thief_session")

# The CalendarGrid session mints in sessions/_CalendarGridSession.h
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
    "constraints.*not satisfied|no matching function"
    "CalendarGridSessionSurface|mint_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_non_surface
    "constraints.*not satisfied|no matching function"
    "CalendarGridSessionSurface|mint_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_producer_session_wrong_handle
    "no matching function|cannot convert"
    "ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_calendar_grid_consumer_session_wrong_handle
    "no matching function|cannot convert"
    "ConsumerHandle|mint_consumer_session")

# The ShardedCalendarGrid session mints in
# sessions/_ShardedCalendarGridSession.h
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
# the sharded mints to prove the non-deducible-parameter
# discipline holds on both sides.
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_non_surface
    "constraints.*not satisfied|no matching function"
    "ShardedCalendarGridSessionSurface|mint_sharded_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_sharded_calendar_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_non_surface
    "constraints.*not satisfied|no matching function"
    "ShardedCalendarGridSessionSurface|mint_sharded_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_sharded_calendar_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_producer_session_wrong_handle
    "no matching function|cannot convert"
    "ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_calendar_grid_consumer_session_wrong_handle
    "no matching function|cannot convert"
    "ConsumerHandle|mint_consumer_session")

# The ShardedGrid session mints in sessions/_ShardedGridSession.h
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
    "constraints.*not satisfied|no matching function"
    "ShardedGridSessionSurface|mint_sharded_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_sharded_grid_producer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_non_surface
    "constraints.*not satisfied|no matching function"
    "ShardedGridSessionSurface|mint_sharded_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_wrong_perm
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "Permission|mint_sharded_grid_consumer")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_producer_session_wrong_handle
    "no matching function|cannot convert"
    "ProducerHandle|mint_producer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_session_non_ctx
    "constraints.*not satisfied|no matching function"
    "IsExecCtx|mint_consumer_session")
crucible_neg_compile_fixy_test(neg_fixy_substr_sharded_grid_consumer_session_wrong_handle
    "no matching function|cannot convert"
    "ConsumerHandle|mint_consumer_session")

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
    "static assertion failed"
    "HotPathViolation|fixy-A4-030")
crucible_neg_compile_fixy_test(neg_fixy_reject_effectrow_misclassified
    "static assertion failed"
    "EffectRowMismatch|fixy-A4-030")

# ── FIXY-MINT-Mach — fixy/Mach.h re-export of the state-machine token
# mints (mint_machine, transition_to):
crucible_neg_compile_fixy_test(neg_fixy_mach_construct_unbuildable
    "constraints.*not satisfied|constraints.*not satisfied"
    "is_constructible")
crucible_neg_compile_fixy_test(neg_fixy_mach_too_many_args
    "constraints.*not satisfied|constraints.*not satisfied"
    "is_constructible")
crucible_neg_compile_fixy_test(neg_fixy_mach_transition_unspecialized
    "constraints.*not satisfied|constraints.*not satisfied"
    "MachineTransition")
crucible_neg_compile_fixy_test(neg_fixy_mach_transition_wrong_direction
    "constraints.*not satisfied|constraints.*not satisfied"
    "MachineTransition")

# ── FIXY-WRAP — fixy/Wrap.h re-export of value-level safety wrappers
# (Linear / Refined / SealedRefined / Tagged / Secret / Monotonic /
# AppendOnly / Stale / TimeOrdered / SharedPermission + Mutation.h
# derivatives WriteOnce / WriteOnceNonNull / BoundedMonotonic /
# OrderedAppendOnly / AtomicMonotonic).  10 HS14 fixtures pin
# distinct violation classes across the family:
crucible_neg_compile_fixy_test(neg_fixy_wrap_linear_copy
    "deleted function|use of deleted function"
    "move-only")
crucible_neg_compile_fixy_test(neg_fixy_wrap_refined_predicate_fails
    "contract|not a constant expression"
    "pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_sealedrefined_no_into
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "no member named|has no member|'into'")
crucible_neg_compile_fixy_test(neg_fixy_wrap_tagged_mismatch
    "could not convert|cannot convert|invalid conversion"
    "source::FromUser")
crucible_neg_compile_fixy_test(neg_fixy_wrap_monotonic_backward
    "contract|not a constant expression"
    "pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_writeonce_double_set
    "contract|not a constant expression"
    "pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_writeoncenonnull_nonpointer
    "static assertion"
    "WriteOnceNonNull_NonPointer_Type|requires a pointer")
crucible_neg_compile_fixy_test(neg_fixy_wrap_bounded_monotonic_overflow
    "contract|not a constant expression"
    "pre|contract_assert")
crucible_neg_compile_fixy_test(neg_fixy_wrap_appendonly_redundant_writeonce
    "static assertion"
    "AppendOnly<WriteOnce|is redundant")
crucible_neg_compile_fixy_test(neg_fixy_wrap_timeordered_zero_processes
    "static assertion"
    "TimeOrdered<T, 0>|forbidden|N > 0|N >= 1")

# ── FIXY-DIAG / SOURCE HS14 fixtures ────────────────────────────────
#
# Phase D surfaces: 2 fixtures per surface area pinning distinct
# concept-rejection classes.
#
# Diag:
#   _non_tag              — is_diagnostic_class_v rejects int
#   _diagnostic_non_tag   — Diagnostic<int, ...> requires-clause fires
# Source:
#   _cross_axis_tagged   — Tagged<T, FromUser> != Tagged<T, Sanitized>
#   _trust_vs_source     — source::FromUser != trust::Verified
crucible_neg_compile_fixy_test(neg_fixy_diag_non_tag
    "static assertion"
    "is_diagnostic_class_v|tag_base|DiagnosticAccessor_NonTag")
crucible_neg_compile_fixy_test(neg_fixy_diag_diagnostic_non_tag
    "constraints.*not satisfied|constraints.*not satisfied"
    "is_diagnostic_class_v|tag_base")
crucible_neg_compile_fixy_test(neg_fixy_source_cross_axis_tagged
    "static assertion failed"
    "source::FromUser|source::Sanitized")
crucible_neg_compile_fixy_test(neg_fixy_source_trust_vs_source
    "static assertion"
    "is_same|cross-axis")

# ─────────────────────────────────────────────────────────────────
# FIXY-V-025 HS14 — retag_policy + RetagAllowed reached via fixy alias
# ─────────────────────────────────────────────────────────────────
#
# V-022 ships safety::retag_policy + safety::RetagAllowed; V-023 the
# catalog; V-024 wires the concept onto Tagged::retag()'s requires-
# clause.  V-025 re-exports both at fixy::tags::* (`using` + sentinel
# namespace alias).  Two HS14 fixtures pin distinct mismatch classes
# at the FIXY BAND consumer surfaces:
#   - default_rejects: GATE level via free-function template
#     constrained on fixy::tags::RetagAllowed; sentinel-pair witness.
#   - tagged_retag_cross_axis: CONSUMER level via Tagged::retag()'s
#     wired requires-clause; cross-axis miss with both tags through
#     fixy::tags.
# Broad diagnostic family — GCC 16 wording varies across the
# requires-clause / using-declaration / instantiation paths.
crucible_neg_compile_fixy_test(neg_fixy_retag_policy_default_rejects
    "constraints.*not satisfied|constraints.*not satisfied"
    "the required expression|RetagAllowed")
crucible_neg_compile_fixy_test(neg_fixy_tagged_retag_cross_axis
    "constraints.*not satisfied|constraints.*not satisfied"
    "the required expression|RetagAllowed")

# ── FIXY-STRUCT — fixy/Struct.h re-export of structural wrappers
# (Pinned / NonMovable / NotInherited / FinalBy / Checked /
# ConstantTime / Simd / OwnedRegion / Workload).  11 HS14 fixtures
# pin distinct violation classes across the family:
crucible_neg_compile_fixy_test(neg_fixy_struct_pinned_copy
    "deleted function|use of deleted function"
    "stable address")
crucible_neg_compile_fixy_test(neg_fixy_struct_pinned_move
    "deleted function|use of deleted function"
    "stable address")
crucible_neg_compile_fixy_test(neg_fixy_struct_nonmovable_copy
    "deleted function|use of deleted function"
    "exclusive ownership")
crucible_neg_compile_fixy_test(neg_fixy_struct_notinherited_non_final
    "static assertion"
    "NotInherited_Not_Final|not marked")
crucible_neg_compile_fixy_test(neg_fixy_struct_finalby_subclass
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "FinalBy_Subclass_Forbidden|private within this context|is private")
crucible_neg_compile_fixy_test(neg_fixy_struct_checked_signed_shift
    "no matching function|constraints.*not satisfied"
    "checked_shl")
crucible_neg_compile_fixy_test(neg_fixy_struct_checked_safe_capacity_overflow
    "static assertion"
    "Checked_Capacity_Overflow|overflow")
crucible_neg_compile_fixy_test(neg_fixy_struct_ct_signed_select
    "constraints.*not satisfied|associated constraints|no matching function"
    "unsigned_integral")
crucible_neg_compile_fixy_test(neg_fixy_struct_ct_signed_is_zero
    "constraints.*not satisfied|associated constraints|no matching function"
    "unsigned_integral")
crucible_neg_compile_fixy_test(neg_fixy_struct_simd_detsafe_float
    "constraints.*not satisfied|associated constraints"
    "DetSafeSimd|std::integral")
# `iota_v<int>` does reject, but none of the old alternatives appeared in
# any diagnostic — `value_type` was matched out of the quoted body of
# `iota_v` itself.  Require the instantiation that was attempted and the
# error it produced, which are the two lines the compiler actually wrote.
crucible_neg_compile_fixy_multi_test(neg_fixy_struct_simd_iota_non_vec
    "constexpr V crucible::simd::iota_v\\(\\) .with V = int."
    ".int. is not a class, struct, or union type")
crucible_neg_compile_fixy_test(neg_fixy_struct_owned_region_copy
    "deleted function|use of deleted function"
    "move-only|Permission")
crucible_neg_compile_fixy_test(neg_fixy_struct_workload_zero_shards
    "static assertion"
    "requires N > 0|N > 0|N >= 1")

# ── FIXY-IS — fixy/Is.h concept-gate aliases for safety/Is*.h.
# 3 HS14 fixtures pin canonical alias-rejects-unrelated cases:
crucible_neg_compile_fixy_test(neg_fixy_is_linear_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsLinear")
crucible_neg_compile_fixy_test(neg_fixy_is_secret_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsSecret")
crucible_neg_compile_fixy_test(neg_fixy_is_session_handle_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsSessionHandle")

# GAPS-047: Delegate<T, K> now bonds recipient-crash propagation into
# the public crash walker.  This fixture pins the unrecovered outbound
# delegated-protocol case at the consteval assertion boundary.
crucible_neg_compile_session_test(neg_delegate_recipient_crash_unrecovered
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "static assertion failed.*CrashBranch_Missing_In_Tree")

# GAPS-049: exact-name delegated-crash rejection fixtures.  These pin
# the public consteval assertion boundary so custom call sites do not
# have to infer the failure from the broader crash-walker diagnostic.
crucible_neg_compile_session_test(neg_delegate_to_unreliable_no_crash_branch
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "DelegatedCrashPropagation_MissingRecovery|recipient is unreliable but carrier K has no crash-recovery branch")

# GAPS-048: Delegate<Stop, K> has no K continuation at the handle
# surface; the operation is explicitly deleted with a framework-owned
# diagnostic.
crucible_neg_compile_session_test(neg_delegate_stop_continuation
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "DelegateStop_NoContinuation")

crucible_neg_compile_session_test(neg_delegate_stop_without_continuation_capability
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "DelegateStop_NoContinuation")

crucible_neg_compile_session_test(neg_delegate_compose_incompatible_crash_classes
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CrashLattice_Composition_Incompatible")

crucible_neg_compile_session_test(neg_delegate_recipient_crash_no_propagation_metafunction
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "DelegatedCrashPropagation_PrimaryTemplate|primary template fires")

# GAPS-064: CrashWatchedHandle consumes Stop_g<CrashClass C>.
# These fixtures pin the NoThrow rejection and the exact Stop_g grade
# match at the watched crash boundary.
crucible_neg_compile_session_test(neg_crashwatched_noThrow_with_unreliable_peer
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CrashWatched_NoThrow_Rejected")
crucible_neg_compile_session_test(neg_crashwatched_strong_class_recovery_path_missing
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CrashWatched_StopClass_Mismatch")
crucible_neg_compile_session_test(neg_crashwatched_class_lattice_violation
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CrashWatched_StopClass_Mismatch")
crucible_neg_compile_session_test(neg_crashwatched_stop_class_mismatch
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CrashWatched_StopClass_Mismatch")

# HS14 floor for the `!is_terminal_state<B>` conjunct
# in the is_well_formed<Loop<B>, LoopCtx> specialization at
# include/crucible/sessions/_Session.h.  Together these two fixtures
# witness the rejection of `Loop<End>` and `Loop<Stop>` — both
# semantically equivalent to their terminal body, neither containing
# a reachable Continue.
crucible_neg_compile_session_test(neg_loop_body_terminal_end
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "Protocol_Ill_Formed|is_well_formed")
crucible_neg_compile_session_test(neg_loop_body_terminal_stop
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "Protocol_Ill_Formed|is_well_formed")

# GAPS-066: NumericalTier Send covariance / Recv contravariance and
# channel-boundary fixtures.  These pin the compile-time recipe-tier
# discipline that feeds MIMIC §41: weak producers cannot satisfy strict
# receivers, bare payloads cannot regain tier evidence, and loop
# composition cannot hide a downgrade.
crucible_neg_compile_session_test(neg_send_weaker_tier_to_recv_stronger
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_ChannelBoundary_ProducerTierTooWeak")
crucible_neg_compile_session_test(neg_recv_promotes_unauthorized
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_RecvPromotion_RequiresTightening")
crucible_neg_compile_session_test(neg_session_subtype_tier_mismatch
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_SendSubtype_ProducerTierTooWeak")
crucible_neg_compile_session_test(neg_payload_unwrap_loses_tier
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_BarePayload_CannotGainTier")
crucible_neg_compile_session_test(neg_default_recipe_to_strict_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_DefaultRecipe_CannotSatisfyStrictRecv")
crucible_neg_compile_session_test(neg_tier_compose_in_loop
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "NumericalTier_LoopBody_TierDowngrade")

# GAPS-067: VendorCtx pins PermissionedSessionHandle chains to a
# VendorLattice backend.  NV and AMD are mutually-incomparable, so the
# framework-owned composition assertion rejects an NV provider for an
# AMD consumer.
crucible_neg_compile_session_test(neg_vendor_ctx_nv_to_amd
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_vendor_ctx_none_to_nv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")

# GAPS-068: VendorCtx/VendorPinned admission at session mint,
# substrate bridge, Delegate_seq, subtype, and payload boundaries.
crucible_neg_compile_session_test(neg_mint_substrate_nv_to_amd
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_cntp_layer_vendor_mismatch
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_collective_session_vendor_unspecified
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_session_subtype_vendor_violation
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "SessionSubtype\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_session_compose_cross_vendor_no_portable
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_session_promote_to_portable_unauthorized
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*VendorCtx_Mismatch")
crucible_neg_compile_session_test(neg_send_payload_vendor_in_pure_session
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolVendorAdmittedByLoopCtx")
crucible_neg_compile_session_test(neg_recv_payload_unauthorized_vendor
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolVendorAdmittedByLoopCtx")

# GAPS-070: ProductLattice protocol-grade filter on synchronous
# subtyping.  Each fixture installs a deliberately-too-permissive
# local is_subsort axiom; the structural Gay-Hole relation would pass,
# but the orthogonal grade filter must still reject.
crucible_neg_compile_session_test(neg_session_grade_vendor_axis
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numerical_axis
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_cipher_axis
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_epoch_axis
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numa_axis
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_vendor_numerical_axes
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_vendor_cipher_axes
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_numerical_cipher_axes
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_evolution_weakens_numerical
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")
crucible_neg_compile_session_test(neg_session_grade_evolution_changes_vendor
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolGradeMismatch")

# GAPS-072: EpochedDelegate ctx-bound minting pins cross-Relay reshard
# handoffs to the current sender epoch/generation while EpochedAccept
# rejects stale recipients.
crucible_neg_compile_session_test(neg_epoched_delegate_stale_epoch_send
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")
crucible_neg_compile_session_test(neg_epoched_delegate_recipient_stale_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleRecipient")
crucible_neg_compile_session_test(neg_epoched_delegate_generation_decrease
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")
crucible_neg_compile_session_test(neg_epoched_delegate_compose_stale_chain
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolEpochAdmittedByLoopCtx|CtxFitsPermissionedProtocol|EpochCtx_StaleSender")

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
    "static assertion failed"
    "static_assert|OsUniverse_Overflow|fixy-A3-018")
crucible_neg_compile_effects_test(neg_os_universe_atom_underlying_widened
    "static assertion failed"
    "static_assert|OsUniverse_Underlying|fixy-A3-018")

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
    "static assertion|contract|precondition|non-constant"
    "content_hash|is_zero")
crucible_neg_compile_cog_test(neg_cog_identity_compute_kind_rejects_nic
    "constraint|constraints.*not satisfied"
    "IsComputeKind|kernel_count_for|GAPS-185")

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
    "constraint|constraints.*not satisfied"
    "HasCaps|lookup_caps|PsuRail|GAPS-186")
crucible_neg_compile_cog_test(neg_target_caps_warp_size_not_power_of_two
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<(fixy::)?IsPowerOfTwo\\(\\), (fixy::)?BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(33\\)")

# cog/OffloadTarget.h, the one check that a Cog can carry an offload.
#   * neg_offload_target_feature_of_other_schema: a feature names a bit of
#     the schema of its own enum.
#   * neg_offload_target_no_kind: an offload names a kind of Cog.
crucible_neg_compile_cog_test(neg_offload_target_feature_of_other_schema
    "no matching function for call to .validate_offload_target<crucible::cog::GpuFeature::GpuDirectRdma, crucible::cog::CogKind::NvSwitch>\\("
    "required for the satisfaction of .AdvertisesFeatureOf<Caps, Feature>. \\[with Caps = crucible::cog::NvSwitchTargetCaps")
crucible_neg_compile_cog_test(neg_offload_target_no_kind
    "no matching function for call to .validate_offload_target<crucible::cog::SwitchFeature::P4>\\("
    "the expression .sizeof \\.\\.\\. \\(\\(Kinds \\.\\.\\.\\)\\) > 0 \\[with Kinds = \\{\\}\\]. evaluated to .false.")

# ─── #66 ledger/Ledger.h HS14 fixtures ────────────────────────────
#
# Two fixtures pinning the two halves of CtxFitsLedgerStore, so a
# regression that dropped either conjunct is still caught by the other:
# * neg_ledger_view_hot_fg_ctx — HotFgCtx carries Row<>, claiming
#   nothing.  Missing both IO and Block.  This is the mismatch that
#   matters in practice: the hot path consults a value someone else
#   measured, and a caller that loaded the file itself would turn a
#   ~2 ns dispatch into a disk read.
# * neg_ledger_view_init_ctx_cannot_block — ColdInitCtx carries
#   Row<Init, Alloc, IO>, so it CAN touch the kernel.  Missing only
#   Block, because cap_permitted_row<Init> deliberately omits it:
#   initialization is not permitted to park on a disk.
crucible_neg_compile_ledger_test(neg_ledger_view_hot_fg_ctx
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsLedgerStore")
crucible_neg_compile_ledger_test(neg_ledger_view_init_ctx_cannot_block
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsLedgerStore")

# The host probe reads procfs and sysfs, so CtxFitsHostProbe admits a
# context only when it owns IO and Block.  The foreground context owns
# neither, and the startup context owns IO but not Block.
crucible_neg_compile_ledger_test(neg_host_probe_hot_fg_ctx
    "no matching function for call to .probe_host_facts\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsHostProbe<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_ledger_test(neg_host_probe_init_ctx_cannot_block
    "no matching function for call to .probe_competence\\(const fixy::ColdInitCtx&\\)."
    "required for the satisfaction of .CtxFitsHostProbe<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")

# ─── #67 ledger/RefreshDaemon.h HS14 fixtures ─────────────────────
#
# Three fixtures pinning the three conjuncts of CtxFitsRefreshDaemon
# separately, so a regression that dropped any one is caught:
# * neg_refresh_daemon_test_ctx_lacks_bg — TestRunnerCtx has IO and
#   Block and satisfies CtxFitsLedgerStore.  It lacks only Bg.  This is
#   the fixture that matters: without it, dropping Bg leaves the concept
#   identical to CtxFitsLedgerStore and the other two keep passing.
# * neg_refresh_daemon_compile_ctx_cannot_block — BgCompileCtx has Bg
#   and IO.  It lacks only Block.  The opposite conjunct from the above.
# * neg_refresh_daemon_hot_fg_ctx — HotFgCtx carries Row<>, missing all
#   three.  The blunt case, with the worst consequence.
#
# The regex deliberately omits `static assertion`.  With Bg removed the
# fixture fails on its own premise assert instead of on the mint, and a
# looser pattern would read that as a pass.
crucible_neg_compile_ledger_test(neg_refresh_daemon_test_ctx_lacks_bg
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")
crucible_neg_compile_ledger_test(neg_refresh_daemon_compile_ctx_cannot_block
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")
crucible_neg_compile_ledger_test(neg_refresh_daemon_hot_fg_ctx
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")

# ─── GAPS-187 OpcodeLatencyTable HS14 fixtures ────────────────────
#
# Two fixtures, distinct mismatch classes:
#   * Concept gate (HasOpcodeTable<K>) — refuses non-substrate Cogs
#     (PsuRail) at template substitution.  Mirrors the GAPS-186 Caps
#     concept gate; double-rejection on HasCaps + opcodes_for-defined.
#   * Data-invariant gate (OrderedLatencyQuantiles) — the checked mint
#     refuses a p50/p99/p999 triple out of order.  The second pattern
#     names the predicate type and the refused triple, so only this
#     refusal matches.
crucible_neg_compile_cog_test(neg_opcode_latency_rejects_psu_rail
    "constraint|constraints.*not satisfied"
    "HasOpcodeTable|lookup_opcodes|PsuRail|GAPS-187")
crucible_neg_compile_cog_test(neg_opcode_latency_quantiles_misordered
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{99, 50, 30\\}\\)")

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
    "constraint|constraints.*not satisfied"
    "FitsCog|schedule_kernel|row_fits_cog|999|GAPS-191")
crucible_neg_compile_cog_test(neg_fits_cog_non_substrate
    "constraint|constraints.*not satisfied"
    "FitsCog|HasCogCapacity|schedule_kernel|PsuRail|GAPS-191")

# cog/NicOffloadAudit.h, the read-only NIC offload audit.
#   * neg_nic_offload_audit_rejects_gpu: the audit refuses a kind other
#     than a NIC port in its constraint.
#   * neg_nic_offload_audit_raw_required_offloads: a raw integer mask does
#     not convert to the typed offload set.
crucible_neg_compile_cog_test(neg_nic_offload_audit_rejects_gpu
    "no matching function for call to .audit_nic_offloads<crucible::cog::CogKind::Gpu>"
    "required for the satisfaction of .NicOffloadAuditableCog<K>. \\[with K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_cog_test(neg_nic_offload_audit_raw_required_offloads
    "could not convert .1. from .unsigned int."
    "to .fixy::Bits<crucible::cog::NicFeature>.")

# cog/NumaNic.h, the read-only NUMA placement check of a NIC.
#   * neg_numa_nic_rejects_gpu: the check refuses a kind other than a NIC
#     port in its constraint.
#   * neg_numa_nic_raw_node: a raw integer does not convert to a NUMA
#     node id.
crucible_neg_compile_cog_test(neg_numa_nic_rejects_gpu
    "no matching function for call to .verify_numa_pinning<crucible::cog::CogKind::Gpu>"
    "required for the satisfaction of .NumaNicAuditableCog<K>. \\[with K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_cog_test(neg_numa_nic_raw_node
    "could not convert .0. from .unsigned int."
    "to .crucible::cog::NumaNodeId.")

# warden/Quarantine.h, the quarantine policy of the Cogs.  Each fixture
# pins the call or the class that is refused and the gate that refuses it.
#   * mint_bg_ctx, mint_hot_fg_ctx: only a context that owns Init mints a
#     policy.
#   * direct_construct: the constructor is private, so the mint is the one
#     door.
#   * record_fg_ctx, record_init_ctx: only a context that owns Bg records a
#     health, failure or probe fact.
#   * override_bg_ctx, override_hot_fg_ctx: only a context that owns Init
#     or Test forces a state.
#   * override_missing_permission: a forced state needs the operator token.
#   * zero_slots, zero_events: a policy has Cog slots and event slots.
crucible_neg_compile_warden_test(neg_quarantine_mint_bg_ctx
    "no matching function for call to .mint_quarantine_policy<2>\\(fixy::BgDrainCtx\\)."
    "required for the satisfaction of .CtxFitsQuarantineMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_warden_test(neg_quarantine_mint_hot_fg_ctx
    "no matching function for call to .mint_quarantine_policy<2>\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsQuarantineMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_warden_test(neg_quarantine_direct_construct
    ".constexpr crucible::warden::QuarantinePolicy<MaxCogs, MaxEvents>::QuarantinePolicy\\(crucible::warden::QuarantineConfig\\) \\[with long unsigned int MaxCogs = 2. long unsigned int MaxEvents = 8\\]. is private within this context"
    "declared private here")
crucible_neg_compile_warden_test(neg_quarantine_record_fg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2>::on_health_event\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::topology::HealthSnapshot&, int\\)."
    "required for the satisfaction of .CtxFitsQuarantineRecord<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_warden_test(neg_quarantine_record_init_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2>::record_recovery_probe\\(const fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::observe::ProbeOutcome&, int\\)."
    "required for the satisfaction of .CtxFitsQuarantineRecord<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >.")
crucible_neg_compile_warden_test(neg_quarantine_override_bg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2>::operator_override\\(fixy::BgDrainCtx, "
    "required for the satisfaction of .CtxFitsQuarantineOverride<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_warden_test(neg_quarantine_override_hot_fg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2>::operator_override\\(const fixy::HotFgCtx&, "
    "required for the satisfaction of .CtxFitsQuarantineOverride<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_warden_test(neg_quarantine_override_missing_permission
    "no matching function for call to .crucible::warden::QuarantinePolicy<2>::operator_override\\(const fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::warden::QuarantineState, int, int\\)."
    "candidate expects 6 arguments, 5 provided")
crucible_neg_compile_warden_test(neg_quarantine_zero_slots
    "In instantiation of .class crucible::warden::QuarantinePolicy<0, 4>."
    "static assertion failed: QuarantinePolicy requires Cog slots")
crucible_neg_compile_warden_test(neg_quarantine_zero_events
    "In instantiation of .class crucible::warden::QuarantinePolicy<2, 0>."
    "static assertion failed: QuarantinePolicy requires audit-event slots")

# ─── FIXY-U-084 §XXI Universal Mint Pattern for warden/ hubs ──────
# Three new mints (mint_hardening / mint_hot_region_registry_handle /
# mint_deadline_watchdog) each ship two HS14 neg-compile fixtures
# proving the CtxFits* concept gates fire on missing Init.
crucible_neg_compile_warden_test(neg_hardening_mint_bg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHardeningMint")
crucible_neg_compile_warden_test(neg_hardening_mint_hot_fg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHardeningMint")
# The cold init context owns Init but no Block, and the choice of the hot
# CPU reads sysfs.
crucible_neg_compile_warden_test(neg_hardening_mint_cold_init_ctx
    "required for the satisfaction of .CtxFitsFileOpen<Ctx>."
    "required for the satisfaction of .CtxFitsHardeningMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_bg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_hot_fg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_warden_test(neg_hot_region_registry_handle_direct_ctor
    "HotRegionRegistryHandle::HotRegionRegistryHandle\\(\\). is private within this context"
    "declared private here")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_bg_ctx
    "no matching function for call to .mint_deadline_watchdog\\(fixy::BgLoadCtx, std::nullptr_t, crucible::warden::Policy&\\)."
    "required for the satisfaction of .CtxFitsDeadlineWatchdogMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_hot_fg_ctx
    "no matching function for call to .mint_deadline_watchdog\\(const fixy::HotFgCtx&, std::nullptr_t, crucible::warden::Policy&\\)."
    "required for the satisfaction of .CtxFitsDeadlineWatchdogMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")

# ─── §XXI Universal Mint Pattern for perf/ hubs ───────────────────
# Each perf hub mint has at least two negative fixtures, and each fixture
# shows one context that the context gate of the mint rejects.
#
# The gate of each BPF hub mint demands a row that holds Alloc, IO and
# Block, because the load calls bpf(BPF_PROG_LOAD) and waits on the
# kernel verifier.  Each BPF hub has four fixtures: a background drain
# context, a hot foreground context, a cold init context and a
# background compile context.  Each fixture pins the gate, the row that
# the gate demands and the row of the context that it rejects.
crucible_neg_compile_perf_test(neg_sense_hub_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sense_hub_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")

# The six other BPF hubs (LockContention, PmuSample, SchedSwitch,
# SchedTpBtf, SyscallLatency and SyscallTpBtf) have the same gate as the
# SenseHub mint.  WorkloadProfiler borrows a hub that is already loaded,
# and its gate is a different one.
crucible_neg_compile_perf_test(neg_lock_contention_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_lock_contention_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_sched_switch_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sched_switch_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_perf_test(neg_workload_profiler_mint_bg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Init>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadProfilerMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_workload_profiler_mint_hot_fg_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Init>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadProfilerMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
# The two constructors are private: a profiler comes only from its
# mint, and a ProfiledDecision only from a profiler.  The decision is
# not trivially copyable either, so std::bit_cast cannot forge one.
crucible_neg_compile_perf_test(neg_workload_profiler_constructor_private
    "is private within this context"
    ".constexpr crucible::perf::WorkloadProfiler::WorkloadProfiler\\(const crucible::perf::Senses\\*, crucible::perf::WorkloadProfilerConfig\\). is private")
crucible_neg_compile_perf_test(neg_workload_profiler_decision_constructor_private
    "is private within this context"
    ".constexpr crucible::perf::ProfiledDecision::ProfiledDecision\\(fixy::concurrent::ParallelismDecision\\). is private")
crucible_neg_compile_perf_test(neg_workload_profiler_decision_bit_cast
    "the expression .is_trivially_copyable_v<_To> .with _To = crucible::perf::ProfiledDecision.. evaluated to .false."
    ".crucible::perf::ProfiledDecision. is not trivially copyable")

# The second pair of fixtures for each BPF hub mint:
#
#   * _cold_init_ctx: ColdInitCtx claims Alloc and IO and no Block.  It
#     holds the startup capability, and the gate rejects it on the row.
#   * _bg_compile_ctx: BgCompileCtx holds the background capability and
#     claims Row<Bg, Alloc, IO>, so it fails on Block alone.  With the
#     fixture above, it shows that the gate reads the wait and not the
#     capability source.
#
# Each gate is its own concept over its own <hub>_required_row alias, so
# a pair for the family as a whole would leave the other gates with no
# witness.
crucible_neg_compile_perf_test(neg_lock_contention_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_lock_contention_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_pmu_sample_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sched_switch_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_sched_switch_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_sched_tp_btf_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sense_hub_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_sense_hub_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_syscall_latency_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_syscall_tp_btf_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_cold_init_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_perf_test(neg_sense_hub_v2_mint_bg_compile_ctx
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")

# 3 strengthening fixtures: SECOND mismatch class per mint = non-IsExecCtx.
# CtxFitsXMint = IsExecCtx<Ctx> ∧ row_contains_v<…, Init>; fixtures
# above exercise the row half (different Init-less ExecCtx shapes);
# fixtures below exercise the IsExecCtx half via raw struct rejection.
crucible_neg_compile_warden_test(neg_hardening_mint_not_exec_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHardeningMint")
crucible_neg_compile_warden_test(neg_hot_region_registry_mint_not_exec_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_warden_test(neg_deadline_watchdog_mint_not_exec_ctx
    "no matching function for call to .mint_deadline_watchdog\\(NotAnExecCtx, std::nullptr_t, crucible::warden::Policy&\\)."
    "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = NotAnExecCtx.")

# ─── cntp/dataplane/Xdp.h soundness gates ─────────────────────────
#
#   * neg_xdp_raw_program_spec — NIC admission requires source::Xdp
#     provenance, not a raw descriptor.
#   * neg_xdp_mint_bg_ctx — only Init-row contexts mint attach plans.
#   * neg_xdp_mint_not_exec_ctx — a bare struct is not a context.
#   * neg_xdp_map_non_pod_value — BPF map values must be verifier-safe POD.
#   * neg_xdp_map_oversized_value — a map element must fit the 16-bit
#     length field.
#   * neg_xdp_map_key_without_equality — in-process map images require typed
#     key equality, not bytewise accidental comparison.
crucible_neg_compile_dataplane_test(neg_xdp_raw_program_spec
    "could not convert .raw. from .crucible::cntp::dataplane::XdpProgramSpec. to .crucible::cntp::dataplane::DeclaredXdpProgram."
    "fixy::Tagged<crucible::cntp::dataplane::XdpProgramSpec, fixy::tags::source::Xdp>")
crucible_neg_compile_dataplane_test(neg_xdp_mint_bg_ctx
    "no matching function for call to .mint_xdp_program\\(fixy::BgDrainCtx&"
    "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>.")
crucible_neg_compile_dataplane_test(neg_xdp_mint_not_exec_ctx
    "no matching function for call to .mint_xdp_program\\(NotAnExecCtx"
    "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = NotAnExecCtx.")
crucible_neg_compile_dataplane_test(neg_xdp_map_non_pod_value
    "no matching function for call to .mint_bpf_map_spec<Key, std::string>"
    "required for the satisfaction of .BpfScalar<T>. .with T = std::__cxx11::basic_string<char")
crucible_neg_compile_dataplane_test(neg_xdp_map_oversized_value
    "no matching function for call to .mint_bpf_map_spec<Key, WideValue>"
    "requires \\(BpfMapElement<Key>\\) && \\(BpfMapElement<Value>\\)")
crucible_neg_compile_dataplane_test(neg_xdp_map_key_without_equality
    "template constraint failure for .template<class Key, class Value, unsigned int MaxEntries, crucible::cntp::dataplane::BpfMapKind Kind>"
    "required for the satisfaction of .BpfKey<Key>. .with Key = Key.")

# ─── cntp/dataplane/TcEbpf.h soundness gates ─────────────
#
#   * neg_tc_ebpf_raw_program_spec — NIC admission takes a declared plan.
#   * neg_tc_ebpf_mint_bg_ctx — only an Init-row context mints a plan.
#   * neg_tc_ebpf_mint_raw_ifindex — a plan takes an admitted index.
#   * neg_tc_ebpf_raw_flow_class — a map value is a declared flow class.
#   * neg_tc_ebpf_flow_class_raw_dscp / neg_tc_ebpf_flow_class_raw_classid
#     — a flow class takes admitted fields.
#   * neg_tc_ebpf_zero_flow_map — a map holds at least one flow.
crucible_neg_compile_dataplane_test(neg_tc_ebpf_raw_program_spec
    "could not convert .raw. from .crucible::cntp::dataplane::TcProgramSpec."
    "to .crucible::cntp::dataplane::DeclaredTcProgram. \\{aka .fixy::Tagged<crucible::cntp::dataplane::TcProgramSpec, fixy::tags::source::TcEbpf>.\\}")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_mint_bg_ctx
    "no matching function for call to .mint_tc_program\\(fixy::BgDrainCtx&, "
    "required for the satisfaction of .CtxFitsTcMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_mint_raw_ifindex
    "no matching function for call to .mint_tc_program\\(fixy::ColdInitCtx&, crucible::cntp::NicInterfaceName&, uint32_t, "
    "cannot convert .11. \\(type .uint32_t. \\{aka .unsigned int.\\}\\) to type .crucible::cntp::dataplane::TcIfIndex.")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_raw_flow_class
    "cannot convert .crucible::cntp::dataplane::TcFlowClass. to .crucible::cntp::dataplane::DeclaredTcFlowClass."
    "initializing argument 2 of .constexpr std::expected<void, crucible::cntp::dataplane::XdpError> crucible::cntp::dataplane::TcFlowClassMap<MaxFlows>::update\\(")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_flow_class_raw_dscp
    "could not convert .46. from .uint8_t. \\{aka .unsigned char.\\} to .crucible::cntp::dataplane::TcDscp."
    "fixy::Refinement<fixy::InRange<0, 63>\\(\\), unsigned char, false>")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_flow_class_raw_classid
    "could not convert .65537. from .uint32_t. \\{aka .unsigned int.\\} to .crucible::cntp::dataplane::TcClassId."
    "fixy::Refinement<fixy::IsPositive\\(\\), unsigned int, false>")
crucible_neg_compile_dataplane_test(neg_tc_ebpf_zero_flow_map
    "template constraint failure for .template<unsigned int MaxFlows>  requires  MaxFlows > 0 class crucible::cntp::dataplane::TcFlowClassMap."
    "In substitution of .template<unsigned int MaxFlows>  requires  MaxFlows > 0 class crucible::cntp::dataplane::TcFlowClassMap .with unsigned int MaxFlows = 0..")

# mimic::CogMimic comes only from mint_cog_mimic.  Each fixture reaches
# one gate:
#
#   * non_substrate: CtxFitsCogMimic refuses a kind that is not a Mimic
#     substrate, such as a power rail.
#   * ctx_row_missing and mint_hot_fg_ctx: the mint refuses a context
#     that owns neither Init nor Bg.
#   * aggregate_build, private_constructor and default_construct: no
#     constructor that a caller can reach binds an identity.
#   * bit_cast: the class is not trivially copyable, so no CogMimic is
#     built from bytes.
#   * temporary_identity: the mint refuses an identity that is a
#     temporary, which the CogMimic would outlive.
crucible_neg_compile_cog_test(neg_cog_mimic_non_substrate
    "no matching function for call to .allocate_cog_mimic_slot<crucible::cog::CogKind::PsuRail, fixy::ColdInitCtx>\\(\\)."
    "required for the satisfaction of .IsMimicSubstrate<K>. \\[with K = crucible::cog::CogKind::PsuRail\\]")
crucible_neg_compile_cog_test(neg_cog_mimic_ctx_row_missing
    "no matching function for call to .mint_cog_mimic<crucible::cog::CogKind::Gpu>\\(const fixy::TestRunnerCtx&,"
    "required for the satisfaction of .CtxFitsCogMimic<Ctx, K>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test,")
crucible_neg_compile_cog_test(neg_cog_mimic_mint_hot_fg_ctx
    "no matching function for call to .mint_cog_mimic<crucible::cog::CogKind::Gpu>\\(const fixy::HotFgCtx&,"
    "required for the satisfaction of .CtxFitsCogMimic<Ctx, K>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_cog_test(neg_cog_mimic_aggregate_build
    "no matching function for call to .crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>::CogMimic\\(<brace-enclosed initializer list>\\)."
    "no known conversion for argument 1 from .crucible::cog::CogIdentity\\*. to .const crucible::cog::CogIdentity&.")
crucible_neg_compile_cog_test(neg_cog_mimic_private_constructor
    "is private within this context"
    ".constexpr crucible::mimic::CogMimic<K>::CogMimic\\(const crucible::cog::CogIdentity&, CalibratedCaps, OpcodeTable\\) \\[with crucible::cog::CogKind K = crucible::cog::CogKind::Gpu.")
crucible_neg_compile_cog_test(neg_cog_mimic_default_construct
    "use of deleted function .crucible::mimic::CogMimic<K>::CogMimic\\(\\) \\[with crucible::cog::CogKind K = crucible::cog::CogKind::Gpu\\]."
    "a CogMimic binds a calibrated identity. take one from mint_cog_mimic")
crucible_neg_compile_cog_test(neg_cog_mimic_bit_cast
    "the expression .is_trivially_copyable_v<_To> \\[with _To = crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>\\]. evaluated to .false."
    ".crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>. is not trivially copyable")
crucible_neg_compile_cog_test(neg_cog_mimic_temporary_identity
    "use of deleted function .constexpr crucible::mimic::CogMimic<K> crucible::mimic::mint_cog_mimic\\(const Ctx&, const crucible::cog::CogIdentity&&,"
    "the CogMimic borrows its identity, so a temporary dangles at the end of the full expression. bind the identity to a name first")

# ─── topology::TopologyGraph ──────────────────────────────────────────
#
#   * neg_topology_graph_non_ctx_first_arg — an int in the context slot is
#     refused by the IsExecCtx constraint, not deep inside the factory.
#   * neg_topology_graph_test_ctx_refused — a test context carries no Init
#     row, so it cannot mint the fleet's topology graph.
crucible_neg_compile_topology_test(neg_topology_graph_non_ctx_first_arg
    "no matching function for call to .mint_topology_graph.int,"
    "IsExecCtx<Ctx>. \\[with Ctx = int\\]")
crucible_neg_compile_topology_test(neg_topology_graph_test_ctx_refused
    "no matching function for call to .mint_topology_graph.fixy::TestRunnerCtx&"
    "CtxFitsTopologyGraph<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test")

# ─── topology::Discovery ──────────────────────────────────────────────
#
#   * neg_topology_discovery_mint_test_ctx — a snapshot is built only from
#     an initialisation context; a test context is refused.
#   * neg_topology_discovery_direct_construct — the snapshot's constructor
#     is private, so the mint is the only door.
#   * neg_topology_discovery_raw_lspci_text — a parser takes only text
#     tagged as External, never a raw string.
#   * neg_topology_discovery_zero_nodes — a snapshot needs at least one node
#     slot and one edge slot.
crucible_neg_compile_topology_test(neg_topology_discovery_mint_test_ctx
    "no matching function for call to .mint_discovery_snapshot<1, 1>.fixy::TestRunnerCtx&"
    "CtxFitsDiscoveryInit<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test")
crucible_neg_compile_topology_test(neg_topology_discovery_direct_construct
    "is private within this context"
    "DiscoverySnapshot<MaxNodes, MaxEdges>::DiscoverySnapshot\\(\\)")
crucible_neg_compile_topology_test(neg_topology_discovery_raw_lspci_text
    "could not convert .\\(const char\\*\\)\"Slot:.* from .const char\\*. to .crucible::topology::ExternalDiscoveryText."
    "fixy::Tagged<std::basic_string_view<char>, fixy::tags::source::External>")
crucible_neg_compile_topology_test(neg_topology_discovery_zero_nodes
    "no matching function for call to .mint_discovery_snapshot<0, 1>.fixy::ColdInitCtx&"
    "required for the satisfaction of .DiscoveryShape<MaxNodes, MaxEdges>. \\[with MaxNodes = 0; MaxEdges = 1\\]")

# ─── topology::Health ─────────────────────────────────────────────────
#
#   * neg_topology_health_mint_bg_ctx — only Init-row contexts mint the
#     bounded health substrate; Bg may update supplied facts but may not
#     create the policy carrier.
#   * neg_topology_health_direct_construct — the scorer's constructor is
#     private, so the mint is the only door.
#   * neg_topology_health_update_fg_ctx — hot foreground contexts cannot
#     mutate heartbeat/thermal/ECC/drop/wear state.
#   * neg_topology_health_raw_score — quarantine thresholds consume the
#     HealthScore strong type, not raw integers.
crucible_neg_compile_topology_test(neg_topology_health_mint_bg_ctx
    "no matching function for call to .mint_topology_health<fixy::BgDrainCtx, 2>"
    "CtxFitsHealthMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_topology_test(neg_topology_health_direct_construct
    "is private within this context"
    "CompositeHealthScorer<MaxPeers, Window, MaxEvents>::CompositeHealthScorer\\(crucible::topology::HealthPolicy\\)")
crucible_neg_compile_topology_test(neg_topology_health_update_fg_ctx
    "no matching function for call to .crucible::topology::CompositeHealthScorer<2, 32, 8>::update_thermal"
    "CtxFitsHealthUpdate<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_topology_health_raw_score
    "could not convert .0. from .int. to .crucible::topology::HealthScore."
    "HealthScore")

# ─── topology::Telemetry ──────────────────────────────────────────────
#
#   * neg_topology_telemetry_mint_bg_ctx, _mint_hot_fg_ctx: the history
#     mint refuses a context whose row holds no Init.
#   * neg_topology_telemetry_record_fg_ctx, _record_init_ctx: a record
#     refuses a context whose row holds no Bg.
#   * neg_topology_telemetry_read_fg_ctx, _read_test_ctx: a read refuses a
#     context that owns neither Init nor Bg, because the history has no
#     lock.
#   * neg_topology_telemetry_window_overflow: a window that the uint16_t
#     indices cannot hold stops at a static assertion.
#   * neg_topology_telemetry_history_direct_construct,
#     _snapshot_direct_construct: each is reached only through its mint.
#   * neg_topology_telemetry_raw_netdev_text: the parser takes text under
#     the External tag, not a raw string.
#   * neg_topology_telemetry_snapshot_raw_netdev, _snapshot_raw_tcp: the
#     snapshot mint takes tagged samples, not raw ones.
crucible_neg_compile_topology_test(neg_topology_telemetry_mint_bg_ctx
    "no matching function for call to .mint_nic_telemetry_history<4>\\(fixy::BgDrainCtx\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_mint_hot_fg_ctx
    "no matching function for call to .mint_nic_telemetry_history<4>\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_record_fg_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::record\\(const fixy::HotFgCtx&, const crucible::topology::NicTelemetrySnapshot&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_record_init_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::record\\(fixy::ColdInitCtx&, const crucible::topology::NicTelemetrySnapshot&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_read_fg_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::current_snapshot\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRead<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_read_test_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::current_snapshot\\(fixy::TestRunnerCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRead<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >\\]")
crucible_neg_compile_topology_test(neg_topology_telemetry_window_overflow
    "static assertion failed: NicTelemetryHistory stores bounded history indices in uint16_t"
    "In instantiation of .class crucible::topology::NicTelemetryHistory<65536>.")
crucible_neg_compile_topology_test(neg_topology_telemetry_history_direct_construct
    ".constexpr crucible::topology::NicTelemetryHistory<Window>::NicTelemetryHistory\\(\\) \\[with long unsigned int Window = 4\\]. is private within this context"
    "topology/Telemetry.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_topology_test(neg_topology_telemetry_snapshot_direct_construct
    ".constexpr crucible::topology::NicTelemetrySnapshot::NicTelemetrySnapshot\\(\\). is private within this context"
    "topology/Telemetry.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_topology_test(neg_topology_telemetry_raw_netdev_text
    "could not convert .\\(const char\\*\\).rx_bytes: 1.012.. from .const char\\*."
    "to .crucible::topology::ExternalTelemetryText. \\{aka .fixy::Tagged<std::basic_string_view<char>, fixy::tags::source::External>.\\}")
crucible_neg_compile_topology_test(neg_topology_telemetry_snapshot_raw_netdev
    "could not convert .crucible::topology::NetdevCounters\\{0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\}. from .crucible::topology::NetdevCounters."
    "to .crucible::topology::DeclaredNetdevCounters. \\{aka .fixy::Tagged<crucible::topology::NetdevCounters, fixy::tags::source::KernelTelemetry>.\\}")
crucible_neg_compile_topology_test(neg_topology_telemetry_snapshot_raw_tcp
    "could not convert .raw. from .const crucible::topology::CongestionSample."
    "to .crucible::topology::TcpInfoSnapshot. \\{aka .fixy::Tagged<crucible::topology::CongestionSample, fixy::tags::source::TcpInfo>.\\}")

# ─── topology::AsymmetricFailure ──────────────────────────────────────
#
#   * neg_asymmetric_failure_mint_bg_ctx — only Init-row contexts mint
#     the bounded detector.
#   * neg_asymmetric_failure_direct_construct — the detector's constructor
#     is private, so the mint is the only door.
#   * neg_asymmetric_failure_record_fg_ctx — hot foreground contexts
#     cannot mutate probe/witness state.
#   * neg_asymmetric_failure_zero_window — a detector must carry a
#     non-empty bounded probe window.
crucible_neg_compile_topology_test(neg_asymmetric_failure_mint_bg_ctx
    "no matching function for call to .mint_asymmetric_failure_detector<fixy::BgDrainCtx, 2>"
    "CtxFitsAsymmetricFailureMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_topology_test(neg_asymmetric_failure_direct_construct
    "is private within this context"
    "AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>::AsymmetricFailureDetector\\(crucible::topology::AsymmetricFailurePolicy\\)")
crucible_neg_compile_topology_test(neg_asymmetric_failure_record_fg_ctx
    "no matching function for call to .crucible::topology::AsymmetricFailureDetector<2, 8, 8>::record_outbound"
    "CtxFitsAsymmetricFailureRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_asymmetric_failure_zero_window
    "static assertion failed: AsymmetricFailureDetector requires a non-empty probe window"
    "non-empty probe window")

# ─── topology::Pingmesh ───────────────────────────────────────────────
#
#   * neg_pingmesh_mint_bg_ctx — only an Init-row context mints the
#     all-pairs carrier.
#   * neg_pingmesh_direct_construct — the constructor is private, so the
#     mint is the only door.
#   * neg_pingmesh_enable_pair_bg_ctx — enabling a pair writes the mask
#     that recording reads, so it takes an Init-row context.
#   * neg_pingmesh_record_fg_ctx — the foreground context cannot change
#     the latency histograms.
#   * neg_pingmesh_raw_measurement — a raw probe outcome has no Pingmesh
#     source tag, so it cannot be recorded.
#   * neg_pingmesh_zero_peer_capacity — all-pairs storage needs at least
#     two peers.
crucible_neg_compile_topology_test(neg_pingmesh_mint_bg_ctx
    "no matching function for call to .mint_pingmesh<fixy::BgDrainCtx, 2>\\(fixy::BgDrainCtx\\)"
    "CtxFitsPingmeshMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_topology_test(neg_pingmesh_direct_construct
    "is private within this context"
    "Pingmesh<MaxPeers, Significant, MaxLatencyNs>::Pingmesh\\(crucible::topology::PingmeshConfig\\)")
crucible_neg_compile_topology_test(neg_pingmesh_enable_pair_bg_ctx
    "no matching function for call to .crucible::topology::Pingmesh<2>::enable_pair\\(fixy::BgDrainCtx, crucible::cog::Uuid, crucible::cog::Uuid\\)"
    "CtxFitsPingmeshMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_topology_test(neg_pingmesh_record_fg_ctx
    "no matching function for call to .crucible::topology::Pingmesh<2>::record_measurement\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
    "CtxFitsPingmeshRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_pingmesh_raw_measurement
    "no matching function for call to .crucible::topology::Pingmesh<2>::record_measurement\\(fixy::BgDrainCtx, crucible::topology::PingmeshMeasurement&\\)"
    "cannot convert .raw. \\(type .crucible::topology::PingmeshMeasurement.\\) to type .const crucible::topology::DeclaredPingmeshMeasurement&.")
crucible_neg_compile_topology_test(neg_pingmesh_zero_peer_capacity
    "static assertion failed: Pingmesh<MaxPeers> requires at least two peers"
    "In instantiation of .class crucible::topology::Pingmesh<1, 2, 60000000000>.")

# ─── topology::Ptp ────────────────────────────────────────────────────
#
#   * neg_ptp_mint_bg_ctx — only an Init-row context mints a PTP handle.
#   * neg_ptp_handle_direct_construct — the handle's constructor is
#     private, so the mint is the only door.
#   * neg_ptp_record_fg_ctx — the foreground context cannot record into a
#     handle.
#   * neg_ptp_raw_fd — a raw int does not name the clock of a handle.
#   * neg_ptp_raw_timestamp — a raw nanosecond count is not a PTP
#     timestamp.
#   * neg_ptp_open_raw_device_index — a raw int does not name /dev/ptpN.
#   * neg_ptp_open_ctx_lacks_block — opening /dev/ptpN needs a context
#     that admits IO and Block.
#   * neg_ptp_enable_raw_socket — a raw socket fd cannot program
#     timestamping.
#   * neg_ptp_raw_daemon_report_status — a status comes only from an
#     admitted daemon report.
#   * neg_ptp_daemon_report_fg_ctx — the foreground context cannot admit a
#     daemon report.
crucible_neg_compile_topology_test(neg_ptp_mint_bg_ctx
    "no matching function for call to .mint_ptp_handle\\(fixy::BgDrainCtx&,"
    "required for the satisfaction of .CtxFitsPtpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_topology_test(neg_ptp_handle_direct_construct
    "is private within this context"
    "crucible::topology::PtpHandle::PtpHandle\\(crucible::cog::CogIdentity, crucible::topology::PtpClockFd, const crucible::topology::PtpStatus&\\)")
crucible_neg_compile_topology_test(neg_ptp_record_fg_ctx
    "no matching function for call to .crucible::topology::PtpHandle::record_timestamp\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
    "required for the satisfaction of .CtxFitsPtpRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_topology_test(neg_ptp_raw_fd
    "no matching function for call to .mint_ptp_handle\\(fixy::ColdInitCtx&, crucible::cog::CogIdentity&, int\\)"
    "cannot convert .3. \\(type .int.\\) to type .crucible::topology::PtpClockFd.")
crucible_neg_compile_topology_test(neg_ptp_raw_timestamp
    "could not convert .42. from .unsigned int. to .crucible::topology::PtpTimestampNs."
    "fixy::Tagged<long unsigned int, fixy::tags::source::Ptp>")
crucible_neg_compile_topology_test(neg_ptp_open_raw_device_index
    "no matching function for call to .open_ptp_clock\\(foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&, int\\)"
    "cannot convert .0. \\(type .int.\\) to type .crucible::topology::PtpDeviceIndex.")
crucible_neg_compile_topology_test(neg_ptp_open_ctx_lacks_block
    "no matching function for call to .open_ptp_clock\\(fixy::BgDrainCtx&,"
    "required for the satisfaction of .CtxFitsPtpClockReaderMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_topology_test(neg_ptp_enable_raw_socket
    "could not convert .3. from .int. to .crucible::cntp::SocketFd."
    "fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>")
crucible_neg_compile_topology_test(neg_ptp_raw_daemon_report_status
    "invalid initialization of reference of type .const crucible::topology::DeclaredPtpDaemonReport&. .aka .const fixy::Tagged<crucible::topology::PtpDaemonReport, fixy::tags::source::Ptp>&.. from expression of type .crucible::topology::PtpDaemonReport."
    "in passing argument 1 of .constexpr crucible::topology::DeclaredPtpStatus crucible::topology::ptp_status_from_daemon_report")
crucible_neg_compile_topology_test(neg_ptp_daemon_report_fg_ctx
    "no matching function for call to .admit_ptp_daemon_report\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
    "required for the satisfaction of .CtxFitsPtpRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# Each perf program load, and the Senses aggregate over the seven of
# them, takes the startup load context as its one parameter.  A
# background load context also claims Block, but no conversion makes it
# a startup load context, and the parameter has no default.  The two
# fixtures of each load pin these two refusals.
crucible_neg_compile_effects_test(neg_perf_sense_hub_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SenseHub> crucible::perf::SenseHub::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_sense_hub_load_no_cap
    "no matching function for call to .crucible::perf::SenseHub::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_effects_test(neg_perf_sched_switch_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SchedSwitch> crucible::perf::SchedSwitch::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_sched_switch_load_no_cap
    "no matching function for call to .crucible::perf::SchedSwitch::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_effects_test(neg_perf_pmu_sample_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::PmuSample> crucible::perf::PmuSample::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_pmu_sample_load_no_cap
    "no matching function for call to .crucible::perf::PmuSample::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_effects_test(neg_perf_lock_contention_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::LockContention> crucible::perf::LockContention::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_lock_contention_load_no_cap
    "no matching function for call to .crucible::perf::LockContention::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_effects_test(neg_perf_syscall_latency_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SyscallLatency> crucible::perf::SyscallLatency::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_syscall_latency_load_no_cap
    "no matching function for call to .crucible::perf::SyscallLatency::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_effects_test(neg_perf_senses_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static crucible::perf::Senses crucible::perf::Senses::load_all\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_senses_load_no_cap
    "no matching function for call to .crucible::perf::Senses::load_all\\(\\)."
    "candidate expects 1 argument, 0 provided")
crucible_neg_compile_effects_test(neg_perf_sched_tp_btf_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SchedTpBtf> crucible::perf::SchedTpBtf::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_sched_tp_btf_load_no_cap
    "no matching function for call to .crucible::perf::SchedTpBtf::load\\(\\)."
    "candidate expects 1 argument, 0 provided")
crucible_neg_compile_effects_test(neg_perf_syscall_tp_btf_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SyscallTpBtf> crucible::perf::SyscallTpBtf::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_effects_test(neg_perf_syscall_tp_btf_load_no_cap
    "no matching function for call to .crucible::perf::SyscallTpBtf::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

# The constructor of warden::DeadlineWatchdog is private, and
# mint_deadline_watchdog is its one route.  One fixture builds a
# watchdog with the constructor, and one calls the mint with no context.
crucible_neg_compile_effects_test(neg_warden_deadline_watchdog_constructor_private
    "is private within this context"
    ".constexpr crucible::warden::DeadlineWatchdog::DeadlineWatchdog\\(const crucible::perf::Senses\\*, const crucible::warden::Policy&\\). is private")
crucible_neg_compile_effects_test(neg_warden_deadline_watchdog_mint_no_ctx
    "no matching function for call to .mint_deadline_watchdog\\(std::nullptr_t, crucible::warden::Policy&\\)."
    "candidate expects 3 arguments, 2 provided")

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
# Most patterns below are FRAMEWORK-CONTROLLED.  The GCC-WRAPPER-TEXT
# patterns are neg_schema_register_with_sealed_view,
# neg_ckernel_register_with_sealed_view and neg_cipher_wrong_tag.  Each
# is an overload-set rejection in production code, and each has an
# annotated comment below.

# Two companions of test_row_hash_distinctness.  Each fixture holds a
# copy of one consteval gate of that test and must not compile:
# neg_row_hash_collision holds two equal hashes, and
# neg_row_hash_sentinel holds the reserved value 0.
crucible_neg_compile_test(neg_row_hash_collision
  "row_hash collision")
crucible_neg_compile_test(neg_row_hash_sentinel
  "EMPTY-slot sentinel")

# WRAP-Types-1 (#1067) — ElementBytes::value_ wraps Refined<bounded_above<16>,
# uint8_t>; constructing ElementBytes{17} in constexpr context drives the
# bounded_above<16>(17) contract assertion through constant evaluation.  The
# violation is rejected as a non-constant expression (P1494R5 + P2900R14),
# pinning the structural bound at the storage type rather than relying on
# the source dtype enum being well-formed.
crucible_neg_compile_test(neg_element_bytes_above_max
  "contract|bounded_above|not a constant|non-constant|constexpr")

# IterationDetector::match_pos_ is the fixy Refined<bounded_above<
# MATCH_POS_MAX = K-1 = 4>, uint8_t>, and mint_refined<kMatchPosBound> is
# its one door.  Minting a value above 4 in a constant evaluation fails the
# door's precondition.  Two fixtures cover distinct forbidden values:
#   * _above_max — boundary edge (5 = K = smallest forbidden), catches
#     off-by-one drift in MATCH_POS_MAX.
#   * _uint8_max — wide miss (255 = uint8_t max), catches a wider
#     intermediate truncated to uint8_t before the mint.
# Each pins the door, the predicate and the refused value.
crucible_neg_compile_multi_test(neg_iter_det_match_pos_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4>\\(\\), unsigned char>\\(5\\)")
crucible_neg_compile_multi_test(neg_iter_det_match_pos_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4>\\(\\), unsigned char>\\(255\\)")

# CallSiteTable::Lineno is fixy::NonNegative<int32_t>, and the checked
# mint is its only door.  The two fixtures give the mint -1, the largest
# refused value, and the smallest int32_t.  The second regex names the
# mint, its predicate and the refused value.
crucible_neg_compile_multi_test(neg_call_site_lineno_negative
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonNegative\\(\\), int>\\(-1\\)")
crucible_neg_compile_multi_test(neg_call_site_lineno_int_min
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonNegative\\(\\), int>\\(std::numeric_limits<int>::min\\(\\)\\)")

# CallSiteTable::Entry stores its filename and funcname as InternedName,
# a fixy::Tagged<std::string, source::Interned>.  The table keeps one copy
# for each hash and runs no sanitizer, so a stored name claims neither the
# tag of its caller nor Sanitized.  Three fixtures:
#   * _filename_raw_string: an Entry cannot hold a raw std::string, since
#     the constructor of Tagged is private and mint_tagged is its door.
#   * _filename_external_passback: a stored name does not convert to an
#     External name.
#   * _interned_name_to_sanitized: no retag edge leaves Interned, so a
#     stored name, which can hold External bytes, never becomes Sanitized.
crucible_neg_compile_multi_test(neg_call_site_filename_raw_string
  "could not convert .* to .crucible::CallSiteTable::InternedName. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::Interned>.\\}"
  "fixy/Tagged.h:[0-9]+:[0-9]+: note: explicit conversion function was not considered")
crucible_neg_compile_multi_test(neg_call_site_filename_external_passback
  "invalid initialization of reference of type .const ExternalName&. .*from expression of type .const crucible::CallSiteTable::InternedName."
  "in passing argument 1 of .void wants_external_only\\(const ExternalName&\\).")
crucible_neg_compile_multi_test(neg_call_site_interned_name_to_sanitized
  "no matching function for call to .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::Interned>::retag<fixy::tags::source::Sanitized>\\(\\)."
  "required for the satisfaction of .RetagAllowed<Tag, NewTag>. \\[with Tag = fixy::tags::source::Interned. NewTag = fixy::tags::source::Sanitized\\]")

# IterationDetector::SignatureLen is the fixy BoundedMonotonic<uint32_t,
# K = 5>.  Its constructor, reached only through mint_bounded_monotonic,
# carries pre(!(T(Max) < initial)), and a value above K fails it in a
# constant evaluation.  Two fixtures:
#   * _above_k     — boundary edge (6 = K+1), catches off-by-one drift
#     in the bound.
#   * _uint32_max  — wide miss (UINT32_MAX), catches a regression that
#     replaces BoundedMonotonic with plain Monotonic and loses the bound.
# The diagnostic names the constructor's precondition in Mutation.h and
# not the value, so each fixture pins the predicate site.
crucible_neg_compile_multi_test(neg_iter_det_signature_len_above_k
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")
crucible_neg_compile_multi_test(neg_iter_det_signature_len_uint32_max
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")

# IterationDetector::OpsSinceBoundary is the fixy Monotonic<uint32_t>.
# advance(smaller) and bump() at numeric_limits::max() fail the two
# mutation preconditions (monotonicity and overflow) in a constant
# evaluation.  Two fixtures:
#   * _regression — boundary edge (advance(5) from 10), catches a
#     monotonicity predicate that weakens.
#   * _overflow   — wide miss (bump() at UINT32_MAX), catches a bump()
#     that loses its != max() check and wraps to 0.
# Each pins the mutation that failed and the value it was given.
crucible_neg_compile_multi_test(neg_iter_det_ops_since_boundary_regression
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int, std::less<unsigned int> >::advance\\(5\\)")
crucible_neg_compile_multi_test(neg_iter_det_ops_since_boundary_overflow
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int, std::less<unsigned int> >::bump\\(\\)")

# The iter_det_state::{Building, Steady} tags and their view_ok overloads
# give the detector's phase a type, so a caller can carry a witness of it.
# Two fixtures, distinct mismatch classes:
#   * _view_steady_on_building — the value-level precondition of
#     ::fixy::mint_view on a Building detector at constant evaluation.
#     Catches a Steady overload that admits a signature shorter than K,
#     or a caller that mints the view before the detector left Building.
#   * _view_in_field — the structural audit no_scoped_view_field_check
#     on a class that holds a ScopedView<IterationDetector, Steady> as a
#     member.  Catches a container shape the recursive unwrap misses, or
#     a refactor that adds a cached view field.
crucible_neg_compile_multi_test(neg_iter_det_view_steady_on_building
  "contract predicate is false in constant expression"
  "fixy/ScopedView.h:[0-9]+:[0-9]+: error: contract predicate")
crucible_neg_compile_multi_test(neg_iter_det_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "contains_scoped_view<OffendingContainer>\\(\\)")

# RegionNode::VariantCounter is the fixy Monotonic<uint32_t>: the active
# variant moves from 0 to some non-zero id and only forward after that.
# Two fixtures, the same shape as the detector's counter above:
#   * _regression — boundary edge (current = 10, advance(5)).  Catches a
#     monotonicity predicate that weakens and admits a downgrade.
#   * _overflow   — wide miss (bump() at UINT32_MAX).  Catches a bump()
#     that loses its != max() check and wraps to 0, which would claim
#     that no variant is selected.
# Each pins the mutation that failed and the value it was given.
crucible_neg_compile_multi_test(neg_merkle_dag_variant_id_regression
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int, std::less<unsigned int> >::advance\\(5\\)")
crucible_neg_compile_multi_test(neg_merkle_dag_variant_id_overflow
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int, std::less<unsigned int> >::bump\\(\\)")

# ValidCKernelIdRaw is Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>.
# It is the gate at each widening of a uint8_t to a CKernelId.  The door
# mint_refined must refuse the NUM_KERNELS sentinel (the edge of the
# bound) and UINT8_MAX (the wide miss).  The first regex says that the
# precondition of the door failed.  The second names the door and the
# refused value.
crucible_neg_compile_multi_test(neg_ckernel_id_raw_at_sentinel
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<[0-9]+>\\(\\), unsigned char>\\(\\(\\(uint8_t\\)crucible::CKernelId::NUM_KERNELS\\)\\)")
crucible_neg_compile_multi_test(neg_ckernel_id_raw_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<[0-9]+>\\(\\), unsigned char>\\(255\\)")

# CKernelTable::SizeCounter is BoundedMonotonic<uint32_t,
# CKERNEL_TABLE_CAP>.  The door mint_bounded_monotonic must refuse CAP + 1
# (the edge of the bound) and UINT32_MAX (the wide miss).  GCC names
# neither the door nor the value when a precondition of a constructor
# fails in a constant expression.  So the second regex pins the header
# that holds the precondition.
crucible_neg_compile_multi_test(neg_ckernel_table_size_above_cap
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")
crucible_neg_compile_multi_test(neg_ckernel_table_size_uint32_max
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")

# The global CKernelTable carries source::Singleton provenance.  A raw
# pointer and a pointer with a different tag must not satisfy a consumer
# of the singleton.
crucible_neg_compile_test(neg_ckernel_singleton_raw_pointer
  "could not convert|cannot convert|no matching function")
crucible_neg_compile_multi_test(neg_ckernel_singleton_cross_tag
  "could not convert .external. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Singleton>.")

# SchemaTable::SizeCounter is BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>,
# and the door mint_bounded_monotonic refuses a value past the bound in
# constant evaluation.  The check in register_name is a second defence.
#   * _above_cap: the edge of the bound (CAP + 1).  It fails when the bound
#     widens past SCHEMA_TABLE_CAP.
#   * _uint32_max: the wide miss (UINT32_MAX).  It fails when the counter
#     becomes a plain Monotonic<uint32_t>.
crucible_neg_compile_multi_test(neg_schema_table_size_above_cap
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")
crucible_neg_compile_multi_test(neg_schema_table_size_uint32_max
  "contract predicate is false in constant expression"
  "fixy/Mutation.h:[0-9]+:[0-9]+: error: contract predicate")

# SchemaTable::lookup() returns
# LookupName = Tagged<Borrowed<const char, SchemaTable>, source::Interned>.
#   * neg_schema_table_lookup_raw_const_char: the typed borrow does not
#     convert to a raw const char* without .value().data().
#   * neg_schema_table_lookup_cross_tag: interned table output does not pass
#     as source::External.
crucible_neg_compile_multi_test(neg_schema_table_lookup_raw_const_char
  "cannot convert .crucible::SchemaTable::LookupName. \\{aka .fixy::Tagged<fixy::Borrowed<const char, crucible::SchemaTable>, fixy::tags::source::Interned>.\\} to .const char\\*. in initialization"
  "neg_schema_table_lookup_raw_const_char.cpp:18")
crucible_neg_compile_multi_test(neg_schema_table_lookup_cross_tag
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Interned>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. requested"
  "neg_schema_table_lookup_cross_tag.cpp:23")

# The schema table admits a name only when its tag satisfies
# SchemaNameSource: Sanitized (a boundary check retagged it) or
# FromInternal (it crossed no boundary).
#   * neg_schema_register_external_name: a trace-file name that the loader
#     did not retag is refused at register_name.
#   * neg_schema_register_abi_boundary_name: a C ABI name that the entry did
#     not retag is refused at register_schema_name.
crucible_neg_compile_multi_test(neg_schema_register_external_name
  "neg_schema_register_external_name.cpp:24:31: error: no matching function for call to .crucible::SchemaTable::register_name"
  "required for the satisfaction of .SchemaNameSource<Tag>. \\[with Tag = fixy::tags::source::External\\]")
crucible_neg_compile_multi_test(neg_schema_register_abi_boundary_name
  "neg_schema_register_abi_boundary_name.cpp:22:42: error: no matching function for call to .register_schema_name"
  "required for the satisfaction of .SchemaNameSource<Tag>. \\[with Tag = fixy::tags::source::ABIBoundary\\]")

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

# The ring storage of TransactionLog<N> is fixy::CyclicBuffer<Transaction,
# N>, and two gates hold the positive power-of-two capacity:
#   * _non_power_of_two (N = 7): the class-body static_assert and the
#     requires-clause of CyclicBuffer both refuse it.  A masked cursor over
#     a non-power-of-two ring aliases two logical positions onto one slot.
#   * _zero_capacity (N = 0): the static_assert admits it, because
#     `0 & (0u - 1u)` is 0, and only `requires (N > 0 ...)` of CyclicBuffer
#     refuses it.
# The bound of the fill counter lives inside CyclicBuffer.
crucible_neg_compile_multi_test(neg_transaction_log_non_power_of_two
  "Transaction.h:[0-9]+:[0-9]+: error: static assertion failed: N must be a power of 2"
  "the expression .\\(N & N - 1\\) == 0 \\[with N = 7\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_transaction_log_zero_capacity
  "Transaction.h:[0-9]+:[0-9]+: error: template constraint failure for .template<class T, long unsigned int N>  requires  N > 0"
  "the expression .N > 0 \\[with N = 0\\]. evaluated to .false.")

# TransactionLog::commit takes the region as
# fixy::Tagged<RegionNode*, source::Arena>.  A raw RegionNode* and a region
# under the Loaded tag, which deserialize_region returns, both fail at the
# commit boundary.
crucible_neg_compile_multi_test(neg_transaction_region_raw_pointer
  "neg_transaction_region_raw_pointer.cpp:31:33: error: cannot convert .crucible::RegionNode\\*. to .crucible::Transaction::ArenaRegion."
  "\\{aka .fixy::Tagged<crucible::RegionNode\\*, fixy::tags::source::Arena>.\\}")
crucible_neg_compile_multi_test(neg_transaction_region_loaded_cross_tag
  "neg_transaction_region_loaded_cross_tag.cpp:33:33: error: cannot convert"
  ".Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Loaded>. to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Arena>.")

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

# Transaction::step_id is fixy::Monotonic<uint64_t>.
#   * neg_transaction_step_id_assign_from_raw: Monotonic has no assignment
#     from uint64_t, so a raw write does not compile.
#   * neg_transaction_step_id_advance_retrograde: an advance to a smaller
#     value breaks the precondition of advance in a constant evaluation.
crucible_neg_compile_multi_test(neg_transaction_step_id_assign_from_raw
  "neg_transaction_step_id_assign_from_raw.cpp:19:29: error: no match for .operator=."
  "\\(operand types are .fixy::Monotonic<long unsigned int, std::less<long unsigned int> >. and .uint64_t.")
crucible_neg_compile_multi_test(neg_transaction_step_id_advance_retrograde
  "in .constexpr. expansion of .tx.crucible::Transaction::step_id.fixy::Monotonic<long unsigned int, std::less<long unsigned int> >::advance\\(50\\)."
  "error: .__builtin_trap\\(\\). is not a constant expression")

# The log holds a monotonic clock reader, so CtxFitsTransactionLog admits
# only a context whose row claims Bg, Init or Test.
#   * neg_transaction_log_foreground_ctx: the foreground context.
#   * neg_transaction_log_bg_cap_without_bg_row: a background capability
#     with a row that claims Alloc alone.
crucible_neg_compile_multi_test(neg_transaction_log_foreground_ctx
  "neg_transaction_log_foreground_ctx.cpp:24:89: error: no matching function for call to .crucible::TransactionLog<16, Owner>::TransactionLog"
  "required for the satisfaction of .CtxFitsTransactionLog<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_multi_test(neg_transaction_log_bg_cap_without_bg_row
  "neg_transaction_log_bg_cap_without_bg_row.cpp:29:55: error: no matching function for call to .crucible::TransactionLog<16, Owner>::TransactionLog"
  "required for the satisfaction of .CtxFitsTransactionLog<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >\\]")

# The transaction log has one owner.  Each member takes the owner proof,
# a call with no proof does not compile, and a proof that could travel to
# a second thread is refused as an owner.  The publish-stage proof is
# built by the background thread alone, and neither bit_cast nor the
# checked lifetime start can make one from bytes.
crucible_neg_compile_multi_test(neg_transaction_log_rollback_without_owner
  "neg_transaction_log_rollback_without_owner.cpp:26:23: error: no matching function for call to .crucible::TransactionLog<16, Owner>::rollback\\(\\)."
  "candidate expects 1 argument, 0 provided")
crucible_neg_compile_multi_test(neg_transaction_log_copyable_owner
  "neg_transaction_log_copyable_owner.cpp:16:47: error: template constraint failure for .template<unsigned int N, class Owner>  requires  OwnerProof<Owner> class crucible::TransactionLog."
  "the expression .!\\(is_copy_constructible_v<P>\\) \\[with P = CopyableOwner\\]. evaluated to .false.")
crucible_neg_compile_test(neg_publish_stage_built_outside
  "PublishStage\\(\\)[^\\n]*private within this context|is private within this context")
crucible_neg_compile_test(neg_publish_stage_forged_by_bit_cast
  "bit_cast|trivially copyable|no matching function")
crucible_neg_compile_test(neg_publish_stage_forged_by_lifetime_start
  "ImplicitLifetimeThroughout|constraints not satisfied")

# ValidTraceNodeKindRaw is the fixy Refined<bounded_above<TERMINAL>,
# uint8_t>, minted through kValidTraceNodeKindBound at every uint8_t to
# TraceNodeKind widening site (Serialize::read_header is the canonical
# one).  Two fixtures, distinct mismatch classes:
#   * neg_trace_node_kind_above_terminal — boundary edge (= TERMINAL + 1).
#     Catches a bound that widens past TERMINAL and admits a byte that
#     names no enumerator.
#   * neg_trace_node_kind_uint8_max — wide miss (= UINT8_MAX).  Catches a
#     refinement that degenerates into a plain uint8_t.
# Each pins the door, the bound and the refused value.
crucible_neg_compile_multi_test(neg_trace_node_kind_above_terminal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<3>\\(\\), unsigned char>\\(\\(\\(uint8_t\\)\\(\\(\\(int\\)\\(\\(uint8_t\\)crucible::TraceNodeKind::TERMINAL\\)\\) \\+ 1\\)\\)\\)")
crucible_neg_compile_multi_test(neg_trace_node_kind_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<3>\\(\\), unsigned char>\\(255\\)")

# CDAG_VERSION is a fixy::Tagged<uint32_t, source::FormatVersion>.
# Disk-read header versions enter as source::External and are compared
# with the in-process format constant before the wire body is trusted.
# The two fixtures pin both escape directions.
#
#   * neg_cdag_version_raw_uint32.cpp — read-side raw extraction
#     rejected; byte writers must spell `.value()` at the boundary.
#   * neg_cdag_version_external_cross_tag.cpp — cross-tag assignment
#     rejected; source::FormatVersion cannot masquerade as
#     source::External.
crucible_neg_compile_multi_test(neg_cdag_version_raw_uint32
  "cannot convert .const crucible::CdagFormatVersion. \\{aka .const fixy::Tagged<unsigned int, fixy::tags::source::FormatVersion>.\\} to .uint32_t."
  "neg_cdag_version_raw_uint32.cpp:20")
crucible_neg_compile_multi_test(neg_cdag_version_external_cross_tag
  "conversion from .Tagged<\\[...\\],fixy::tags::source::FormatVersion>. to non-scalar type .Tagged<\\[...\\],fixy::tags::source::External>. requested"
  "neg_cdag_version_external_cross_tag.cpp:24")

# deserialize_region returns std::optional<LoadedRegionNode>, a region
# under the earned source::Loaded tag, reached on the Replayed to
# Loaded edge after the fold of the operations matches the recorded
# content hash.  The two fixtures pin the two escape directions.
#
#   * neg_deserialize_region_raw_region_ptr.cpp — the result converts
#     to no raw RegionNode*.
#   * neg_deserialize_region_cross_tag.cpp — Loaded provenance cannot
#     pass as source::External.
crucible_neg_compile_multi_test(neg_deserialize_region_raw_region_ptr
  "cannot convert .std::optional<fixy::Tagged<crucible::RegionNode\\*, fixy::tags::source::Loaded> >. to .crucible::RegionNode\\*. in initialization"
  "neg_deserialize_region_raw_region_ptr.cpp:21")
crucible_neg_compile_multi_test(neg_deserialize_region_cross_tag
  "conversion from .Tagged<\\[...\\],fixy::tags::source::Loaded>. to non-scalar type .Tagged<\\[...\\],fixy::tags::source::External>. requested"
  "neg_deserialize_region_cross_tag.cpp:25")

# kSchemaNameLenRange is the typed gate of a .crtrace schema-name
# length, in_range<1, 256>.  load_trace takes each length only through
# its mint.  The name feeds a 257-byte stack buffer, so a length outside
# [1, 256] is either an empty-name entry in the global schema table
# (lower) or a stack-buffer overflow (upper).  Two fixtures:
#   * neg_schema_name_len_below_min.cpp — lower-bound edge (= 0).
#     Catches a range that widens to in_range<0, 256>.
#   * neg_schema_name_len_above_max.cpp — wide miss (= UINT16_MAX).
#     Catches a gate that no longer runs its predicate.
crucible_neg_compile_multi_test(neg_schema_name_len_below_min
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 256>\\(\\), short unsigned int>\\(0\\)")
crucible_neg_compile_multi_test(neg_schema_name_len_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 256>\\(\\), short unsigned int>\\(65535\\)")

# The three header counts of a .crtrace file each have a typed gate:
# kTraceNumOpsBound (MAX_OPS = 1 << 22), kTraceNumMetasBound
# (MAX_METAS = 1 << 24) and kTraceNumNamesBound (SCHEMA_TABLE_CAP).
# load_trace takes each count only through the mint of its gate.
# Without the gates a corrupted header could request multi-GB
# allocations, or drive the schema-name loop past the fixed entries
# of the schema table.  Each gate has two fixtures: a boundary edge
# (cap + 1) that catches a bound that widens by one, and a wide miss
# (UINT32_MAX) that catches a gate that no longer runs its predicate.
crucible_neg_compile_multi_test(neg_trace_num_ops_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4194304>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::MAX_OPS\\) \\+ 1\\)\\)")
crucible_neg_compile_multi_test(neg_trace_num_ops_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4194304>\\(\\), unsigned int>\\(4294967295\\)")
crucible_neg_compile_multi_test(neg_trace_num_metas_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<16777216>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::MAX_METAS\\) \\+ 1\\)\\)")
crucible_neg_compile_multi_test(neg_trace_num_metas_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<16777216>\\(\\), unsigned int>\\(4294967295\\)")
crucible_neg_compile_multi_test(neg_trace_num_names_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<512>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::SCHEMA_TABLE_CAP\\) \\+ 1\\)\\)")
crucible_neg_compile_multi_test(neg_trace_num_names_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<512>\\(\\), unsigned int>\\(4294967295\\)")

# WRAP-TraceGraph-3 (#1044) — TraceGraph build counters are
# WriteOnce<uint32_t>.  The fixtures pin both soundness directions:
# producers cannot raw-assign a counter, and consumers cannot read one
# through an implicit uint32_t conversion.
crucible_neg_compile_test(neg_trace_graph_num_ops_assign_raw
  "no match for|no known conversion")
crucible_neg_compile_test(neg_trace_graph_num_edges_implicit_raw
  "cannot convert|invalid conversion|no viable conversion")

# ─── ValidMetaAppendCount negative-compile fixtures ──────────────────
#
# The count that MetaLog::try_append takes has a Refined bound at
# MetaLog::CAPACITY (1 << 20 = 1,048,576 records, 168 MB).
# An append of more than CAPACITY records can never succeed, because one
# batch larger than the whole ring has no place to go.  The alias
# ValidMetaAppendCount holds the bound in its type.  mint_refined is its
# one checked door, and in a constant evaluation a count above the bound
# makes the check trap.
#
# Each fixture mints through the predicate and the value type of the
# alias itself, and each regex names the value that the mint refused:
#   * neg_metalog_append_count_above_max is the boundary edge
#     (CAPACITY + 1).  It fails if the bound widens by one or more.
#   * neg_metalog_append_count_uint32_max is the wide miss (UINT32_MAX).
#     It fails if the bound is removed.
crucible_neg_compile_multi_test(neg_metalog_append_count_above_max
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<1048576>\\(\\), unsigned int>\\(\\(\\(\\(unsigned int\\)crucible::MetaLog::CAPACITY\\) \\+ 1\\)\\)."
    "__builtin_trap\\(\\). is not a constant expression"
    "in expansion of macro .CRUCIBLE_PRE.")
crucible_neg_compile_multi_test(neg_metalog_append_count_uint32_max
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<1048576>\\(\\), unsigned int>\\(4294967295\\)."
    "__builtin_trap\\(\\). is not a constant expression"
    "in expansion of macro .CRUCIBLE_PRE.")

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
# ValidNDim is ::fixy::Refined<::fixy::bounded_above<kMaxTensorNDim>,
# uint8_t>.  Its checked mint refuses any byte past 8: CRUCIBLE_PRE traps
# in constant evaluation, and read_meta substitutes a valid byte and
# fails the read at run time.
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

# read_meta dtype gate (sibling of #534 ndim / #892 kernel_id) — ScalarType
# dtype byte typed gate at the read_meta()
# deserialize boundary.  ScalarType is a SPARSE int8_t enum; a corrupt or
# version-skewed Cipher byte outside the enumerator set reached
# element_size()'s `default: std::unreachable()` as UB.  ValidScalarType is
# ::fixy::Refined<valid_scalar_type, int8_t>; its checked mint refuses an
# invalid byte.
# Two fixtures per HS14, distinct mismatch classes:
#   * neg_tensor_meta_dtype_gap_value — INTERIOR gap (14, between Bool=11
#     and BFloat16=15) catches "predicate relaxed to a plain range check
#     [-1,26]" drift that would admit the 12..14 / 16..22 gaps.
#   * neg_tensor_meta_dtype_above_max — ABOVE-MAX wide miss (99 > 26)
#     catches "drop the predicate entirely" regression.
crucible_neg_compile_test(neg_tensor_meta_dtype_gap_value
  "contract|Refined|valid_scalar_type|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_tensor_meta_dtype_above_max
  "contract|Refined|valid_scalar_type|not a constant|non-constant|constexpr")

# read_meta device_type gate (sibling of the dtype gate) — DeviceType is a
# sparse int8_t enum (0,1,2,6,9,13,14,20); it feeds the node content hash
# (Merkle identity), so a corrupt/version-skewed Cipher byte silently
# corrupts that identity.  ValidDeviceType = Refined<valid_device_type,
# int8_t> fail-closes at deserialize entry.  NOT a UB fix (DeviceType has
# no std::unreachable consumer).  Two fixtures per HS14, distinct classes:
#   * neg_tensor_meta_device_type_gap_value — INTERIOR gap (3, between
#     MKLDNN=2 and HIP=6) catches "predicate relaxed to a plain [0,20]
#     range check" drift admitting the sparse gaps.
#   * neg_tensor_meta_device_type_above_max — ABOVE-MAX wide miss (99 > 20)
#     catches "drop the predicate entirely" regression.
crucible_neg_compile_test(neg_tensor_meta_device_type_gap_value
  "contract|Refined|valid_device_type|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_tensor_meta_device_type_above_max
  "contract|Refined|valid_device_type|not a constant|non-constant|constexpr")

# read_meta layout gate (last enum reconstruction in read_meta) — Layout is
# a DENSE signed int8_t enum (Strided=0 .. SparseBsc=5); it feeds the node
# content hash (Merkle identity), so a corrupt/version-skewed Cipher byte
# silently corrupts that identity.  ValidLayout = Refined<valid_layout,
# int8_t> fail-closes.  NOT a UB fix (Layout has no std::unreachable
# consumer).  Two fixtures per HS14, distinct classes:
#   * neg_tensor_meta_layout_below_min — BELOW-MIN (-1) catches the
#     signed-lower-bound bug where bounded_above<5> would accept negatives.
#   * neg_tensor_meta_layout_above_max — ABOVE-MAX wide miss (99 > 5)
#     catches "drop the predicate entirely" regression.
crucible_neg_compile_test(neg_tensor_meta_layout_below_min
  "contract|Refined|valid_layout|not a constant|non-constant|constexpr")
crucible_neg_compile_test(neg_tensor_meta_layout_above_max
  "contract|Refined|valid_layout|not a constant|non-constant|constexpr")

# write_meta writes the data pointer and the gradient-function hash as
# zero.  Each zero is a constexpr fixy::Refined<is_zero, uint64_t>, so
# an edit that feeds the live field in does not compile, where it would
# otherwise break DetSafe bit-stable replay.  Two fixtures:
#   * neg_serialize_is_zero_nonzero_construct — boundary edge (v == 1)
#     catches a predicate relaxed to admit every non-negative value.
#   * neg_serialize_is_zero_uint64_max — wide miss (UINT64_MAX)
#     catches a refinement that no longer runs its predicate, so that
#     a uint64_t-encoded process-local address could reach the on-disk
#     bytes.
crucible_neg_compile_multi_test(neg_serialize_is_zero_nonzero_construct
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsZero\\(\\), long unsigned int>\\(1\\)")
crucible_neg_compile_multi_test(neg_serialize_is_zero_uint64_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsZero\\(\\), long unsigned int>\\(18446744073709551615\\)")

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

# dim_hash_*_det returns the fixy DetSafe<Pure, Tagged<uint64_t,
# hash_family::FamilyB>>.  Two fixtures cover both fences:
#   * neg_dim_hash_raw_uint64 — raw uint64_t cannot enter the typed
#     deterministic Family-B carrier.
#   * neg_dim_hash_cross_family — Family-A values cannot substitute
#     for process-local Family-B dim hashes.
crucible_neg_compile_multi_test(neg_dim_hash_raw_uint64
  "could not convert .raw. from .const uint64_t."
  "to .crucible::DimHashDet.")
crucible_neg_compile_multi_test(neg_dim_hash_cross_family
  "could not convert .persistent. from .Graded<\\[...\\],\\[...\\],Tagged<\\[...\\],crucible::hash_family::FamilyA>>."
  "to .Graded<\\[...\\],\\[...\\],Tagged<\\[...\\],crucible::hash_family::FamilyB>>.")

# observe/HdrHistogram.h keeps the recording boundary typed and the merge
# geometry exact:
#   * neg_hdr_histogram_record_raw_uint64 — raw external samples cannot
#     bypass fixy::Refined<in_range<0, MaxValue>, uint64_t>.
#   * neg_hdr_histogram_merge_cross_config — histograms with different
#     significant-digit layouts cannot be merged.
crucible_neg_compile_test(neg_hdr_histogram_record_raw_uint64
  "cannot convert|no matching function|could not convert|Refined|in_range|value_type")
crucible_neg_compile_test(neg_hdr_histogram_merge_cross_config
  "cannot convert|no matching function|could not convert|HdrHistogram")

# observe/SyntheticProbe.h keeps synthetic-probe publication on the
# background row and transport registration typed:
#   * neg_synthetic_probe_record_fg_ctx — HotFgCtx cannot record probe
#     outcomes because it does not admit Row<Bg>.
#   * neg_synthetic_probe_raw_kind_mask — raw integer masks cannot
#     substitute for fixy::Bits<TransportProbeKind>.
crucible_neg_compile_test(neg_synthetic_probe_record_fg_ctx
  "constraint|constraints not satisfied|CtxFitsSyntheticProbeRecord|record_gate")
crucible_neg_compile_test(neg_synthetic_probe_raw_kind_mask
  "cannot convert|no matching function|could not convert|Bits|TransportProbeKind")

# observe/SdcDetect.h keeps redundant-compute evidence typed:
#   * neg_sdc_detect_mint_bg_ctx — only Init-row contexts mint detectors.
#   * neg_sdc_detect_run_hot_ctx — hot foreground contexts cannot mutate
#     bounded SDC event state.
#   * neg_sdc_detect_raw_result_as_verified — raw results cannot stand in
#     for tags::source::SdcVerified values.
#   * neg_sdc_detect_cross_tag_result — external provenance cannot stand
#     in for tags::source::SdcVerified values.
#   * neg_sdc_detect_sampling_rate_above_max — ppm rates above 100% cannot
#     cross the SdcConfig boundary.
#   * neg_sdc_detect_direct_construct — a detector is built only through
#     mint_sdc_detector, so its constructor is private.
crucible_neg_compile_effects_test(neg_sdc_detect_mint_bg_ctx
  "constraint|constraints.*not satisfied"
    "CtxFitsSdcMint")
crucible_neg_compile_effects_test(neg_sdc_detect_run_hot_ctx
  "constraint|constraints.*not satisfied"
    "CtxFitsSdcRun")
crucible_neg_compile_test(neg_sdc_detect_raw_result_as_verified
  "could not convert .42. from .uint64_t.*SdcVerified")
crucible_neg_compile_test(neg_sdc_detect_cross_tag_result
  "could not convert .raw. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::SdcVerified>")
crucible_neg_compile_multi_test(neg_sdc_detect_sampling_rate_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 1000000>\\(\\), unsigned int>\\(1000001\\)")
crucible_neg_compile_multi_test(neg_sdc_detect_direct_construct
  "is private within this context"
  "SdcDetector<MaxCogs, MaxEvents>::SdcDetector\\(crucible::observe::SdcConfig\\)")

# canopy/Hlc.h, the hybrid logical clock.
#   * neg_hlc_raw_timestamp_to_tagged: a raw HlcTimestamp does not
#     convert to a timestamp of the clock.
#   * neg_hlc_cross_source_timestamp: an External timestamp does not
#     convert to a timestamp of the clock.
#   * neg_hlc_direct_ctor: the constructor is private, and the mint is
#     its only friend.
#   * neg_hlc_mint_test_context: the mint takes the init context and
#     refuses a test context.
#   * neg_hlc_mint_background_context: the mint refuses a background
#     context.
#   * neg_hlc_mint_without_capability: the mint has no overload without
#     a context.
crucible_neg_compile_multi_test(neg_hlc_raw_timestamp_to_tagged
  "could not convert|cannot convert|no matching function"
  "HlcClockTimestamp")
crucible_neg_compile_multi_test(neg_hlc_cross_source_timestamp
  "could not convert|cannot convert|no matching function"
  "source::External")
crucible_neg_compile_multi_test(neg_hlc_direct_ctor
  "Hlc::Hlc\\(\\). is private within this context"
  "declared private here")
crucible_neg_compile_effects_test(neg_hlc_mint_test_context
  "could not convert|no matching function|cannot convert"
    "effects::Test|effects::Init")
crucible_neg_compile_effects_test(neg_hlc_mint_background_context
  "could not convert|no matching function|cannot convert"
    "effects::Bg|effects::Init")
crucible_neg_compile_effects_test(neg_hlc_mint_without_capability
  "no matching function|too few arguments"
    "mint_hlc")

# The three mints of the runtime metrics channel, one fixture each:
# the writer mint refuses the permission of the reader role, and the
# two reader mints refuse a call without a metrics channel.
crucible_neg_compile_multi_test(neg_observe_mint_metrics_writer_reader_permission
  "no matching function|could not convert|cannot convert|invalid initialization|cannot bind"
  "Permission|WriterTag|ReaderTag")
crucible_neg_compile_multi_test(neg_observe_mint_keeper_metrics_reader_without_channel
  "no matching function|too few arguments"
  "mint_keeper_metrics_reader")
crucible_neg_compile_multi_test(neg_observe_mint_canopy_metrics_reader_int_channel
  "no matching function|could not convert|cannot convert|invalid initialization|cannot bind"
  "RuntimeMetricsChannel|int")

# canopy/VectorClock.h, the operational vector clock.
#   * neg_vector_clock_zero_nodes: a clock with no node fails the node
#     bound of the mint.
#   * neg_vector_clock_cross_n_snapshot: snapshots of different node
#     counts are different types, so one does not cross an API boundary
#     sized for the other.
#   * neg_vector_clock_direct_ctor: the constructor is private, and
#     mint_vector_clock is the only door.
#   * neg_vector_clock_mint_test_context and
#     neg_vector_clock_mint_background_context: the mint takes Init, and
#     neither a Test nor a background context converts to it.
crucible_neg_compile_multi_test(neg_vector_clock_zero_nodes
  "required for the satisfaction of .VectorClockNodeBound<MaxNodes>. \\[with MaxNodes = 0\\]"
  "the expression .MaxNodes > 0 \\[with MaxNodes = 0\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_vector_clock_cross_n_snapshot
  "could not convert .three. from"
  ".VectorClockSnapshot<3>. to .VectorClockSnapshot<4>.")
crucible_neg_compile_multi_test(neg_vector_clock_direct_ctor
  "VectorClock<MaxNodes, Tag>::VectorClock\\(node_index_type\\) \\[with long unsigned int MaxNodes = 4. Tag = void. node_index_type = fixy::Refinement<fixy::BoundedAbove<3>\\(\\), short unsigned int, false>\\]"
  "is private within this context")
crucible_neg_compile_effects_test(neg_vector_clock_mint_test_context
  "no matching function for call to .mint_vector_clock<4>\\(foundation::effects::Test, int\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_vector_clock_mint_background_context
  "no matching function for call to .mint_vector_clock<4>\\(foundation::effects::Bg, int\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")

# canopy/Crdt.h, the state-based CRDT substrate.
#   * neg_crdt_gset_raw_add: a local write is a LocalWrite, and a raw value
#     does not convert to one.
#   * neg_crdt_merge_raw_state: a merge takes received state only as
#     gossiped state, and raw state matches no merge.
#   * neg_crdt_merge_local_state: state from the local door is not
#     gossiped state at a merge.
#   * neg_crdt_lww_partial_clock: an LWW clock must be totally ordered, and
#     a vector-clock snapshot is not.
#   * neg_crdt_mv_register_unordered_value: an MV-register value must be
#     totally ordered, so that concurrent versions sort in one order.
#   * neg_crdt_gset_floating_value and neg_crdt_gset_pointer_value: the slot
#     hash admits only enums and integers.
#   * neg_crdt_counter_update_raw_fields: raw integers do not build a
#     counter update, because its replica and its amount are refinements.
crucible_neg_compile_multi_test(neg_crdt_gset_raw_add
  "cannot convert .int. to"
  "fixy::Tagged<int, fixy::tags::source::Local>")
crucible_neg_compile_multi_test(neg_crdt_merge_raw_state
  "no matching function for call to .crucible::canopy::GSet<int, 8>::merge"
  "merge\\(crucible::canopy::detail::BoundedHashSetState<int, 8>&\\)")
crucible_neg_compile_multi_test(neg_crdt_merge_local_state
  "no matching function for call to .crucible::canopy::GSet<int, 8>::merge"
  "BoundedHashSetState<int, 8>, fixy::tags::source::Local>&\\)")
crucible_neg_compile_multi_test(neg_crdt_lww_partial_clock
  "template constraint failure for .template<class V, class Clock>"
  "three_way_comparable<Clock, std::strong_ordering>. \\[with Clock = crucible::canopy::VectorClockSnapshot<4, void>\\]")
crucible_neg_compile_multi_test(neg_crdt_mv_register_unordered_value
  "template constraint failure for .template<class V, long unsigned int MaxVersions"
  "totally_ordered<V>. \\[with V = EqualityOnlyPayload\\]")
crucible_neg_compile_multi_test(neg_crdt_gset_floating_value
  "template constraint failure for .template<class T, long unsigned int Capacity>"
  "HashableValue<T>. \\[with T = double\\]")
crucible_neg_compile_multi_test(neg_crdt_gset_pointer_value
  "template constraint failure for .template<class T, long unsigned int Capacity>"
  "HashableValue<T>. \\[with T = int\\*\\]")
crucible_neg_compile_multi_test(neg_crdt_counter_update_raw_fields
  "could not convert .9. from .int. to"
  "fixy::Refinement<fixy::BoundedAbove<3>\\(\\), short unsigned int, false>")

# canopy/Swim.h, the bounded SWIM membership.
#   * neg_swim_zero_max_peers: a zero peer bound fails the shape of the
#     mint.
#   * neg_swim_raw_peer_add: add_peer takes a SWIM member, and a raw
#     identity does not convert to one.
#   * neg_swim_external_peer_add: an External identity is not a SWIM
#     member.
#   * neg_swim_direct_ctor: the constructor is private, and
#     mint_swim_membership is the only door.
#   * neg_swim_mint_test_context and neg_swim_mint_background_context: the
#     mint takes Init, and neither a Test nor a background context
#     converts to it.
#   * neg_swim_witness_count_assign: the count of a witness set is a slot
#     count, so it cannot be set past the capacity.
crucible_neg_compile_multi_test(neg_swim_zero_max_peers
  "required for the satisfaction of .SwimShape<MaxPeers, MaxPiggyback>. \\[with MaxPeers = 0. MaxPiggyback = 32\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_swim_raw_peer_add
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::SwimMembership<4, 32>::peer_type."
  "aka .fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::SwimMember>.")
crucible_neg_compile_multi_test(neg_swim_external_peer_add
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>."
  "to .Tagged<\\[...\\],fixy::tags::source::SwimMember>.")
crucible_neg_compile_multi_test(neg_swim_direct_ctor
  "SwimMembership<MaxPeers, MaxPiggyback>::SwimMembership\\(std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::SwimMember> >, crucible::canopy::SwimConfig\\)"
  "is private within this context")
crucible_neg_compile_effects_test(neg_swim_mint_test_context
  "no matching function for call to .mint_swim_membership\\(foundation::effects::Test\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_swim_mint_background_context
  "no matching function for call to .mint_swim_membership<4>\\(foundation::effects::Bg\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")
crucible_neg_compile_multi_test(neg_swim_witness_count_assign
  "no match for .operator=. \\(operand types are .crucible::canopy::SlotCount<4>."
  "and .uint16_t.")

# canopy/HyParView.h, the bounded active and passive overlay.
#   * neg_hyparview_zero_active, neg_hyparview_active_exceeds_passive: the
#     shape of the mint refuses a zero active capacity and an active
#     capacity above the passive capacity.
#   * neg_hyparview_raw_peer_join, neg_hyparview_cross_tag_peer: join takes
#     an overlay peer, and neither a raw nor an External identity converts
#     to one.
#   * neg_hyparview_raw_shuffle, neg_hyparview_external_shuffle: a received
#     shuffle is gossiped, and neither a raw nor an External shuffle
#     converts to one.
#   * neg_hyparview_direct_ctor: the constructor is private, and
#     mint_hyparview is the only door.
#   * neg_hyparview_mint_test_context and
#     neg_hyparview_mint_background_context: the mint takes Init, and
#     neither a Test nor a background context converts to it.
crucible_neg_compile_multi_test(neg_hyparview_zero_active
  "required for the satisfaction of .HyParViewShape<MaxActive, MaxPassive>. \\[with MaxActive = 0. MaxPassive = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_hyparview_active_exceeds_passive
  "required for the satisfaction of .HyParViewShape<MaxActive, MaxPassive>. \\[with MaxActive = 8. MaxPassive = 4\\]"
  "the expression .MaxActive <= MaxPassive \\[with MaxActive = 8. MaxPassive = 4\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_hyparview_raw_peer_join
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::HyParViewPeer."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::join\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_multi_test(neg_hyparview_cross_tag_peer
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::HyParView>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::join\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_multi_test(neg_hyparview_raw_shuffle
  "cannot convert .crucible::canopy::HyParViewShuffle<8>. to .crucible::canopy::GossipedHyParViewShuffle<8>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::apply_shuffle\\(crucible::canopy::GossipedHyParViewShuffle<MaxPassive>\\)")
crucible_neg_compile_multi_test(neg_hyparview_external_shuffle
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::apply_shuffle\\(crucible::canopy::GossipedHyParViewShuffle<MaxPassive>\\)")
crucible_neg_compile_multi_test(neg_hyparview_direct_ctor
  "HyParViewMembership<MaxActive, MaxPassive>::HyParViewMembership\\(std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::HyParView> >, std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::HyParView> >, crucible::canopy::HyParViewConfig\\)"
  "is private within this context")
crucible_neg_compile_effects_test(neg_hyparview_mint_test_context
  "no matching function for call to .mint_hyparview<4>\\(foundation::effects::Test\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_hyparview_mint_background_context
  "no matching function for call to .mint_hyparview<4>\\(foundation::effects::Bg\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")

# canopy/Plumtree.h, the eager and lazy broadcast tree.
#   * neg_plumtree_zero_peers, neg_plumtree_overlay_too_large: the shape
#     gate of the mint refuses a zero peer capacity and an overlay whose
#     active view does not fit the link slots.
#   * neg_plumtree_raw_peer, neg_plumtree_external_peer: a link takes an
#     overlay peer, and neither a raw nor an External identity converts to
#     one.
#   * neg_plumtree_raw_message, neg_plumtree_external_message: a received
#     message is gossiped.
#   * neg_plumtree_raw_ihave, neg_plumtree_external_ihave: a received
#     repair summary is gossiped.
#   * neg_plumtree_direct_ctor: the constructor is private, and
#     mint_plumtree is the only door.
#   * neg_plumtree_mint_test_context and
#     neg_plumtree_mint_background_context: the mint takes Init, and
#     neither a Test nor a background context converts to it.
crucible_neg_compile_multi_test(neg_plumtree_zero_peers
  "required for the satisfaction of .PlumtreeShape<MaxPeers, MaxHistory>. \\[with MaxPeers = 0. MaxHistory = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_plumtree_overlay_too_large
  "required for the satisfaction of .PlumtreeFitsOverlay<MaxPeers, MaxHistory, HyMaxActive, HyMaxPassive>. \\[with MaxPeers = 4. MaxHistory = 8. HyMaxActive = 8. HyMaxPassive = 64\\]"
  "the expression .HyMaxActive <= MaxPeers \\[with HyMaxActive = 8. MaxPeers = 4\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_plumtree_raw_peer
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::HyParViewPeer."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::add_eager_peer\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_multi_test(neg_plumtree_external_peer
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::HyParView>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::add_lazy_peer\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_multi_test(neg_plumtree_raw_message
  "cannot convert .crucible::canopy::PlumtreeMessage. to .crucible::canopy::GossipedPlumtreeMessage."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeReceivePlan<MaxPeers, MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_message\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeMessage\\)")
crucible_neg_compile_multi_test(neg_plumtree_external_message
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeReceivePlan<MaxPeers, MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_message\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeMessage\\)")
crucible_neg_compile_multi_test(neg_plumtree_raw_ihave
  "cannot convert .crucible::canopy::PlumtreeIHave<8>. to .crucible::canopy::GossipedPlumtreeIHave<8>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeRepairPlan<MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_ihave\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeIHave<MaxHistory>\\)")
crucible_neg_compile_multi_test(neg_plumtree_external_ihave
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeRepairPlan<MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_ihave\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeIHave<MaxHistory>\\)")
crucible_neg_compile_multi_test(neg_plumtree_direct_ctor
  "PlumtreeBroadcast<MaxPeers, MaxHistory>::PlumtreeBroadcast\\(const crucible::canopy::HyParViewMembership<[A-Za-z]+, [A-Za-z]+>&, crucible::canopy::PlumtreeConfig\\)"
  "is private within this context")
crucible_neg_compile_effects_test(neg_plumtree_mint_test_context
  "no matching function for call to .mint_plumtree<4, 8>\\(foundation::effects::Test, crucible::canopy::HyParViewMembership<4, 8>&\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_plumtree_mint_background_context
  "no matching function for call to .mint_plumtree<4, 8>\\(foundation::effects::Bg, crucible::canopy::HyParViewMembership<4, 8>&\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")

# canopy/Lifeguard.h, the SWIM false-positive reduction.
#   * neg_lifeguard_zero_peers: a zero peer bound fails the shape of the
#     mint.
#   * neg_lifeguard_raw_local_peer: the local identity must be a SWIM
#     member, and a raw identity does not convert to one.
#   * neg_lifeguard_direct_ctor: the constructor is private, and
#     mint_lifeguard_swim is the only door, also for a caller that holds
#     Init.
#   * neg_lifeguard_mint_test_context and
#     neg_lifeguard_mint_background_context: the mint takes Init, and
#     neither a Test nor a background context converts to it.
#   * neg_lifeguard_event_count_advance: the count of an event batch is a
#     slot count, so it cannot be advanced past the capacity.
crucible_neg_compile_multi_test(neg_lifeguard_zero_peers
  "required for the satisfaction of .LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>. \\[with MaxPeers = 0. MaxPiggyback = 8. RttWindow = 4. MaxEvents = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_lifeguard_raw_local_peer
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Init, crucible::cog::CogIdentity&\\)."
  "cannot convert .local. \\(type .crucible::cog::CogIdentity.\\) to type .crucible::canopy::SwimPeer.")
crucible_neg_compile_multi_test(neg_lifeguard_direct_ctor
  "LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>::LifeguardSwim\\(foundation::effects::Init, crucible::canopy::SwimPeer,"
  "is private within this context")
crucible_neg_compile_effects_test(neg_lifeguard_mint_test_context
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Test,"
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_lifeguard_mint_background_context
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Bg, crucible::canopy::SwimPeer\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")
crucible_neg_compile_multi_test(neg_lifeguard_event_count_advance
  "no match for .operator\\+\\+."
  "\\(operand type is .crucible::canopy::SlotCount<4>.\\)")

# canopy/Scuttlebutt.h, the bounded anti-entropy substrate.
#   * neg_scuttlebutt_zero_max_peers: a zero peer bound fails the shape.
#   * neg_scuttlebutt_raw_key_register: a raw key does not register a
#     CRDT.  The key must be a local key.
#   * neg_scuttlebutt_raw_delta_apply: a raw delta is not a gossiped delta.
#   * neg_scuttlebutt_raw_digest_compare: a raw digest is not a gossiped
#     digest.
#   * neg_scuttlebutt_non_crdt_publish: publish requires a CRDT.
#   * neg_scuttlebutt_direct_ctor: the constructor is private, and
#     mint_scuttlebutt is the only door.
#   * neg_scuttlebutt_mint_test_context and
#     neg_scuttlebutt_mint_background_context: the mint takes Init, and
#     neither a Test nor a background context converts to it.
#   * neg_scuttlebutt_digest_count_assign and
#     neg_scuttlebutt_request_count_advance: a table count has one mutator,
#     which refuses at the capacity, so it cannot be assigned or advanced.
crucible_neg_compile_multi_test(neg_scuttlebutt_zero_max_peers
  "template constraint failure for .template<long unsigned int MaxPeers, long unsigned int MaxKeys>"
  "ScuttlebuttShape<MaxPeers, MaxKeys>. \\[with MaxPeers = 0. MaxKeys = 4\\]")
crucible_neg_compile_multi_test(neg_scuttlebutt_raw_key_register
  "no matching function for call to .crucible::canopy::ScuttlebuttSync<4, 4>::register_state\\(crucible::canopy::ScuttlebuttKey&"
  "cannot convert .key. \\(type .crucible::canopy::ScuttlebuttKey.\\) to type .crucible::canopy::LocalScuttlebuttKey.")
crucible_neg_compile_multi_test(neg_scuttlebutt_raw_delta_apply
  "cannot convert .crucible::canopy::ScuttlebuttDelta<crucible::canopy::detail::BoundedHashSetState<long unsigned int, 4> >. to"
  "fixy::Tagged<crucible::canopy::ScuttlebuttDelta<crucible::canopy::detail::BoundedHashSetState<long unsigned int, 4> >, fixy::tags::source::Gossiped>")
crucible_neg_compile_multi_test(neg_scuttlebutt_raw_digest_compare
  "cannot convert .crucible::canopy::ScuttlebuttDigest<4, 4>. to"
  "fixy::Tagged<crucible::canopy::ScuttlebuttDigest<4, 4>, fixy::tags::source::Gossiped>")
crucible_neg_compile_multi_test(neg_scuttlebutt_non_crdt_publish
  "no matching function for call to .crucible::canopy::ScuttlebuttSync<4, 4>::publish_local_change"
  "required for the satisfaction of .ScuttlebuttCrdt<C>. \\[with C = NotCrdt\\]")
crucible_neg_compile_multi_test(neg_scuttlebutt_direct_ctor
  "ScuttlebuttSync<MaxPeers, MaxKeys>::ScuttlebuttSync\\(peer_type, std::span<"
  "is private within this context")
crucible_neg_compile_effects_test(neg_scuttlebutt_mint_test_context
  "could not convert .foundation::effects::testing::test\\(\\)."
  "from .foundation::effects::Test. to .foundation::effects::Init.")
crucible_neg_compile_effects_test(neg_scuttlebutt_mint_background_context
  "could not convert .foundation::effects::testing::bg\\(\\)."
  "from .foundation::effects::Bg. to .foundation::effects::Init.")
crucible_neg_compile_multi_test(neg_scuttlebutt_digest_count_assign
  "no match for .operator=. \\(operand types are .crucible::canopy::SlotCount<4>."
  "and .uint16_t.")
crucible_neg_compile_multi_test(neg_scuttlebutt_request_count_advance
  "no match for .operator\\+\\+."
  "\\(operand type is .crucible::canopy::SlotCount<4>.\\)")

# GAPS-120 — cntp/CongestionControl.h per-socket TCP CC selection.
#   * neg_congestion_control_raw_algorithm_apply: raw enum values cannot
#     drive TCP_CONGESTION; callers must use a tagged DeclaredCcChoice.
#   * neg_congestion_control_dctcp_cross_dc: DCTCP is lossless-fabric only.
#   * neg_congestion_control_custom_missing_name: custom CC modules must
#     provide a constexpr kernel congestion-control name.
#   * neg_congestion_control_custom_invalid_name: that name must also be
#     a valid kernel name, which has no uppercase letter.
crucible_neg_compile_multi_test(neg_congestion_control_raw_algorithm_apply
  "no matching function for call to .set_cc_for_socket\\(fixy::InitLoadCtx&, fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>&, crucible::cntp::CcAlgorithm\\)."
  "cannot convert .crucible::cntp::CcAlgorithm::Cubic. \\(type .crucible::cntp::CcAlgorithm.\\) to type .crucible::cntp::DeclaredCcChoice.")
crucible_neg_compile_test(neg_congestion_control_dctcp_cross_dc
  "constraints not satisfied|CcCompatible|LosslessDatacenterFabric")
crucible_neg_compile_test(neg_congestion_control_custom_missing_name
  "constraints not satisfied|CustomCcModule|congestion_control_name")
crucible_neg_compile_test(neg_congestion_control_custom_invalid_name
  "constraints not satisfied|CustomCcModule|has_value")

# The socket option gate of cntp/CongestionControl.h.  A socket option
# call takes the socket lock, so the gate asks for IO and Block.
#   * neg_socket_option_cold_init_ctx: a context with IO and no Block.
#   * neg_socket_option_bg_drain_ctx: a context with neither.
crucible_neg_compile_effects_test(neg_socket_option_cold_init_ctx
  "no matching function for call to .set_cc_for_socket\\(fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsSocketOption<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_effects_test(neg_socket_option_bg_drain_ctx
  "no matching function for call to .set_socket_pacing_rate\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsSocketOption<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")

# cog/Calibrate.h, the per-Cog calibration schema.
#   * neg_calibrate_psu_rail: only a kind with caps and an opcode table
#     calibrates.
#   * neg_calibrate_zero_latency_quantile and
#     neg_calibrate_misordered_latency_quantile: each conjunct of the
#     calibration predicate refuses its own half of a bad triple.
#   * neg_calibrate_nonpositive_throughput: a throughput is a finite
#     positive rate.
#   * neg_calibrate_hot_fg_ctx: calibration is Init or Bg work, never the
#     hot foreground.
# A refused checked mint reports the mint call with its predicate and its
# refused value, so each refinement fixture pins all three.
crucible_neg_compile_cog_test(neg_calibrate_psu_rail
  "constraints.*not satisfied"
    "CalibratableCogKind")
crucible_neg_compile_cog_test(neg_calibrate_zero_latency_quantile
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::IsMedianPositive\\(\\)>\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{0, 1, 2\\}\\)")
crucible_neg_compile_cog_test(neg_calibrate_misordered_latency_quantile
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::IsMedianPositive\\(\\)>\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{90, 50, 70\\}\\)")
crucible_neg_compile_cog_test(neg_calibrate_nonpositive_throughput
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<(fixy::)?IsPositive\\(\\), (fixy::)?BoundedAbove<1.0e\\+30>\\(\\)>\\(\\), double>\\(0.0\\)")
crucible_neg_compile_effects_test(neg_calibrate_hot_fg_ctx
  "constraints.*not satisfied"
    "CtxFitsCalibration")

# cog/NicConfig.h, the admission of NIC configuration intent.
#   * neg_nic_config_raw_apply: a raw plan does not convert to the
#     declared configuration that a privileged apply takes.
#   * neg_nic_config_ring_not_power_of_two: the checked mint of a ring
#     size stops the constant evaluation at 1000.
#   * neg_nic_config_mint_bg_ctx, neg_nic_config_mint_hot_fg_ctx: the
#     mint refuses a context whose row holds no Init.
crucible_neg_compile_cog_test(neg_nic_config_raw_apply
    "could not convert .raw. from .crucible::cog::nic::NicConfigPlan."
    "to .crucible::cog::nic::DeclaredNicConfig. \\{aka .fixy::Tagged<crucible::cog::nic::NicConfigPlan, fixy::tags::source::NicConfig>.\\}")
crucible_neg_compile_cog_test(neg_nic_config_ring_not_power_of_two
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), InRange<256, 8192>\\(\\)>\\(\\), short unsigned int>\\(1000\\)")
crucible_neg_compile_effects_test(neg_nic_config_mint_bg_ctx
    "no matching function for call to .mint_nic_config\\(fixy::BgDrainCtx"
    "required for the satisfaction of .CtxFitsNicConfigMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_nic_config_mint_hot_fg_ctx
    "no matching function for call to .mint_nic_config\\(const fixy::HotFgCtx&"
    "required for the satisfaction of .CtxFitsNicConfigMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cog/SrIov.h, the admission of a partition into virtual functions.
#   * neg_sriov_raw_plan_enable: a raw plan does not convert to the
#     declared plan that enable takes.
#   * neg_sriov_zero_vfs: the checked mint of a count stops the constant
#     evaluation at zero, the disabled state.
#   * neg_sriov_multicast_mac: the checked mint of an address stops the
#     constant evaluation at a multicast address.
#   * neg_sriov_mint_bg_ctx, neg_sriov_mint_hot_fg_ctx: the mint refuses
#     a context whose row holds no Init.
#   * neg_sriov_vf_handle_forged: a handle is built only from a declared
#     plan, so its constructor from a parent and an index is private.
crucible_neg_compile_cog_test(neg_sriov_raw_plan_enable
    "could not convert .plan. from .crucible::cog::sriov::SrIovPlan."
    "to .crucible::cog::sriov::DeclaredSrIovPlan. \\{aka .fixy::Tagged<crucible::cog::sriov::SrIovPlan, fixy::tags::source::SrIov>.\\}")
crucible_neg_compile_cog_test(neg_sriov_zero_vfs
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<InRange<1, 4096>\\(\\), short unsigned int>\\(0\\)")
crucible_neg_compile_cog_test(neg_sriov_multicast_mac
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<crucible::cog::sriov::IsAssignableVfMac\\(\\), crucible::cog::sriov::MacAddress>\\(crucible::cog::sriov::MacAddress\\{std::array<unsigned char, 6>.*\\{1, 0, 0, 0, 0, 1\\}")
crucible_neg_compile_effects_test(neg_sriov_mint_bg_ctx
    "no matching function for call to .mint_sriov_plan\\(fixy::BgDrainCtx"
    "required for the satisfaction of .CtxFitsSrIovMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_sriov_mint_hot_fg_ctx
    "no matching function for call to .mint_sriov_plan\\(const fixy::HotFgCtx&"
    "required for the satisfaction of .CtxFitsSrIovMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_cog_test(neg_sriov_vf_handle_forged
    ".constexpr crucible::cog::sriov::VfHandle::VfHandle\\(crucible::cog::CogIdentity, crucible::cog::sriov::VfIndex\\). is private within this context"
    "cog/SrIov.h:[0-9]+:[0-9]+: note: declared private here")

# GAPS-121 — cntp/Pacing.h fq/qdisc verification + SO_MAX_PACING_RATE.
#   * neg_pacing_bbr_pfifo_config: BBR pacing config admits only fq/fq_codel.
#   * neg_pacing_raw_rate: socket pacing requires admitted positive rate.
#   * neg_pacing_raw_config_ensure: fq verification requires tagged qdisc config.
crucible_neg_compile_test(neg_pacing_bbr_pfifo_config
  "constraints not satisfied|BbrCompatibleQdisc|Qdisc::Pfifo")
crucible_neg_compile_multi_test(neg_pacing_raw_rate
  "no matching function for call to .set_socket_pacing_rate\\(fixy::TestRunnerCtx&, fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>&, long long unsigned int\\)."
  "cannot convert .1000000. \\(type .long long unsigned int.\\) to type .crucible::cntp::PositivePacingRate.")
crucible_neg_compile_test(neg_pacing_raw_config_ensure
  "could not convert|no matching function|DeclaredQdiscConfig|Tagged")

# cntp/PathSwap.h: the application-level move of a flow to a new path.
#   * neg_path_swap_raw_plan_begin: a swap starts only from a tagged plan.
#   * neg_path_swap_plan_raw_path_id: the plan mint takes admitted path ids.
#   * neg_path_swap_plan_direct: the plan mint is the only door to a plan.
#   * neg_path_swap_mint_bg_ctx: the swapper mint needs a context that owns Init.
#   * neg_path_swapper_direct: the swapper mint is the only door to a swapper.
#   * neg_path_swap_commit_fg_ctx, neg_path_swap_begin_init_ctx: a transition
#     needs a context that owns Bg.
#   * neg_path_swap_commit_non_session_resource,
#     neg_path_swap_commit_pointer_resource: the new resource is a
#     SessionResource.
crucible_neg_compile_multi_test(neg_path_swap_raw_plan_begin
  "no matching function for call to .crucible::cntp::PathSwapper<>::begin_swap.fixy::BgDrainCtx&, crucible::cntp::PathSwapPlan&, int.."
  "cannot convert .raw. .type .crucible::cntp::PathSwapPlan.. to type .const crucible::cntp::DeclaredPathSwapPlan&.")
crucible_neg_compile_multi_test(neg_path_swap_plan_raw_path_id
  "could not convert .0. from .uint64_t."
  "to .crucible::cntp::PositivePathId. .aka .fixy::Refinement<fixy::IsPositive.., long unsigned int, false>..")
crucible_neg_compile_multi_test(neg_path_swap_plan_direct
  ".constexpr crucible::cntp::PathSwapPlan::PathSwapPlan.crucible::cntp::PositivePathId, crucible::cntp::PositivePathId, crucible::cntp::PositivePathId, crucible::cntp::PositiveNanoseconds.. is private within this context"
  "PathSwap.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_effects_test(neg_path_swap_mint_bg_ctx
  "no matching function for call to .mint_path_swapper.fixy::BgDrainCtx&.."
  "required for the satisfaction of .CtxFitsPathSwapMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_multi_test(neg_path_swapper_direct
  ".constexpr crucible::cntp::PathSwapper<MaxEvents>::PathSwapper.. .with long unsigned int MaxEvents = 8.. is private within this context"
  "PathSwap.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_effects_test(neg_path_swap_commit_fg_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<>::commit_sender.fixy::HotFgCtx&"
  "required for the satisfaction of .CtxFitsPathSwapTransition<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_effects_test(neg_path_swap_begin_init_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<>::begin_swap.fixy::ColdInitCtx&"
  "required for the satisfaction of .CtxFitsPathSwapTransition<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >.")
crucible_neg_compile_multi_test(neg_path_swap_commit_non_session_resource
  "no matching function for call to .crucible::cntp::PathSwapper<>::commit_sender.fixy::BgDrainCtx&"
  "required for the satisfaction of .PathSwapSessionResource<NewResource>. .with NewResource = Wire&.")
crucible_neg_compile_multi_test(neg_path_swap_commit_pointer_resource
  "no matching function for call to .crucible::cntp::PathSwapper<>::commit_sender.fixy::BgDrainCtx&"
  "required for the satisfaction of .PathSwapSessionResource<NewResource>. .with NewResource = PointerWire.")

# ─── topology::CongestionTelemetry and its worker ────────────────────
#
#   * neg_congestion_telemetry_raw_fd_harvest — a live harvest takes an
#     admitted SocketFd, not a raw int.
#   * neg_congestion_telemetry_raw_state_aggregate — aggregation takes
#     samples tagged as coming from TCP_INFO, not raw CongestionSample.
#   * neg_congestion_telemetry_worker_fg_record — recording is background
#     work, so the foreground hot-path context is refused.
#   * neg_congestion_telemetry_worker_poll_bg_drain_ctx — a poll reads
#     sockets, so a drain context with no IO fails the socket option half.
#   * neg_congestion_telemetry_worker_poll_init_load_ctx — a poll is
#     background work, so a startup context fails the background half.
#   * neg_congestion_telemetry_worker_mint_bg_ctx — only an Init-row
#     context mints the worker.
#   * neg_congestion_telemetry_worker_mint_non_ctx — an int in the context
#     slot is refused by the IsExecCtx constraint.
#   * neg_congestion_telemetry_worker_direct_construct — the worker's
#     constructor is private, so the mint is the only door.
crucible_neg_compile_multi_test(neg_congestion_telemetry_raw_fd_harvest
  "no matching function for call to .harvest_socket\\(fixy::TestRunnerCtx&, int\\)."
  "cannot convert .3. \\(type .int.\\) to type .crucible::cntp::SocketFd. \\{aka .fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>.\\}")
crucible_neg_compile_multi_test(neg_congestion_telemetry_raw_state_aggregate
  "could not convert .std::span<crucible::topology::CongestionSample, 1>\\(samples\\)."
  "to .span<const fixy::Tagged<crucible::topology::CongestionSample, fixy::tags::source::TcpInfo>,")
crucible_neg_compile_effects_test(neg_congestion_telemetry_worker_fg_record
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::record_link\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
  "required for the satisfaction of .CtxFitsCongestionTelemetryHarvest<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_effects_test(neg_congestion_telemetry_worker_poll_bg_drain_ctx
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::poll_link\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsSocketOption<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]"
  "required for the satisfaction of .CtxFitsCongestionTelemetryPoll<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_effects_test(neg_congestion_telemetry_worker_poll_init_load_ctx
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::poll_link\\(fixy::InitLoadCtx&, "
  "required for the satisfaction of .CtxFitsCongestionTelemetryHarvest<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, "
  "required for the satisfaction of .CtxFitsCongestionTelemetryPoll<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, ")
crucible_neg_compile_topology_test(neg_congestion_telemetry_worker_mint_bg_ctx
  "no matching function for call to .mint_congestion_telemetry_worker<1, 1>\\(fixy::BgDrainCtx&\\)"
  "required for the satisfaction of .CtxFitsCongestionTelemetryStart<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_topology_test(neg_congestion_telemetry_worker_mint_non_ctx
  "no matching function for call to .mint_congestion_telemetry_worker<1, 1>\\(int\\)"
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = int\\]")
crucible_neg_compile_topology_test(neg_congestion_telemetry_worker_direct_construct
  "is private within this context"
  "CongestionTelemetryWorker<MaxLinks, MaxFlows>::CongestionTelemetryWorker\\(\\) \\[with long unsigned int MaxLinks = 1; long unsigned int MaxFlows = 1\\]")

# GAPS-124 — cntp/IncastControl.h incast mitigation substrate.
#   * neg_incast_raw_fd_configure: socket tuning requires admitted SocketFd.
#   * neg_incast_raw_config_configure: socket tuning requires tagged config.
#   * neg_incast_credit_fg_ctx: credit mutation is Bg-row work.
crucible_neg_compile_test(neg_incast_raw_fd_configure
  "could not convert|no matching function|SocketFd|Refined")
crucible_neg_compile_test(neg_incast_raw_config_configure
  "could not convert|no matching function|DeclaredIncastConfig|Tagged")
crucible_neg_compile_effects_test(neg_incast_credit_fg_ctx
  "constraints.*not satisfied"
    "CtxFitsIncastCredit|Bg")

# cntp/RoceConfig.h.  Each fixture names the call that fails and the
# reason, so a fixture that fails for another reason does not pass.
#   * neg_roce_zero_pfc_mask: a config selects at least one PFC priority.
#   * neg_roce_dscp_above_max: a DSCP is a 6-bit value.
#   * neg_roce_raw_config_apply: the apply path takes a config from the mint.
#   * neg_roce_config_wrong_source: a config of another source is refused.
#   * neg_roce_pause_counters_without_block: the counter read needs Block.
#   * neg_roce_pause_counters_hot_ctx: the hot context reads no counter.
crucible_neg_compile_multi_test(neg_roce_zero_pfc_mask
  "no matching function for call to .mint_roce_config<0, 26>\\(crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .ValidPfcPriorityMask<PfcPriorities>. \\[with PfcPriorities = 0\\]")
crucible_neg_compile_multi_test(neg_roce_dscp_above_max
  "no matching function for call to .mint_roce_config<8, 64>\\(crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .ValidRoceDscp<Dscp>. \\[with Dscp = 64\\]")
crucible_neg_compile_multi_test(neg_roce_raw_config_apply
  "invalid initialization of reference of type .const crucible::cntp::DeclaredRoceConfig&. .* from expression of type .crucible::cntp::RoceConfig."
  "in passing argument 1 of .constexpr std::expected<void, crucible::cntp::RoceError> crucible::cntp::apply_roce_config\\(const DeclaredRoceConfig&\\).")
crucible_neg_compile_multi_test(neg_roce_config_wrong_source
  "invalid initialization of reference of type .const crucible::cntp::DeclaredRoceConfig&. .* from expression of type .fixy::Tagged<crucible::cntp::RoceConfig, fixy::tags::source::External>."
  "in passing argument 1 of .constexpr std::expected<void, crucible::cntp::RoceError> crucible::cntp::validate_roce_config\\(const DeclaredRoceConfig&\\).")
crucible_neg_compile_multi_test(neg_roce_pause_counters_without_block
  "no matching function for call to .query_pfc_pause_counters\\(fixy::ColdInitCtx&, crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .CtxFitsFileMint<Ctx, fixy::atom::fs::mode<fixy::fs::open_mode::ReadOnly> >. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_multi_test(neg_roce_pause_counters_hot_ctx
  "no matching function for call to .query_pfc_pause_counters\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >, crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .CtxFitsFileMint<Ctx, fixy::atom::fs::mode<fixy::fs::open_mode::ReadOnly> >. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/_wip/GpuDirect.h, the GPUDirect RDMA and Storage scaffold.  Each
# fixture names the call that fails and the reason.
#   * neg_gpu_direct_null_address: a GPU virtual address is not null.
#   * neg_gpu_direct_zero_bytes: a region or a transfer covers a byte.
#   * neg_gpu_direct_raw_plan_register: a registration takes a declared
#     plan.
#   * neg_gpu_direct_mint_bg_ctx, neg_gpu_direct_mint_hot_ctx: a region
#     plan is minted at initialization.
#   * neg_gpu_direct_storage_bg_ctx, neg_gpu_direct_storage_hot_ctx: a
#     storage plan is minted at initialization.
crucible_neg_compile_multi_test(neg_gpu_direct_null_address
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_gpu_direct_zero_bytes
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_gpu_direct_raw_plan_register
  "could not convert .plan. from .const crucible::cntp::_wip::gpu_direct::GpuDirectMrPlan. to .crucible::cntp::_wip::gpu_direct::DeclaredGpuDirectMrPlan."
  "aka .fixy::Tagged<crucible::cntp::_wip::gpu_direct::GpuDirectMrPlan, crucible::cntp::_wip::gpu_direct::wip_source::GpuDirect>.")
crucible_neg_compile_effects_test(neg_gpu_direct_mint_bg_ctx
  "no matching function for call to .mint_gpu_direct_mr_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_gpu_direct_mint_hot_ctx
  "no matching function for call to .mint_gpu_direct_mr_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_effects_test(neg_gpu_direct_storage_bg_ctx
  "no matching function for call to .mint_gpu_direct_storage_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_gpu_direct_storage_hot_ctx
  "no matching function for call to .mint_gpu_direct_storage_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/_wip/Sharp.h, the in-network reduction scaffold.  Each fixture names
# the call that fails and the reason.
#   * neg_sharp_zero_participants: a fabric plan has a participant.
#   * neg_sharp_raw_plan_dispatch: a dispatch takes a declared plan.
#   * neg_sharp_strict_recipe_concept, neg_sharp_non_commutative_recipe:
#     the recipe laws refuse a recipe with a fixed order and one that does
#     not commute.
#   * neg_sharp_mint_bg_ctx, neg_sharp_mint_hot_ctx: a fabric plan is
#     minted at initialization.
#   * neg_sharp_context_bg_ctx, neg_sharp_context_hot_ctx: a SHARP context
#     is minted at initialization.
#   * neg_sharp_dispatch_init_ctx, neg_sharp_dispatch_hot_ctx: an
#     all-reduce is background work.
crucible_neg_compile_multi_test(neg_sharp_zero_participants
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), short unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_sharp_raw_plan_dispatch
  "could not convert .plan. from .const crucible::cntp::_wip::sharp::SharpFabricPlan. to .crucible::cntp::_wip::sharp::DeclaredSharpFabricPlan."
  "aka .fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>.")
crucible_neg_compile_multi_test(neg_sharp_strict_recipe_concept
  "no matching function for call to .sharp_recipe_laws<StrictRecipe>\\(\\)."
  "the expression .\\(Recipe::determinism\\) != crucible::ReductionDeterminism::BITEXACT_STRICT \\[with Recipe = StrictRecipe\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_sharp_non_commutative_recipe
  "no matching function for call to .sharp_recipe_laws<NonCommutativeRecipe>\\(\\)."
  "the expression .Recipe::commutative \\[with Recipe = NonCommutativeRecipe\\]. evaluated to .false.")
crucible_neg_compile_effects_test(neg_sharp_mint_bg_ctx
  "no matching function for call to .mint_sharp_fabric_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, "
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_sharp_mint_hot_ctx
  "no matching function for call to .mint_sharp_fabric_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, "
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_effects_test(neg_sharp_context_bg_ctx
  "no matching function for call to .mint_sharp_context\\(fixy::BgDrainCtx&, fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>&\\)."
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_sharp_context_hot_ctx
  "no matching function for call to .mint_sharp_context\\(const fixy::HotFgCtx&, fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>&\\)."
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_effects_test(neg_sharp_dispatch_init_ctx
  "no matching function for call to .crucible::cntp::_wip::sharp::SharpReducer::allreduce_via_sharp\\(fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsSharpDispatch<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_effects_test(neg_sharp_dispatch_hot_ctx
  "no matching function for call to .crucible::cntp::_wip::sharp::SharpReducer::allreduce_via_sharp\\(const fixy::HotFgCtx&, "
  "required for the satisfaction of .CtxFitsSharpDispatch<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/_wip/Doca.h, the DPU offload scaffold.  Each fixture names the call
# that fails and the reason.
#   * neg_doca_zero_program_id: a program ID reserves zero.
#   * neg_doca_zero_image_bytes: a program image is not empty.
#   * neg_doca_zero_queue_depth: a DPU queue holds at least one entry.
#   * neg_doca_raw_plan_deploy: a deploy takes a declared plan.
#   * neg_doca_mint_bg_ctx, neg_doca_mint_hot_ctx: a plan is minted at
#     initialization.
#   * neg_doca_send_init_ctx, neg_doca_recv_hot_ctx: the channel is used
#     from background work.
crucible_neg_compile_multi_test(neg_doca_zero_program_id
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_doca_zero_image_bytes
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_doca_zero_queue_depth
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), short unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_doca_raw_plan_deploy
  "could not convert .raw. from .const crucible::cntp::_wip::doca::DocaDeployPlan. to .crucible::cntp::_wip::doca::DeclaredDocaDeployPlan."
  "aka .fixy::Tagged<crucible::cntp::_wip::doca::DocaDeployPlan, crucible::cntp::_wip::doca::wip_source::DocaOffload>.")
crucible_neg_compile_effects_test(neg_doca_mint_bg_ctx
  "no matching function for call to .mint_doca_deploy_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, const crucible::cntp::_wip::doca::DocaOffloadSpec&\\)."
  "required for the satisfaction of .CtxFitsDocaMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_doca_mint_hot_ctx
  "no matching function for call to .mint_doca_deploy_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, const crucible::cntp::_wip::doca::DocaOffloadSpec&\\)."
  "required for the satisfaction of .CtxFitsDocaMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_effects_test(neg_doca_send_init_ctx
  "no matching function for call to .crucible::cntp::_wip::doca::DpuCommChannel::send_to_dpu\\(fixy::ColdInitCtx&, std::array<std::byte, 1>&\\)."
  "required for the satisfaction of .CtxFitsDocaComm<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_effects_test(neg_doca_recv_hot_ctx
  "no matching function for call to .crucible::cntp::_wip::doca::DpuCommChannel::recv_from_dpu\\(const fixy::HotFgCtx&, std::array<std::byte, 8>&\\)."
  "required for the satisfaction of .CtxFitsDocaComm<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/_wip/P4.h, the programmable switch scaffold.  Each fixture names the
# call that fails and the reason.
#   * neg_p4_zero_program_id: a program ID reserves zero.
#   * neg_p4_zero_source_bytes: a program source is not empty.
#   * neg_p4_zero_tcam_entries: a TCAM budget asks for an entry.
#   * neg_p4_raw_program_deploy: a deploy takes a declared program.
#   * neg_p4_mint_bg_ctx, neg_p4_mint_hot_ctx: a program is minted at
#     initialization.
crucible_neg_compile_multi_test(neg_p4_zero_program_id
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_p4_zero_source_bytes
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_p4_zero_tcam_entries
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_p4_raw_program_deploy
  "could not convert .raw. from .const crucible::cntp::_wip::p4::P4ProgramSpec. to .crucible::cntp::_wip::p4::DeclaredP4Program."
  "aka .fixy::Tagged<crucible::cntp::_wip::p4::P4ProgramSpec, crucible::cntp::_wip::p4::wip_source::P4Compiled>.")
crucible_neg_compile_effects_test(neg_p4_mint_bg_ctx
  "no matching function for call to .mint_p4_program\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, const crucible::cntp::_wip::p4::P4ProgramSpec&\\)."
  "required for the satisfaction of .CtxFitsP4Mint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_p4_mint_hot_ctx
  "no matching function for call to .mint_p4_program\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, const crucible::cntp::_wip::p4::P4ProgramSpec&\\)."
  "required for the satisfaction of .CtxFitsP4Mint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/Tcam.h, the hardware ACL and flow-rule substrate.
#   * neg_tcam_zero_entry_count: a table capacity is at least one entry.
#   * neg_tcam_static_table_zero: the in-process table holds a slot.
#   * neg_tcam_table_plan_default: a table plan comes only from its mint.
#   * neg_tcam_raw_rule_add: add_rule takes a declared rule.
#   * neg_tcam_rule_handle_copy: an installed rule handle is linear.
#   * neg_tcam_rule_handle_forge / neg_tcam_rule_handle_mint_linear: only
#     the rule table builds a rule handle.
#   * neg_tcam_mint_bg_ctx / neg_tcam_mint_gpu_caps: the mint gate asks for
#     the initialization row and for a target that carries a TCAM.
crucible_neg_compile_multi_test(neg_tcam_zero_entry_count
  "in .constexpr. expansion of .fixy::mint_refined<InRange<1, 65536>\\(\\), unsigned int>\\(0\\)."
  ".__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_tcam_static_table_zero
  "template constraint failure for .template<unsigned int MaxRules>  requires  TcamTableShape<MaxRules> class crucible::cntp::tcam::TcamRules."
  "required for the satisfaction of .TcamTableShape<MaxRules>. .with MaxRules = 0.")
crucible_neg_compile_multi_test(neg_tcam_table_plan_default
  "no matching function for call to .fixy::Tagged<crucible::cntp::tcam::TcamTablePlan, fixy::tags::source::TcamTable>::Tagged\\(<brace-enclosed initializer list>\\)."
  "required for the satisfaction of .default_initializable<T>. .with T = crucible::cntp::tcam::TcamTablePlan."
  "no matching function for call to .crucible::cntp::tcam::TcamTablePlan::TcamTablePlan\\(\\).")
crucible_neg_compile_multi_test(neg_tcam_raw_rule_add
  "cannot convert .crucible::cntp::tcam::TcamFlowRule. to .crucible::cntp::tcam::DeclaredTcamFlowRule."
  "initializing argument 1 of .constexpr std::expected<fixy::Qtt<foundation::algebra::lattices::QttGrade::One, crucible::cntp::tcam::TcamRuleHandle>, crucible::cntp::tcam::TcamError> crucible::cntp::tcam::TcamRules<MaxRules>::add_rule\\(")
crucible_neg_compile_multi_test(neg_tcam_rule_handle_copy
  "use of deleted function .fixy::Qtt<Grade, T>::Qtt\\(const fixy::Qtt<Grade, T>&\\) .with auto Grade = foundation::algebra::lattices::QttGrade::One. T = crucible::cntp::tcam::TcamRuleHandle.."
  "Linear<T> and Affine<T> are move-only. use std::move or drop\\(\\)")
crucible_neg_compile_multi_test(neg_tcam_rule_handle_forge
  "error: .constexpr crucible::cntp::tcam::TcamRuleHandle::TcamRuleHandle\\(crucible::cog::Uuid, crucible::cntp::tcam::TcamRuleId, uint32_t, uint32_t\\). is private within this context"
  "include/crucible/cntp/Tcam.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_multi_test(neg_tcam_rule_handle_mint_linear
  "no matching function for call to .mint_linear<crucible::cntp::tcam::TcamRuleHandle>\\(crucible::cog::Uuid, fixy::Refinement<fixy::IsNonZero\\(\\), long unsigned int, false>, uint32_t, uint32_t\\)."
  ".crucible::cntp::tcam::TcamRuleHandle. is not constructible from .crucible::cog::Uuid, fixy::Refinement<fixy::IsNonZero\\(\\), long unsigned int, false>, unsigned int, unsigned int., because"
  "TcamRuleHandle::TcamRuleHandle\\(crucible::cog::Uuid, crucible::cntp::tcam::TcamRuleId, uint32_t, uint32_t\\). is private within this context")
crucible_neg_compile_effects_test(neg_tcam_mint_bg_ctx
  "no matching function for call to .mint_tcam_table\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NicPortTargetCaps&, "
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, "
  "required for the satisfaction of .CtxFitsTcamMint<Ctx, Caps>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_effects_test(neg_tcam_mint_gpu_caps
  "no matching function for call to .mint_tcam_table\\(fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps, "
  "required for the satisfaction of .TcamTargetCaps<Caps>. .with Caps = crucible::cog::GpuTargetCaps."
  "required for the satisfaction of .CtxFitsTcamMint<Ctx, Caps>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, ")

# cntp/_wip/Wireguard.h: the WireGuard tunnel scaffold with no kernel backend.
#   * neg_wireguard_zero_allowed_ips: a peer owns at least one allowed CIDR.
#   * neg_wireguard_raw_public_key_peer: a peer takes a tagged public key.
#   * neg_wireguard_key_default_slot: a public key has no empty default.
#   * neg_wireguard_key_bad_text: a public key is 43 base64 letters and "=".
#   * neg_wireguard_private_key_copy: a private key is a Secret, move-only.
#   * neg_wireguard_config_zero_peers, neg_wireguard_config_too_many_peers
#     and the two psk forms: a config holds one to sixteen peers.
#   * neg_wireguard_tunnel_direct_ctor, neg_wireguard_handle_direct_ctor:
#     the mint is the only door to a tunnel and to its handle.
#   * neg_wireguard_mint_bg_ctx, neg_wireguard_mint_hot_ctx: the tunnel mint
#     needs a context that owns Init.
crucible_neg_compile_multi_test(neg_wireguard_zero_allowed_ips
  "no matching function for call to .declare_wireguard_peer\\("
  "required for the satisfaction of .WireguardAllowedIpShape<N>. \\[with N = 0\\]")
crucible_neg_compile_multi_test(neg_wireguard_raw_public_key_peer
  "no matching function for call to .declare_wireguard_peer\\(const crucible::cntp::_wip::WireguardKeyChars&, "
  "cannot convert .raw_key. .* to type .crucible::cntp::_wip::DeclaredWireguardPublicKey.")
crucible_neg_compile_multi_test(neg_wireguard_key_default_slot
  "no matching function for call to .fixy::Tagged<fixy::Refinement<crucible::cntp::_wip::IsWireguardKeyText\\(\\), std::array<char, 44>, false>, crucible::cntp::_wip::wip_source::Wireguard>::Tagged\\(<brace-enclosed initializer list>\\)."
  "required for the satisfaction of .default_initializable<T>. \\[with T = fixy::Refinement<crucible::cntp::_wip::IsWireguardKeyText\\{\\}, std::array<char, 44>, false>\\]")
crucible_neg_compile_multi_test(neg_wireguard_key_bad_text
  "in .constexpr. expansion of .fixy::mint_refined<crucible::cntp::_wip::IsWireguardKeyText\\(\\), std::array<char, 44> >\\(crucible::cntp::_wip::WireguardKeyChars\\(\\)\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_wireguard_private_key_copy
  "use of deleted function .fixy::Secret<T>::Secret\\(const fixy::Secret<T>&\\) \\[with T = crucible::cntp::_wip::WireguardSecretKeyBytes\\]."
  "Secret<T> cannot be silently duplicated")
crucible_neg_compile_multi_test(neg_wireguard_config_zero_peers
  "no matching function for call to .mint_wireguard_config\\("
  "required for the satisfaction of .WireguardPeerSetShape<PeerCount>. \\[with PeerCount = 0\\]")
crucible_neg_compile_multi_test(neg_wireguard_config_too_many_peers
  "no matching function for call to .mint_wireguard_config\\("
  "required for the satisfaction of .WireguardPeerSetShape<PeerCount>. \\[with PeerCount = 17\\]")
crucible_neg_compile_multi_test(neg_wireguard_psk_config_zero_peers
  "no matching function for call to .mint_wireguard_config_with_psk\\("
  "required for the satisfaction of .WireguardPeerSetShape<PeerCount>. \\[with PeerCount = 0\\]")
crucible_neg_compile_multi_test(neg_wireguard_psk_config_too_many_peers
  "no matching function for call to .mint_wireguard_config_with_psk\\("
  "required for the satisfaction of .WireguardPeerSetShape<PeerCount>. \\[with PeerCount = 17\\]")
crucible_neg_compile_multi_test(neg_wireguard_tunnel_direct_ctor
  "converting to .crucible::cntp::_wip::WireguardTunnel<2>::MintKey. from initializer list would use explicit constructor"
  "MintKey::MintKey\\(\\) \\[with unsigned char MaxPeers = 2\\]. is private within this context")
crucible_neg_compile_multi_test(neg_wireguard_handle_direct_ctor
  ".constexpr crucible::cntp::_wip::WireguardTunnelHandle::WireguardTunnelHandle\\(crucible::cntp::NicInterfaceName, crucible::cntp::_wip::DeclaredWireguardPublicKey, uint32_t\\). is private within this context"
  "Wireguard.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_effects_test(neg_wireguard_mint_bg_ctx
  "no matching function for call to .mint_wireguard_tunnel\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsWireguardMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_wireguard_mint_hot_ctx
  "no matching function for call to .mint_wireguard_tunnel\\(const fixy::HotFgCtx&, "
  "required for the satisfaction of .CtxFitsWireguardMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

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

# cntp/_wip/QuicTransport.h: the QUIC and TLS 1.3 substrate with no backend.
#   * neg_quic_raw_config_connect: a connect takes a tagged QUIC config.
#   * neg_quic_raw_cc_config, neg_quic_config_raw_stream_limit: the config
#     mint takes a tagged congestion-control choice and admitted limits.
#   * neg_quic_connection_mint_bg_ctx: the connection mint needs a context
#     that owns Init.
#   * neg_quic_connection_direct: the connection mint is the only door.
#   * neg_quic_open_stream_fg_ctx, neg_quic_datagram_init_ctx: stream and
#     datagram work needs a context that owns Bg.
#   * neg_quic_raw_migration_plan: a migration takes a tagged path-swap plan.
#   * neg_quic_resumption_token_raw_length: from() is the only writer of a
#     token length.
crucible_neg_compile_multi_test(neg_quic_raw_config_connect
  "invalid initialization of reference of type .const crucible::cntp::_wip::DeclaredQuicConfig&. .aka .const fixy::Tagged<crucible::cntp::_wip::QuicConfig, crucible::cntp::_wip::wip_source::Quic>&.. from expression of type .crucible::cntp::_wip::QuicConfig."
  "in passing argument 3 of .std::expected<void, crucible::cntp::_wip::QuicError> crucible::cntp::_wip::connect_quic")
crucible_neg_compile_multi_test(neg_quic_raw_cc_config
  "could not convert .raw_cc. from .crucible::cntp::CcSelection. to .crucible::cntp::DeclaredCcChoice. .aka .fixy::Tagged<crucible::cntp::CcSelection, fixy::tags::source::CcAlgorithm>.."
  "Tagged.h:[0-9]+:[0-9]+: note: explicit conversion function was not considered")
crucible_neg_compile_multi_test(neg_quic_config_raw_stream_limit
  "could not convert .0. from .uint16_t."
  "to .crucible::cntp::_wip::PositiveQuicStreamLimit. .aka .fixy::Refinement<fixy::IsPositive.., short unsigned int, false>..")
crucible_neg_compile_effects_test(neg_quic_connection_mint_bg_ctx
  "no matching function for call to .mint_quic_connection.fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxFitsQuicMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_multi_test(neg_quic_connection_direct
  ".constexpr crucible::cntp::_wip::QuicConnection<MaxStreams>::QuicConnection.crucible::cntp::SocketFd, crucible::cntp::AuthenticatedMtlsPeer, const crucible::cntp::_wip::DeclaredQuicConfig&. .with long unsigned int MaxStreams = 4.* is private within this context"
  "QuicTransport.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_effects_test(neg_quic_open_stream_fg_ctx
  "no matching function for call to .crucible::cntp::_wip::QuicConnection<>::open_stream.fixy::HotFgCtx&.."
  "required for the satisfaction of .CtxFitsQuicRuntime<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_effects_test(neg_quic_datagram_init_ctx
  "no matching function for call to .crucible::cntp::_wip::QuicConnection<>::send_datagram.fixy::ColdInitCtx&"
  "required for the satisfaction of .CtxFitsQuicRuntime<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >.")
crucible_neg_compile_multi_test(neg_quic_raw_migration_plan
  "no matching function for call to .crucible::cntp::_wip::QuicConnection<>::plan_migration.fixy::BgDrainCtx&, crucible::cntp::PathSwapPlan&.."
  "cannot convert .raw. .type .crucible::cntp::PathSwapPlan.. to type .const crucible::cntp::DeclaredPathSwapPlan&.")
crucible_neg_compile_multi_test(neg_quic_resumption_token_raw_length
  ".uint16_t crucible::cntp::_wip::QuicResumptionToken::size_. is private within this context"
  "QuicTransport.h:[0-9]+:[0-9]+: note: declared private here")

# cntp/_wip/KtlsOffload.h: the kernel TLS offload scaffold with no kernel
# install.
#   * neg_ktls_tls12_crypto: the crypto mint refuses TLS 1.2 secrets.
#   * neg_ktls_chacha_crypto: the crypto mint refuses a ChaCha20 suite.
#   * neg_ktls_raw_crypto_offload: an offload takes tagged crypto info.
#   * neg_ktls_offload_bg_ctx: the offload mint needs a context that owns
#     Init.
#   * neg_ktls_socket_bg_ctx, neg_ktls_socket_hot_ctx: the socket mint needs
#     a context that owns Init.
crucible_neg_compile_multi_test(neg_ktls_tls12_crypto
  "no matching function for call to .mint_ktls_crypto_info<crucible::cntp::TlsVersion::V12>\\("
  "required for the satisfaction of .SupportedKtlsVersion<Version>. \\[with Version = crucible::cntp::TlsVersion::V12\\]")
crucible_neg_compile_multi_test(neg_ktls_chacha_crypto
  "no matching function for call to .mint_ktls_crypto_info<crucible::cntp::TlsVersion::V13, crucible::cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256>\\("
  "required for the satisfaction of .KtlsAesGcmCipherSuite<Suite>. \\[with Suite = crucible::cntp::MtlsCipherSuite::TlsChacha20Poly1305Sha256\\]")
crucible_neg_compile_multi_test(neg_ktls_raw_crypto_offload
  "no matching function for call to .mint_ktls_offload_for_socket\\(fixy::ColdInitCtx&, "
  "cannot convert .std::move<crucible::cntp::_wip::TlsCryptoInfo&>\\(raw\\). .* to type .crucible::cntp::_wip::DeclaredTlsCryptoInfo.")
crucible_neg_compile_effects_test(neg_ktls_offload_bg_ctx
  "no matching function for call to .mint_ktls_offload_for_socket\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsKtlsMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_ktls_socket_bg_ctx
  "no matching function for call to .mint_ktls_socket\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsKtlsMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_effects_test(neg_ktls_socket_hot_ctx
  "no matching function for call to .mint_ktls_socket\\(const fixy::HotFgCtx&, "
  "required for the satisfaction of .CtxFitsKtlsMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/Backpressure.h and cntp/BackpressureRuntime.h.  Each fixture names
# the call that fails and the reason, so a fixture that fails for another
# reason does not pass.
#   * neg_backpressure_raw_admission_decision: a decision crosses a
#     boundary only through its mint.
#   * neg_backpressure_invalid_resource_kind: a pressure names a resource
#     from the catalog.
#   * neg_backpressure_raw_pressure_kind: no constructor names the resource
#     around the mint.
#   * neg_backpressure_hot_ctx_credit: the hot context cannot start a flow.
#   * neg_backpressure_start_without_block: the start gate is a block.
#   * neg_backpressure_controller_bg_mint: a controller is built at
#     initialization.
#   * neg_backpressure_controller_hot_accept: the hot context cannot ask
#     for an admission.
crucible_neg_compile_multi_test(neg_backpressure_raw_admission_decision
  "conversion from .const crucible::cntp::AdmissionDecision. to non-scalar type .crucible::cntp::DeclaredAdmissionDecision."
  "fixy::Tagged<crucible::cntp::AdmissionDecision, fixy::tags::source::AdmissionDecision>")
crucible_neg_compile_multi_test(neg_backpressure_invalid_resource_kind
  "no matching function for call to .mint_resource_pressure<bogus>\\(int\\)."
  "required for the satisfaction of .IsResourceKind<Kind>. \\[with Kind = \\(foundation::effects::ResourceKind\\)255\\]")
crucible_neg_compile_multi_test(neg_backpressure_raw_pressure_kind
  "crucible::cntp::ResourcePressure::ResourcePressure\\(foundation::effects::ResourceKind, crucible::cntp::ResourcePressurePpm\\). is private within this context"
  "declared private here")
crucible_neg_compile_multi_test(neg_backpressure_hot_ctx_credit
  "no matching function for call to .crucible::cntp::CreditFlowControl<1>::start_flow\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "required for the satisfaction of .CtxFitsBackpressureRuntime<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_multi_test(neg_backpressure_start_without_block
  "no matching function for call to .crucible::cntp::CreditFlowControl<1>::start_flow\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxMayBlock<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_multi_test(neg_backpressure_controller_bg_mint
  "no matching function for call to .mint_admission_controller<2, 2>\\(fixy::BgLoadCtx&\\)."
  "required for the satisfaction of .CtxFitsBackpressureMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >\\]")
crucible_neg_compile_multi_test(neg_backpressure_controller_hot_accept
  "no matching function for call to .crucible::cntp::AdmissionController<2, 2>::try_accept_connection\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "required for the satisfaction of .CtxFitsBackpressureRuntime<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# cntp/AfXdp.h.  Each fixture names the call that fails and the reason, so
# a fixture that fails for another reason does not pass.
#   * neg_af_xdp_raw_config: a socket mint takes only a config from the door.
#   * neg_af_xdp_non_power_of_two_frame: the static shape refuses 1500 bytes.
#   * neg_af_xdp_cross_source_packet: TX takes only a view of this socket.
#   * neg_af_xdp_unsanitized_rx: an External frame is not a Sanitized one.
#   * neg_af_xdp_config_raw_frame_size: the config door takes refined shapes.
#   * neg_af_xdp_config_weaker_refinement: a bare power of two is too weak.
crucible_neg_compile_multi_test(neg_af_xdp_raw_config
  "no matching function for call to .mint_af_xdp_socket<131072, 2048"
  "cannot convert .raw. .type .crucible::cntp::AfXdpConfig.. to type .crucible::cntp::DeclaredAfXdpConfig.")
crucible_neg_compile_multi_test(neg_af_xdp_non_power_of_two_frame
  "no matching function for call to .mint_af_xdp_socket<131072, 1500"
  "template constraint failure for .template<.*requires  AfXdpStaticShape<UmemBytes, FrameSize")
crucible_neg_compile_multi_test(neg_af_xdp_cross_source_packet
  "cannot convert .Borrowed<.*,OtherOwner,.*>. to .Borrowed<.*,crucible::cntp::AfXdpSocket<131072, 2048"
  "initializing argument 1 of .*enqueue_tx.packet_view.")
crucible_neg_compile_multi_test(neg_af_xdp_unsanitized_rx
  "could not convert .untrusted. from .Tagged<.*,fixy::tags::source::External>."
  "to .Tagged<.*,fixy::tags::source::Sanitized>.")
crucible_neg_compile_multi_test(neg_af_xdp_config_raw_frame_size
  "could not convert .2048. from .uint32_t."
  "to .crucible::cntp::AfXdpFrameSize.")
crucible_neg_compile_multi_test(neg_af_xdp_config_weaker_refinement
  "could not convert .tiny. from .Refinement<fixy::IsPowerOfTwo..,"
  "to .Refinement<fixy::refined_algebra::AllOf<fixy::IsPowerOfTwo.., fixy::InRange<1024, 16384>..>..,")

# cntp/ConnectionPool.h and cntp/ConnectionPoolRuntime.h: the pool of
# established connections.
#   * neg_connection_pool_raw_event: a pool event carries the ConnectionPool tag.
#   * neg_connection_pool_invalid_transport: the connection mint takes an
#     enumerated transport class.
#   * neg_connection_direct: the connection mint is the only door.
#   * neg_connection_pool_mint_bg_ctx: the pool mint needs a context that owns
#     Init.
#   * neg_connection_pool_direct: the pool mint is the only door.
#   * neg_connection_pool_hot_ctx_lease, neg_connection_pool_drain_ctx_lease:
#     a lease needs a context that owns Bg or Test, and Block for the gate.
#   * neg_connection_pool_lease_rewrite: a lease cannot rewrite its connection.
crucible_neg_compile_multi_test(neg_connection_pool_raw_event
  "could not convert .raw. from .crucible::cntp::PoolEvent. to .crucible::cntp::DeclaredPoolEvent. .aka .fixy::Tagged<crucible::cntp::PoolEvent, fixy::tags::source::ConnectionPool>.."
  "Tagged.h:[0-9]+:[0-9]+: note: explicit conversion function was not considered")
crucible_neg_compile_multi_test(neg_connection_pool_invalid_transport
  "no matching function for call to .mint_connection<.crucible::cntp::TransportClass.255>"
  "required for the satisfaction of .PoolTransportClass<T>. .with T = .crucible::cntp::TransportClass.255.")
crucible_neg_compile_multi_test(neg_connection_direct
  "no matching function for call to .mint_linear<crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp> >"
  ".constexpr crucible::cntp::Connection<T>::Connection.crucible::cntp::SocketFd, crucible::cog::Uuid, crucible::cntp::PositiveConnectionId. .with crucible::cntp::TransportClass T = crucible::cntp::TransportClass::Tcp.* is private within this context")
crucible_neg_compile_multi_test(neg_connection_pool_mint_bg_ctx
  "no matching function for call to .mint_connection_pool<crucible::cntp::TransportClass::Tcp, 1, 1>.fixy::BgLoadCtx&.."
  "required for the satisfaction of .CtxFitsConnectionPoolMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >.")
crucible_neg_compile_multi_test(neg_connection_pool_direct
  ".constexpr crucible::cntp::ConnectionPool<T, MaxRemotes, MaxPerRemote, MaxEvents>::ConnectionPool.crucible::cntp::PoolConfig. .with crucible::cntp::TransportClass T = crucible::cntp::TransportClass::Tcp.* is private within this context"
  "ConnectionPoolRuntime.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_multi_test(neg_connection_pool_hot_ctx_lease
  "no matching function for call to .crucible::cntp::ConnectionPool<crucible::cntp::TransportClass::Tcp, 1, 1>::lease.fixy::HotFgCtx&"
  "required for the satisfaction of .CtxFitsConnectionPoolRuntime<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_multi_test(neg_connection_pool_drain_ctx_lease
  "no matching function for call to .crucible::cntp::ConnectionPool<crucible::cntp::TransportClass::Tcp, 1, 1>::lease.fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxMayBlock<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_multi_test(neg_connection_pool_lease_rewrite
  "passing .const crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>. as .this. argument discards qualifiers"
  "in call to .constexpr crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>& crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>::operator=.const crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>&..")

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

# cntp/GossipMulticast.h XDP_TX multicast plan substrate.
#   * neg_gossip_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_gossip_multicast_raw_plan: XDP program extraction requires
#     source::GossipMulticast provenance.
#   * neg_gossip_multicast_raw_topic: raw topic keys cannot register
#     neighbors; callers must pass DeclaredGossipTopic.
#   * neg_gossip_multicast_zero_neighbors: zero-neighbor plans are rejected
#     at the shape concept.
crucible_neg_compile_multi_test(neg_gossip_multicast_mint_bg_ctx
  "no matching function for call to .mint_gossip_multicast_plan<4, 4>\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxFitsGossipMulticastMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_multi_test(neg_gossip_multicast_raw_plan
  "could not convert .raw. from .crucible::cntp::GossipMulticastSpec. to .crucible::cntp::DeclaredGossipMulticastPlan."
  "fixy::Tagged<crucible::cntp::GossipMulticastSpec, fixy::tags::source::GossipMulticast>")
crucible_neg_compile_multi_test(neg_gossip_multicast_raw_topic
  "cannot convert .crucible::cntp::GossipTopicKey. to .crucible::cntp::DeclaredGossipTopic."
  "fixy::Tagged<crucible::cntp::GossipTopicKey, fixy::tags::source::GossipMulticast>")
crucible_neg_compile_multi_test(neg_gossip_multicast_zero_neighbors
  "no matching function for call to .mint_gossip_multicast_plan<4, 0>"
  "required for the satisfaction of .GossipNeighborShape<MaxNeighbors>. .with MaxNeighbors = 0.")

# cntp/OverlayMulticast.h application-layer route substrate.
#   * neg_overlay_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_overlay_multicast_raw_peer: raw peer refs cannot seed a plan.
#   * neg_overlay_multicast_zero_stripes: zero-stripe route tables rejected.
#   * neg_overlay_multicast_cross_tag_peer: externally tagged peer refs cannot
#     seed an overlay route plan.
#   * neg_overlay_multicast_private_ctor: the mint is the one door to a plan.
crucible_neg_compile_multi_test(neg_overlay_multicast_mint_bg_ctx
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>.")
crucible_neg_compile_multi_test(neg_overlay_multicast_raw_peer
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::ColdInitCtx&, crucible::cntp::OverlayPeerRef&\\)"
  "cannot convert .raw. \\(type .crucible::cntp::OverlayPeerRef.\\) to type .crucible::cntp::DeclaredOverlayPeer.")
crucible_neg_compile_multi_test(neg_overlay_multicast_zero_stripes
  "no matching function for call to .mint_overlay_multicast<4, 0, 2>\\(fixy::ColdInitCtx&"
  "required for the satisfaction of .OverlayMulticastShape<MaxPeers, MaxStripes, MaxFanout>. .with MaxPeers = 4; MaxStripes = 0; MaxFanout = 2.")
crucible_neg_compile_multi_test(neg_overlay_multicast_cross_tag_peer
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::ColdInitCtx&, fixy::Tagged<crucible::cntp::OverlayPeerRef, fixy::tags::source::External>&\\)"
  "cannot convert .external. \\(type .fixy::Tagged<crucible::cntp::OverlayPeerRef, fixy::tags::source::External>.\\) to type .crucible::cntp::DeclaredOverlayPeer.")
crucible_neg_compile_multi_test(neg_overlay_multicast_private_ctor
  "OverlayMulticastPlan<MaxPeers, MaxStripes, MaxFanout>::OverlayMulticastPlan\\(crucible::cntp::DeclaredOverlayPeer"
  "is private within this context")

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
  "could not convert|no matching function|cannot convert"
    "effects::Test|effects::Init")
crucible_neg_compile_test(neg_fec_encode_non_contiguous_owned
  "constraints not satisfied|ByteContiguousBuffer|no matching function")

# cntp/Fountain.h, the deterministic LT fountain codec.
#   * neg_fountain_zero_symbols / neg_fountain_zero_symbol_bytes: each
#     shape dimension of a codec is positive.
#   * neg_fountain_mint_test_context / neg_fountain_decoder_mint_bg_ctx:
#     the mint gate asks for the initialization row.
#   * neg_fountain_encoder_mint_zero_symbols /
#     neg_fountain_decoder_mint_short_equations: the mint gate also holds
#     the shape.
#   * neg_fountain_raw_seed: a repair choice takes a DetSafe Philox key.
#   * neg_fountain_encode_non_contiguous_owned: an owned payload is a
#     Linear byte-contiguous buffer.
crucible_neg_compile_multi_test(neg_fountain_zero_symbols
  "template constraint failure for .template<long unsigned int SourceSymbols, long unsigned int SymbolBytes, class Algorithm>  requires \\(FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>\\) && \\(FountainAlgorithm<Algorithm>\\) class crucible::cntp::FountainEncoder."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>. .with SourceSymbols = 0. SymbolBytes = 16.")
crucible_neg_compile_multi_test(neg_fountain_zero_symbol_bytes
  "template constraint failure for .template<long unsigned int SourceSymbols, long unsigned int SymbolBytes, class Algorithm>  requires \\(FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>\\) && \\(FountainAlgorithm<Algorithm>\\) class crucible::cntp::FountainEncoder."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>. .with SourceSymbols = 4. SymbolBytes = 0.")
crucible_neg_compile_effects_test(neg_fountain_mint_test_context
  "no matching function for call to .mint_fountain_encoder<4, 16>\\(fixy::TestRunnerCtx&\\)."
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, "
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>. ")
crucible_neg_compile_effects_test(neg_fountain_encoder_mint_zero_symbols
  "no matching function for call to .mint_fountain_encoder<0, 16>\\(fixy::ColdInitCtx&\\)."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, MaxEquations>. .with SourceSymbols = 0. SymbolBytes = 16. MaxEquations = 0."
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>. ")
crucible_neg_compile_effects_test(neg_fountain_decoder_mint_bg_ctx
  "no matching function for call to .mint_fountain_decoder<8, 16>\\(fixy::BgDrainCtx&\\)."
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, "
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>. ")
crucible_neg_compile_effects_test(neg_fountain_decoder_mint_short_equations
  "no matching function for call to .mint_fountain_decoder<8, 16, 7>\\(fixy::ColdInitCtx&\\)."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, MaxEquations>. .with SourceSymbols = 8. SymbolBytes = 16. MaxEquations = 7."
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>. ")
crucible_neg_compile_multi_test(neg_fountain_raw_seed
  "cannot convert .long long unsigned int. to .crucible::cntp::FountainSeed."
  "initializing argument 2 of .std::expected<void, crucible::cntp::FountainError> crucible::cntp::FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>::start_encoding\\(std::span<const std::byte>, crucible::cntp::FountainSeed\\)")
crucible_neg_compile_multi_test(neg_fountain_encode_non_contiguous_owned
  "no matching function for call to .crucible::cntp::FountainEncoder<4, 16, crucible::cntp::LtFountain>::encode_owned\\("
  "required for the satisfaction of .ByteContiguousBuffer<Input>. .with Input = NotWireBytes.")

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
crucible_neg_compile_multi_test(neg_cipher_head_zero_literal
  "__builtin_trap... is not a constant expression"
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero.., crucible::ContentHash>.crucible::ContentHash.0..")
crucible_neg_compile_multi_test(neg_cipher_head_default_construct
  "__builtin_trap... is not a constant expression"
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero.., crucible::ContentHash>.empty..")

# ─── ValidMerkleRoot neg-compile tests ──────────────────────────────
#
# ValidMerkleRoot is the fixy Refined<non_zero, MerkleHash> and gates the
# parameter to Tx::commit (defense-in-depth alongside the
# computed_merkle_hash() accessor).  MerkleHash{0} is the
# structural sentinel for "subtree never built" — committing it
# would (1) corrupt tx_log's step-by-merkle invariant (a fresh
# Transaction's merkle_root field is also MerkleHash{}), (2) collide
# with TERMINAL leaves in dag_diff equality short-circuits, and (3)
# propagate as a no-op replay key into federation round-trip flows.
#
# Two fixtures, distinct mismatch classes:
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
crucible_neg_compile_multi_test(neg_merkle_root_zero_literal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::MerkleHash>\\(crucible::MerkleHash\\(0\\)\\)")
crucible_neg_compile_multi_test(neg_merkle_root_default_construct
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::MerkleHash>\\(crucible::MerkleHash\\(\\)\\)")

# ─── ValidContentHash neg-compile tests ─────────────────────────────
#
# ValidContentHash is the fixy Refined<non_zero, ContentHash>, declared
# in MerkleDag.h beside ValidMerkleRoot.  ContentHash{0} is the
# structural sentinel for "this region was never folded by
# compute_content_hash" — empty regions legitimately carry it
# (make_region with num_ops==0), but it is never a valid
# cache-publish / federation / merkle-fold key.  Three accessors
# now route through the typed alias instead of inline spellings of
# Refined<non_zero, ContentHash>:
#   * RegionNode::computed_content_hash (MerkleDag.h)
#   * LoopNode::computed_body_content_hash (MerkleDag.h)
#   * TraceGraph::computed_content_hash (TraceGraph.h)
#
# Two fixtures, mirroring the ValidMerkleRoot pair (the predicate
# non_zero is the same; only the strong-hash family differs):
#   * neg_content_hash_zero_literal — explicit-literal mismatch
#     (= ContentHash{uint64_t{0}}).  Catches caller-typed-zero drift.
#   * neg_content_hash_default_construct — NSDMI-default wide miss
#     (= ContentHash{}).  Catches the realistic mode: a freshly
#     allocated RegionNode / TraceGraph whose content_hash field is
#     default-zero and hasn't yet been populated by the streaming
#     fold.
#
# Defense-in-depth: each accessor's `pre(decide::is_non_zero(...))`
# runtime gate is retained.  ValidContentHash catches the value at
# the construction call; the pre clause catches it at the accessor
# body's entry.  Both layers must reject for the type-level
# invariant and the runtime path to disagree.
crucible_neg_compile_multi_test(neg_content_hash_zero_literal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::ContentHash>\\(crucible::ContentHash\\(0\\)\\)")
crucible_neg_compile_multi_test(neg_content_hash_default_construct
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::ContentHash>\\(crucible::ContentHash\\(\\)\\)")

# The live-slot pointer of TransactionLog is
# fixy::Tagged<Transaction*, source::Ring>: a pointer into the ring's inline
# slots, valid for the log's lifetime.
#   * neg_active_tx_ptr_raw_assignment: a raw pointer does not convert,
#     because the constructor from the value is private.
#   * neg_active_tx_ptr_cross_source_assignment: a pointer under the Arena
#     tag does not convert, because no retag edge joins the two tags.
crucible_neg_compile_multi_test(neg_active_tx_ptr_raw_assignment
  "neg_active_tx_ptr_raw_assignment.cpp:27:25: error: conversion from .crucible::FakeTransaction\\*. to non-scalar type"
  "\\{aka .fixy::Tagged<crucible::FakeTransaction\\*, fixy::tags::source::Ring>.\\} requested")
crucible_neg_compile_multi_test(neg_active_tx_ptr_cross_source_assignment
  "neg_active_tx_ptr_cross_source_assignment.cpp:27:20: error: conversion from"
  ".Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Arena>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Ring>. requested")

# CrucibleContext's active region is a
# `fixy::Tagged<const RegionNode*, tags::source::Vigil>`: a pointer that
# Vigil's background worker published.  One fixture per mismatch class:
#   * neg_active_region_ptr_raw_assignment: a raw pointer cannot take the
#     tag, because only mint_tagged writes a provenance (bypass).
#   * neg_active_region_ptr_cross_source_assignment: a pointer tagged
#     Arena cannot become one tagged Vigil, because no retag edge joins
#     the two sources (laundering).
crucible_neg_compile_multi_test(neg_active_region_ptr_raw_assignment
  "conversion from .const crucible::FakeRegionNode\\*. to non-scalar type .ActiveRegionPtr."
  "fixy::Tagged<const crucible::FakeRegionNode\\*, fixy::tags::source::Vigil>")
crucible_neg_compile_multi_test(neg_active_region_ptr_cross_source_assignment
  "conversion from .Tagged<\\[...\\],fixy::tags::source::Arena>. to non-scalar type .Tagged<\\[...\\],fixy::tags::source::Vigil>. requested"
  "neg_active_region_ptr_cross_source_assignment.cpp:35")

# BackgroundThread::device_capability holds a DeviceCapability,
# fixy::Tagged<uint64_t, tags::source::Meridian>.  The tag says that the
# startup calibration pass measured the value on real silicon.
#   * neg_device_capability_raw_assignment: a raw uint64_t does not
#     convert, because the constructor from the value is explicit and
#     private (bypass).
#   * neg_device_capability_cross_source_assignment: a value under
#     tags::source::Calibrated does not convert, because no retag edge
#     joins the two tags (laundering).
crucible_neg_compile_multi_test(neg_device_capability_raw_assignment
  "neg_device_capability_raw_assignment.cpp:23:36: error: no match for .operator=. \\(operand types are .crucible::BackgroundThread::DeviceCapability. \\{aka .fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>.\\} and .uint64_t."
  "no known conversion for argument 1 from .uint64_t. \\{aka .long unsigned int.\\} to .const fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>&.")
crucible_neg_compile_multi_test(neg_device_capability_cross_source_assignment
  "neg_device_capability_cross_source_assignment.cpp:25:36: error: no match for .operator=. \\(operand types are .crucible::BackgroundThread::DeviceCapability. \\{aka .fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>.\\} and .fixy::Tagged<long unsigned int, fixy::tags::source::Calibrated>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<long unsigned int, fixy::tags::source::Calibrated>. to .const fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>&.")

# RegionCache::ops_ holds fixy::Tagged<const TraceEntry*, source::RegionOps>.
#   * neg_region_ops_ptr_raw_assignment: a raw pointer does not convert,
#     because the constructor from the value is private.
#   * neg_region_ops_ptr_cross_source_assignment: a pointer under the Arena
#     tag does not convert, because no retag edge joins the two tags.
crucible_neg_compile_multi_test(neg_region_ops_ptr_raw_assignment
  "neg_region_ops_ptr_raw_assignment.cpp:29:20: error: conversion from .const crucible::FakeTraceEntry\\*. to non-scalar type"
  "\\{aka .fixy::Tagged<const crucible::FakeTraceEntry\\*, fixy::tags::source::RegionOps>.\\} requested")
crucible_neg_compile_multi_test(neg_region_ops_ptr_cross_source_assignment
  "neg_region_ops_ptr_cross_source_assignment.cpp:28:26: error: conversion from"
  ".Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Arena>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::RegionOps>. requested")

# ─── CostModel refinements and context gate ──────────────────────────
#
# ValidRegsPerThread, ValidWarpSize and ValidUtilization are fixy::Refined
# over named predicates, and the checked mint is the only door to each.
# Two fixtures pin each predicate.  The first regex says that the
# precondition of the mint failed.  The second names the mint, its
# predicate and the refused value.
#   * regs_per_thread: 256, one above the ceiling of 255, and 65535, the
#     largest uint16_t.  validate_config also compares the count with the
#     ceiling of the part, which can be below 255.
#   * warp_size: 33, not a power of two, and 256, a power of two above the
#     ceiling of 128.
#   * utilization: 1.5 and -0.5, one on each side of [0, 1].  The formulas
#     keep the ratio in range, so the mint never fires on a valid input.
crucible_neg_compile_multi_test(neg_costmodel_regs_per_thread_overflow
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<255>\\(\\), short unsigned int>\\(256\\)")
crucible_neg_compile_multi_test(neg_costmodel_regs_per_thread_max_uint16
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<255>\\(\\), short unsigned int>\\(std::numeric_limits<short unsigned int>::max\\(\\)\\)")
crucible_neg_compile_multi_test(neg_costmodel_warp_size_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(33\\)")
crucible_neg_compile_multi_test(neg_costmodel_warp_size_too_large
  "__builtin_trap.*is not a constant expression"
  "mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(256\\)")
crucible_neg_compile_multi_test(neg_costmodel_utilization_above_one
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<0.0f, 1.0e\\+0f>\\(\\), float>\\(1.5e\\+0f\\)")
crucible_neg_compile_multi_test(neg_costmodel_utilization_negative
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<0.0f, 1.0e\\+0f>\\(\\), float>\\(-5.0e-1f\\)")

# The evaluators are pure arithmetic over values the caller owns, and
# CtxFitsCostModel admits only a context whose row is empty.  Each fixture
# calls one evaluator with a context that claims one effect.  The first
# regex names the refused call and the context.  The second says that the
# gate failed, and the third names the refused row.
crucible_neg_compile_multi_test(neg_costmodel_wave_efficiency_io_row
  "no matching function for call to .wave_efficiency\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::IO>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_costmodel_sm_occupancy_alloc_row
  "no matching function for call to .sm_occupancy\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_costmodel_evaluate_cost_bg_row
  "no matching function for call to .evaluate_cost\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Bg>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_costmodel_fusion_benefit_block_row
  "no matching function for call to .compute_fusion_benefit\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Block> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow_v<R1, R2> \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")

# BackgroundThread::RegionReadyCallback::Fn is a noexcept function
# pointer, because the callback runs on the publish stage in the middle
# of a publication.  The first fixture shows that a function that is not
# noexcept does not convert to Fn.  The second shows that Fn is not the
# same type as the plain function pointer.
set(BG_CALLBACK_PARAMS "void\\*, const foundation::effects::Bg&, crucible::BackgroundThread::PublishStage, crucible::RegionNode\\*")
set(BG_CALLBACK_CLASS_PARAMS "void\\*, const class foundation::effects::Bg&, class crucible::BackgroundThread::PublishStage, struct crucible::RegionNode\\*")
crucible_neg_compile_multi_test(neg_background_thread_callback_throwing_fnptr_rejected
  "invalid conversion from .void \\(\\*\\)\\(${BG_CALLBACK_PARAMS}\\). to .crucible::BackgroundThread::RegionReadyCallback::Fn."
  "aka .void \\(\\*\\)\\(${BG_CALLBACK_PARAMS}\\) noexcept.")
crucible_neg_compile_multi_test(neg_background_thread_callback_type_identity_drift
  "static assertion failed: RegionReadyCallback::Fn carries noexcept, so it is not the plain function pointer type"
  ".void \\(\\*\\)\\(${BG_CALLBACK_CLASS_PARAMS}\\) noexcept. is not the same as .void \\(\\*\\)\\(${BG_CALLBACK_CLASS_PARAMS}\\).")

# Transaction::ts_ns holds an optional reading of the monotonic clock.
#   - neg_transaction_ts_ns_bare_u64_assign: a bare integer is no reading,
#     because the constructor of ClockSource from its value is private.
#   - neg_transaction_ts_ns_cross_source: a boot clock reading is a distinct
#     type from a monotonic one.
crucible_neg_compile_multi_test(neg_transaction_ts_ns_bare_u64_assign
  "neg_transaction_ts_ns_bare_u64_assign.cpp:18:32: error: no match for .operator=. \\(operand types are .std::optional<fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int> >. and .uint64_t."
  "error: .constexpr fixy::ClockSource<Source, T>::ClockSource\\(T\\) \\[with foundation::algebra::lattices::ClockSource Source = foundation::algebra::lattices::ClockSource::Monotonic; T = long unsigned int\\]. is private within this context")
crucible_neg_compile_multi_test(neg_transaction_ts_ns_cross_source
  "neg_transaction_ts_ns_cross_source.cpp:25:34: error: no match for .operator=. \\(operand types are .std::optional<fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int> >. and .fixy::ClockSource<foundation::algebra::lattices::ClockSource::Boot, long unsigned int>."
  "error: no matching function for call to .fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int>::ClockSource\\(fixy::ClockSource<foundation::algebra::lattices::ClockSource::Boot, long unsigned int>\\).")

# Cipher::obj_path returns fixy::Tagged<std::string, tags::source::CipherPath>.
# The two fixtures read that return type by reflection.
#   - neg_cipher_obj_path_bare_string_assign: a bare std::string does not
#     convert, because the constructor from the value is explicit and private.
#   - neg_cipher_obj_path_cross_source: a path under FromUserPath does not
#     convert, because no retag edge reaches CipherPath.
crucible_neg_compile_multi_test(neg_cipher_obj_path_bare_string_assign
  "neg_cipher_obj_path_bare_string_assign.cpp:41:45: error: no match for .operator=. \\(operand types are .CipherObjPath. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>.\\} and .std::string."
  "no known conversion for argument 1 from .std::string. \\{aka .std::__cxx11::basic_string<char>.\\} to .const fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>&.")
crucible_neg_compile_multi_test(neg_cipher_obj_path_cross_source
  "neg_cipher_obj_path_cross_source.cpp:45:19: error: no match for .operator=. \\(operand types are .CipherObjPath. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>.\\} and .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::FromUserPath>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::FromUserPath>. to .const fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>&.")

# ─── WRAP-Types-3 #1069 HS14 negative-compile fixtures ─────────────
#   - neg_types_hash_family_unspecialized  (incomplete primary template)
#   - neg_types_hash_family_cross_lane     (Family-A hash ≠ IsFamilyB)
crucible_neg_compile_test(neg_types_hash_family_unspecialized
  "incomplete type|no type named|no member named|constraints not satisfied")
crucible_neg_compile_test(neg_types_hash_family_cross_lane
  "static assertion failed|WRAP-Types-3|Family-B|IsFamilyB")

# Expr::hash is fixy::Tagged<uint64_t, hash_family::FamilyB>.  The two
# fixtures take that type from the field.
#   - neg_expr_hash_bare_u64_assign: a bare uint64_t does not convert,
#     because the constructor from the value is explicit and private.
#   - neg_expr_hash_cross_family: a Family-A hash does not convert,
#     because the two families are different types.
crucible_neg_compile_multi_test(neg_expr_hash_bare_u64_assign
  "neg_expr_hash_bare_u64_assign.cpp:29:39: error: no match for .operator=. \\(operand types are .ExprHash. \\{aka .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>.\\} and .uint64_t."
  "no known conversion for argument 1 from .uint64_t. \\{aka .long unsigned int.\\} to .const fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>&.")
crucible_neg_compile_multi_test(neg_expr_hash_cross_family
  "neg_expr_hash_cross_family.cpp:31:21: error: no match for .operator=. \\(operand types are .ExprHash. \\{aka .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>.\\} and .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyA>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyA>. to .const fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>&.")

# ─── WRAP-Reflect-4 #985 HS14 negative-compile fixtures ────────────
#   - neg_reflect_supported_void           (void excluded from allow-list)
#   - neg_reflect_supported_function_type  (function types excluded)
crucible_neg_compile_test(neg_reflect_supported_void
  "static assertion failed|WRAP-Reflect-4|IsReflectFieldSupported")
crucible_neg_compile_test(neg_reflect_supported_function_type
  "static assertion failed|WRAP-Reflect-4|IsReflectFieldSupported")

# pack_field packs only the field categories that IsReflectFieldSupported
# names.  A pointer-to-member field reaches its trailing static_assert.
crucible_neg_compile_test(neg_reflect_pack_field_unsupported_category
  "pack_field handles enum, integral")

# ─── Philox op_key DetSafe gate neg-compiles (2 fixtures) ───────────
#
# The raw per-op key derivation API is not public.  Production callers
# must route through op_key_det(), carrying DetSafe<Pure> provenance
# until the boundary that explicitly unwraps the key bytes.  One fixture
# names the raw function, the other takes the band as a bare uint64_t.
crucible_neg_compile_multi_test(neg_philox_raw_op_key
  "is not a member of"
  ".op_key. is not a member of .crucible::Philox.")
crucible_neg_compile_multi_test(neg_philox_op_key_det_raw_uint64
  "cannot convert"
  "cannot convert .crucible::Philox::DetSafePureKey.*to .uint64_t.")

# ─── FOUND-G12 OpaqueLifetime production-call-site neg-compile ─────
#
# Load-bearing rejection at the persistence boundary: an
# OpaqueLifetime<PER_REQUEST, T> value rejected by Cipher::
# commit_per_fleet, whose requires-clause demands satisfies<PER_FLEET>.
# Captures the inferlet cross-request leak bug class (PdaState /
# user state declared PER_REQUEST silently flowed to fleet-durable
# cold storage).
crucible_neg_compile_multi_test(neg_opaque_lifetime_per_request_at_per_fleet_fence
  "no matching function for call to .crucible::Cipher::commit_per_fleet."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_REQUEST>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_FLEET.. evaluated to .false.")

# The rest of the lifetime to tier refusals.  A request-scoped region
# does not reach the warm tier, which survives a restart, and a
# program-scoped region does not reach the fleet-wide cold tier.  A bare
# pointer and a band of another lattice state no scope, so they reach no
# commit entry point.
crucible_neg_compile_multi_test(neg_opaque_lifetime_per_request_at_per_program_fence
  "no matching function for call to .crucible::Cipher::commit_per_program."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_REQUEST>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_PROGRAM.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_opaque_lifetime_per_program_at_per_fleet_fence
  "no matching function for call to .crucible::Cipher::commit_per_fleet."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_PROGRAM>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_FLEET.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_commit_per_request_bare_pointer
  "no matching function for call to .crucible::Cipher::commit_per_request."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = const crucible::RegionNode... evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_commit_per_request_wrong_band
  "no matching function for call to .crucible::Cipher::commit_per_request."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::CipherTierLattice::At<foundation::algebra::lattices::CipherTierTag::Hot>, const crucible::RegionNode.>.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_commit_per_program_bare_pointer
  "no matching function for call to .crucible::Cipher::commit_per_program."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = const crucible::RegionNode... evaluated to .false.")

# A NumericalTier below BITEXACT must not reach a consumer that requires
# BITEXACT.  The three fixtures refuse a gap of six tiers (RELAXED), one
# tier (ULP_FP64) and three tiers (ULP_FP16), so the refusal is
# structural along the whole lattice.  The first regex names the tier of
# the value, and the second names the gate of the consumer.
crucible_neg_compile_multi_test(neg_recipe_registry_relaxed_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::RELAXED>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_multi_test(neg_recipe_registry_fp64_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::ULP_FP64>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_multi_test(neg_recipe_registry_fp16_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::ULP_FP16>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")

# by_name_pinned takes a Tolerance.  A RecipeFamily enumerator in its
# place must fail at the template parameter.  Admission on the two axes
# uses by_name_spec and then fixy::admits.
crucible_neg_compile_multi_test(neg_recipe_registry_pinned_with_family_template_arg
  "no matching function for call to .crucible::RecipeRegistry::by_name_pinned<foundation::algebra::lattices::RecipeFamily::Kahan>"
  "could not convert .foundation::algebra::lattices::RecipeFamily::Kahan. from .foundation::algebra::lattices::RecipeFamily. to .foundation::algebra::lattices::Tolerance.")

# The RecipeSpec of by_name_spec and the NumericalTier of by_name_pinned
# are different types.  The expected of one does not convert to the
# expected of the other, in each direction.
crucible_neg_compile_multi_test(neg_recipe_registry_spec_assigned_to_pinned_expected
  "conversion from .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::RecipeFamilyLattice> >"
  "to non-scalar type .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ToleranceLattice::At<foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_multi_test(neg_recipe_registry_pinned_assigned_to_spec_expected
  "conversion from .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ToleranceLattice::At<foundation::algebra::lattices::Tolerance::BITEXACT>"
  "to non-scalar type .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::RecipeFamilyLattice> >")

# The lookups of the registry are pure projections.  by_name, by_hash,
# entries, the pinned lookups and the spec lookups each refuse a caller
# row that is not empty.  The first regex names the lookup and the row,
# and the second names the Subrow gate that refused the row.
crucible_neg_compile_multi_test(neg_recipe_registry_by_name_io_row
  "no matching function for call to .crucible::RecipeRegistry::by_name<foundation::effects::Row<foundation::effects::Effect::IO> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO>\\]")
crucible_neg_compile_multi_test(neg_recipe_registry_by_hash_init_row
  "no matching function for call to .crucible::RecipeRegistry::by_hash<foundation::effects::Row<foundation::effects::Effect::Init> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Init>\\]")
crucible_neg_compile_multi_test(neg_recipe_registry_entries_bg_row
  "no matching function for call to .crucible::RecipeRegistry::entries<foundation::effects::Row<foundation::effects::Effect::Bg> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>\\]")
crucible_neg_compile_multi_test(neg_recipe_registry_pinned_alloc_row
  "no matching function for call to .crucible::RecipeRegistry::by_name_pinned<foundation::algebra::lattices::Tolerance::BITEXACT, foundation::effects::Row<foundation::effects::Effect::Alloc> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>\\]")
crucible_neg_compile_multi_test(neg_recipe_registry_spec_block_row
  "no matching function for call to .crucible::RecipeRegistry::by_name_spec<foundation::effects::Row<foundation::effects::Effect::Block> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Block>\\]")

# entries() gives a span tagged source::JsonRegistry.  A raw span and a
# span with a different tag must not reach a consumer of the entries.
crucible_neg_compile_multi_test(neg_recipe_registry_entries_raw_span
  "could not convert .raw. from .std::span<const crucible::RecipeRegistry::Entry>. to .crucible::RecipeRegistry::Entries."
  "fixy::Tagged<std::span<const crucible::RecipeRegistry::Entry>, fixy::tags::source::JsonRegistry>")
crucible_neg_compile_multi_test(neg_recipe_registry_entries_cross_tag
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::JsonRegistry>. requested")

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

# BitMask stores a refined mask.  The numeric ceiling
# ((1 << kGroupWidth) - 1) bounds the mask, not kGroupWidth itself: a
# valid mask of width 16 can be 0xF0F0.  The two fixtures pin the edge
# one above the ceiling and a wide miss.  The first regex says that the
# precondition of the checked mint failed.  The second names the mint and
# its ceiling predicate.  The third names the BitMask constructor and the
# refused value.  On AVX512BW the width is 64 and no value is out of
# range, so each fixture fails there on its own static_assert.
crucible_neg_compile_multi_test(neg_swiss_bitmask_one_past_group_mask
  "__builtin_trap.*is not a constant expression|requires a group width below 64"
  "fixy::mint_refined<BoundedAbove<[0-9]+>\\(\\), long unsigned int>\\(raw_mask\\)|requires a group width below 64"
  "BitMask::BitMask\\(.*kGroupMaskCeiling\\) \\+ 1\\)\\)|requires a group width below 64")
crucible_neg_compile_multi_test(neg_swiss_bitmask_wide_miss
  "__builtin_trap.*is not a constant expression|requires a group width below 64"
  "fixy::mint_refined<BoundedAbove<[0-9]+>\\(\\), long unsigned int>\\(raw_mask\\)|requires a group width below 64"
  "BitMask::BitMask\\(std::numeric_limits<long unsigned int>::max\\(\\)\\)|requires a group width below 64")

# kGroupWidth is a fixy::PowerOfTwo<size_t>, and the checked mint is its
# only door.  The two fixtures give the mint zero and a value that is not
# a power of two.  The second regex names the mint, its predicate and the
# refused value.
crucible_neg_compile_multi_test(neg_swiss_group_width_zero
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(0\\)")
crucible_neg_compile_multi_test(neg_swiss_group_width_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(48\\)")

# ExprPool reads int_cache_ through IntCacheLiteral, which is
# Refined<in_range<-128, 127>, int64_t>.  Two fixtures pin the two ends
# of the closed range: -129 is one below the low end, and 128 is one
# above the high end.  The first regex says that the precondition of
# the door failed.  The second names the door, its predicate and the
# value, so the rejection is the one under test.
crucible_neg_compile_multi_test(neg_expr_pool_int_cache_literal_below_low
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<-128, 127>\\(\\), long int>\\(-129\\)")
crucible_neg_compile_multi_test(neg_expr_pool_int_cache_literal_above_high
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<-128, 127>\\(\\), long int>\\(128\\)")

# ExprPool::capacity_ is a PowerOfTwo<size_t>, and intern_count_ is a
# Monotonic<size_t>.  The fixtures pin two absences: no capacity that is
# not a power of two, and no assignment of a raw integer to the intern
# count.
crucible_neg_compile_multi_test(neg_expr_pool_capacity_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(12\\)")
crucible_neg_compile_multi_test(neg_expr_pool_intern_count_assign_raw
  "no match for .operator=."
  "crucible::ExprPool::InternCount.*fixy::Monotonic<long unsigned int")

# ExprPool::make returns DetSafe<Pure, Tagged<const Expr*,
# source::Interned>>.  A raw Expr* and a value at the same tier with a
# different source tag must both fail at the consumer.
crucible_neg_compile_multi_test(neg_expr_pool_make_raw_expr
  "could not convert"
  "from .const crucible::Expr\\*. to .crucible::ExprPool::PureInternedExpr.")
crucible_neg_compile_multi_test(neg_expr_pool_make_cross_tag
  "could not convert"
  "source::External>>. to .Graded<.*source::Interned>>")

# RecipePool::capacity_ is a PowerOfTwo<uint32_t>, and size_ is a
# Monotonic<uint32_t>.  The fixtures pin two absences: no capacity that
# is not a power of two, and no assignment of a raw integer to the size.
# The first regex says that the gate refused.  The second names the
# door, the predicate or the type, so the refusal is the one under test.
crucible_neg_compile_multi_test(neg_recipe_pool_capacity_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), unsigned int>\\(12\\)")
crucible_neg_compile_multi_test(neg_recipe_pool_size_assign_raw
  "no match for .operator=."
  "crucible::RecipePool::Size.*fixy::Monotonic<unsigned int")

# The RecipePool constructor takes a minted BorrowedRef<Arena, Brand>.
# A raw Arena* and a borrow of a different type must fail at the
# constructor.
crucible_neg_compile_multi_test(neg_recipe_pool_arena_raw_pointer
  "no matching function for call to .crucible::RecipePool::RecipePool"
  "mismatched types .fixy::BorrowedRef<crucible::Arena, Brand>. and .crucible::Arena\\*.")
crucible_neg_compile_multi_test(neg_recipe_pool_arena_wrong_borrow
  "no matching function for call to .crucible::RecipePool::RecipePool"
  "mismatched types .crucible::Arena. and .OtherArena.")

# A RecipePool is built in the initialization phase.  The constructor
# defaults to Row<Init>, and a pure row, a row with only Alloc and a row
# with only Bg each fail the Subrow gate.
crucible_neg_compile_multi_test(neg_recipe_pool_ctor_pure_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<>\\]")
crucible_neg_compile_multi_test(neg_recipe_pool_ctor_alloc_only_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>\\]")
crucible_neg_compile_multi_test(neg_recipe_pool_ctor_bg_only_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>\\]")

# The RecipeRegistry constructor takes a minted BorrowedRef<RecipePool,
# Brand>.  A raw RecipePool* and a borrow of a different type must fail
# at the constructor.
crucible_neg_compile_multi_test(neg_recipe_registry_pool_raw_pointer
  "no matching function for call to .crucible::RecipeRegistry::RecipeRegistry"
  "mismatched types .fixy::BorrowedRef<crucible::RecipePool, Brand>. and .crucible::RecipePool\\*.")
crucible_neg_compile_multi_test(neg_recipe_registry_pool_wrong_borrow
  "no matching function for call to .crucible::RecipeRegistry::RecipeRegistry"
  "mismatched types .crucible::RecipePool. and .crucible::Arena.")

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

# forge/_wip/Phases/Comm.h, the vendor-neutral comm phase sketch.
#   * neg_forge_comm_phase_raw_compute: fusion admission requires source::Ir001.
#   * neg_forge_comm_phase_send_as_collective: ReduceOnRecv requires collective
#     communication, not point-to-point Send.
#   * neg_forge_comm_phase_oversubscribed: combined resource row must FitCog.
#   * neg_forge_comm_phase_strict_compress: BITEXACT_STRICT rejects lossy
#     compression at the compile-time recipe gate.
#   * neg_forge_comm_phase_unknown_pattern: a pattern with no shape row has
#     no shape, so no gate admits it.
#   * neg_forge_comm_phase_wrong_lane: a decision of the FUSE phase does not
#     pass where a later phase is demanded.
crucible_neg_compile_test(neg_forge_comm_phase_raw_compute
  "could not convert|no matching function|DeclaredIr001Node|Tagged|source::Ir001")
crucible_neg_compile_test(neg_forge_comm_phase_send_as_collective
  "static assertion failed|constraints not satisfied|CommFusionEligible|ReduceOnRecv|Ir001CollectiveKind")
crucible_neg_compile_test(neg_forge_comm_phase_oversubscribed
  "static assertion failed|constraints not satisfied|CommFusionEligible|FitsCog|999")
crucible_neg_compile_test(neg_forge_comm_phase_strict_compress
  "constraints not satisfied|CommFusionRecipeAllowed|BITEXACT_STRICT|CompressBeforeSend")
crucible_neg_compile_test(neg_forge_comm_phase_unknown_pattern
  "unreachable|not a constant expression|comm_fusion_shape")
crucible_neg_compile_test(neg_forge_comm_phase_wrong_lane
  "could not convert|no matching function|ForgePhase")

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
  ".crucible::forge::ir001::Ir001Node<crucible::forge::ir001::Ir001OpKind::AllReduce, crucible::forge::ir001::CollectiveAttrs, foundation::effects::Row<> >. is not derived from .fixy::Tagged<Node, fixy::tags::source::Ir001>.")
crucible_neg_compile_test(neg_mimic_network_backend_compute_node
  "static assertion failed|NetworkKernelNode|ir001_network_kernel_kind")
crucible_neg_compile_test(neg_mimic_network_backend_cross_vendor
  "could not convert .am. from .Tagged<\\[\\.\\.\\.\\],crucible::mimic::_wip::network::wip_source::AmNetwork>. to .Tagged<\\[\\.\\.\\.\\],crucible::mimic::_wip::network::wip_source::NvNetwork>.")
crucible_neg_compile_test(neg_mimic_network_backend_wrong_cog
  "static assertion failed|BackendAcceptsCog|Mellanox|Gpu")

# mimic/Semaphore.h: mint_device_semaphore is the one door to a descriptor.
#   * neg_mimic_semaphore_backend_none, _backend_portable: SemaphoreBackend
#     refuses a backend that names no one device, in the mint and in the
#     class.
#   * neg_mimic_semaphore_temporary_counter: the mint refuses a temporary
#     counter, because the descriptor borrows it.
#   * neg_mimic_semaphore_forged, _default_construct: no constructor of the
#     descriptor goes around the mint.
crucible_neg_compile_multi_test(neg_mimic_semaphore_backend_none
  "no matching function for call to .mint_device_semaphore<foundation::algebra::lattices::VendorBackend::None>\\(std::atomic<long unsigned int>&, int\\)."
  "the expression .Backend != foundation::algebra::lattices::VendorLattice::bottom\\(\\)\\(\\) \\[with Backend = foundation::algebra::lattices::VendorBackend::None\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_mimic_semaphore_backend_portable
  "template constraint failure for .template<foundation::algebra::lattices::VendorBackend Backend>  requires  SemaphoreBackend<Backend> class crucible::mimic::DeviceSemaphore."
  "the expression .Backend != foundation::algebra::lattices::VendorLattice::top\\(\\)\\(\\) \\[with Backend = foundation::algebra::lattices::VendorBackend::Portable\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_mimic_semaphore_temporary_counter
  "use of deleted function .constexpr crucible::mimic::DeviceSemaphore<Backend> crucible::mimic::mint_device_semaphore\\(std::atomic<long unsigned int>&&, uint64_t\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU. uint64_t = long unsigned int\\]."
  ".: The DeviceSemaphore borrows its counter, so a temporary counter dangles at the end of the full expression. Bind the counter to a name first.")
crucible_neg_compile_multi_test(neg_mimic_semaphore_forged
  ".constexpr crucible::mimic::DeviceSemaphore<Backend>::DeviceSemaphore\\(std::atomic<long unsigned int>&, uint64_t\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU. uint64_t = long unsigned int\\]. is private within this context"
  "mimic/Semaphore.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_multi_test(neg_mimic_semaphore_default_construct
  "use of deleted function .crucible::mimic::DeviceSemaphore<Backend>::DeviceSemaphore\\(\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU\\]."
  ".: A DeviceSemaphore borrows a counter. Take one from mint_device_semaphore.")

# ReplayEngine::init takes the pool as fixy::BorrowedRef<const
# PoolAllocator>.  A raw pointer and a borrow of another owner type are
# both refused at the init boundary.
crucible_neg_compile_multi_test(neg_replay_engine_pool_raw_pointer
  "cannot convert .const crucible::PoolAllocator\\*. to .crucible::ReplayEngine::PoolBorrow."
  "neg_replay_engine_pool_raw_pointer.cpp:15")
crucible_neg_compile_multi_test(neg_replay_engine_pool_wrong_borrow
  "cannot convert .BorrowedRef<crucible::RegionNode>. to .BorrowedRef<const crucible::PoolAllocator>."
  "neg_replay_engine_pool_wrong_borrow.cpp:15")

# Production-call-site fence: a hot-path consumer that requires the
# Pool allocation class refuses a Heap band.  This is the rejection that
# carries the no-malloc-on-hot-path rule at the slot_ptr_pinned to
# consumer boundary.  Without it, jemalloc allocations (50-200 ns
# setup) could flow into hot-path sites budgeted at a few ns per call.
crucible_neg_compile_multi_test(neg_alloc_class_heap_at_pool_fence
  "no matching function for call to .hot_path_slot_consumer"
  "the expression .satisfies_v<Slot, foundation::algebra::lattices::AllocClassTag::Pool> \\[with Slot = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::AllocClassLattice::At<foundation::algebra::lattices::AllocClassTag::Heap>, int\\*>\\]. evaluated to .false.")

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
# The content key folds the printed name of the function and of each
# argument type, so it refuses a closure's static invoker and a closure
# argument type: either prints a name that depends on the translation
# unit.
crucible_neg_compile_multi_test(neg_computation_cache_closure_invoker
  "computation_cache_key: the function, its signature or an argument type has no stable identity"
  "HasStableKeyIdentity")
crucible_neg_compile_multi_test(neg_computation_cache_closure_argument
  "computation_cache_key: the function, its signature or an argument type has no stable identity"
  "HasStableKeyIdentity")

# Every Cipher operation that reaches the store takes a context whose row
# admits IO and Block: open, mint_open_view, record_event and
# persist_session_events.  Each fixture below refuses one context, and
# the second regex names the context the gate refused.  The four
# record_event rows hold each atom of the gate on its own: IO alone,
# Block alone, Alloc alone, and the background drain row, whose Bg atom
# implies neither IO nor Block.
crucible_neg_compile_multi_test(neg_cipher_record_event_pure_row
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_multi_test(neg_cipher_record_event_io_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::IO> >")
crucible_neg_compile_multi_test(neg_cipher_record_event_block_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Block> >")
crucible_neg_compile_multi_test(neg_cipher_record_event_alloc_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Alloc> >")
crucible_neg_compile_multi_test(neg_cipher_record_event_bg_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >")
crucible_neg_compile_multi_test(neg_cipher_open_view_hot_fg_ctx
  "no matching function for call to .crucible::Cipher::mint_open_view.foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. const."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_multi_test(neg_cipher_open_view_init_ctx
  "no matching function for call to .crucible::Cipher::mint_open_view.fixy::ColdInitCtx. const."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")
crucible_neg_compile_multi_test(neg_cipher_open_ctx_without_block
  "no matching function for call to .crucible::Cipher::open.const fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")
crucible_neg_compile_multi_test(neg_cipher_open_non_context
  "no matching function for call to .crucible::Cipher::open.foundation::effects::Test, "
  "the expression .is_exec_ctx_v<T> .with T = foundation::effects::Test.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_persist_session_events_fg_ctx
  "no matching function for call to .crucible::Cipher::persist_session_events."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_multi_test(neg_cipher_persist_session_events_compile_ctx
  "no matching function for call to .crucible::Cipher::persist_session_events."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")

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

# ─── The row gate of MetaLog::try_append_pure ───────────────────────
# MetaLog::try_append_pure<CallerRow> requires IsPure<CallerRow>, that
# is, CallerRow is a subrow of Row<>.  There is one fixture for each of
# the six atoms (Alloc, IO, Block, Bg, Init and Test), one for a row of
# two atoms, and one for the full row.  If the gate widens to admit one
# atom or one combination of atoms, the fixture for it compiles.  Each
# regex names the row that the gate refused.
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_io
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::IO> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_bg
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Bg> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_alloc
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Alloc> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_block
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Block> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Block>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_init
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Init> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Init>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_test
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Test> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Test>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_io_block
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>.")
crucible_neg_compile_multi_test(neg_metalog_try_append_pure_full_row
    "no matching function for call to .crucible::MetaLog::try_append_pure<fixy::AllRow>"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, ")

# ─── FOUND-I16 TraceRing row-fence — full per-axis matrix on both legs ─
# TraceRing::try_append_pure<CallerRow> AND TraceRing::drain_pure<CallerRow>
# both require IsPure<CallerRow>.  Sibling of the MetaLog::try_append_pure
# matrix above.  16 fixtures = 8 per direction (per-axis × 6 atoms +
# multi-atom + saturation).  Pins both directions of the SPSC ring.
# Producer side:
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_io
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_init
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_test
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_try_append_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
# Consumer side:
crucible_neg_compile_multi_test(neg_tracering_drain_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_io
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_init
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_test
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_multi_test(neg_tracering_drain_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")

# Vigil::dispatch_op_pure takes crucible::VigilFgCtx, the context of the
# Vigil's producer claim.  Each fixture passes another context, or none.
# The first regex pins the refused context or call, and the second pins the
# parameter that refuses it.
set(VIGIL_PURE_TO "to .const crucible::VigilFgCtx&.")
set(VIGIL_PURE_PARAM
  "initializing argument 1 of .crucible::DispatchResult crucible::Vigil::dispatch_op_pure\\(const crucible::VigilFgCtx&, ")
set(VIGIL_PURE_ROW_TAIL "foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_bg_ctx
  "cannot convert .const fixy::BgLoadCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_init_ctx
  "cannot convert .const fixy::InitLoadCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_test_ctx
  "cannot convert .const fixy::TestRunnerCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_multi_test(neg_vigil_dispatch_op_pure_no_ctx
  "no matching function for call to .crucible::Vigil::dispatch_op_pure\\(fixy::Tagged<const crucible::TraceRing::Entry\\*, fixy::tags::vessel_trust::Validated>, crucible::TensorMeta\\*, int\\)."
  "crucible::Vigil::dispatch_op_pure\\(const crucible::VigilFgCtx&, crucible::TraceRing::ValidatedEntryPtr, ")

# BackgroundThread::run_in_row(ctx) requires CtxAdmits<Ctx, Row<Bg,
# Alloc, IO, Block>>.  Each fixture hands in a context whose row lacks
# one required atom or more.  The first regex pins the refused context,
# the second pins the gate and the row it demands.
set(BG_RUN_GATE "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >.")
set(BG_RUN_CALL "no matching function for call to .crucible::BackgroundThread::run_in_row\\(const foundation::effects::ExecCtx<")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_empty
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_bg_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_alloc_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_io_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_block_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_missing_bg
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_missing_alloc
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_missing_io
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_missing_block
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_init_only
  "${BG_RUN_CALL}foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_multi_test(neg_background_thread_run_in_row_test_only
  "${BG_RUN_CALL}foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >&\\)" "${BG_RUN_GATE}")

# The tier mints keep their direction: mint_promote goes up the chain
# only and mint_demote goes down only.  Each mint and mint_restore also
# refuses a payload that cannot move, and mint_restore refuses a cold
# band over another value type and a payload with no content hash
# projection, because without the projection the hash check would run
# for ContentHash alone.
crucible_neg_compile_multi_test(neg_cipher_tier_promote_hot_to_cold
  "no matching function for call to .mint_promote<foundation::algebra::lattices::CipherTierTag::Hot, foundation::algebra::lattices::CipherTierTag::Cold>"
  "the expression .can_promote_tier_v<From, To> .with From = foundation::algebra::lattices::CipherTierTag::Hot. To = foundation::algebra::lattices::CipherTierTag::Cold.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_tier_promote_unmovable_payload
  "required for the satisfaction of .PromotableTier<From, To, T>. .with From = foundation::algebra::lattices::CipherTierTag::Cold. To = foundation::algebra::lattices::CipherTierTag::Warm. T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_multi_test(neg_cipher_tier_demote_cold_to_hot
  "no matching function for call to .mint_demote<foundation::algebra::lattices::CipherTierTag::Cold, foundation::algebra::lattices::CipherTierTag::Hot>"
  "the expression .can_demote_tier_v<From, To> .with From = foundation::algebra::lattices::CipherTierTag::Cold. To = foundation::algebra::lattices::CipherTierTag::Hot.. evaluated to .false.")
crucible_neg_compile_multi_test(neg_cipher_tier_demote_unmovable_payload
  "required for the satisfaction of .DemotableTier<From, To, T>. .with From = foundation::algebra::lattices::CipherTierTag::Hot. To = foundation::algebra::lattices::CipherTierTag::Warm. T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_multi_test(neg_cipher_tier_restore_unmovable_payload
  "required for the satisfaction of .RestorableTier<T>. .with T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_multi_test(neg_cipher_tier_restore_wrong_value_type
  "could not convert .std::declval<foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::CipherTierLattice::At<foundation::algebra::lattices::CipherTierTag::Cold>, int> >"
  "from .Graded<.....,.....,int>. to .Graded<.....,.....,crucible::ContentHash>.")
crucible_neg_compile_multi_test(neg_cipher_tier_restore_no_hash_projection
  "no matching function for call to .mint_restore<ProductionPayload>"
  "required for the satisfaction of .RestorableHashed<T>. .with T = ProductionPayload.")

# State-discipline: SealedView cannot satisfy the MutableView parameter
# of register_name / register_op — the only typed registration path
# requires proof that the table is in the Mutable phase.
#
# [GCC-WRAPPER-TEXT] Production-side typed-view rejection.  The direct
# signature requires MutableView; SealedView reaches GCC's conversion
# diagnostic before any runtime path can exist.
crucible_neg_compile_multi_test(neg_schema_register_with_sealed_view
  "cannot convert .sv. \\(type .fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Sealed>.\\) to type .const crucible::SchemaTable::MutableView&."
  "neg_schema_register_with_sealed_view.cpp:14")
crucible_neg_compile_test(neg_ckernel_register_with_sealed_view
  "cannot convert.*Sealed.*MutableView")
# A field keeps the proof of a sealed view after the table is gone, so the
# field audit refuses it.
crucible_neg_compile_multi_test(neg_ckernel_mint_sealed_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<SealedContainer>\\(\\). evaluates to false")

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
crucible_neg_compile_multi_test(neg_ckernel_mint_mutable_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingContainer>\\(\\). evaluates to false")

# Each carrier has its own ScopedView type, its own view_ok overload and its
# own no_scoped_view_field_check instance, so the kernel table fixtures do
# not see a defect in the schema table gate.  The HS14 scanner counts a
# fixture for the carrier whose class it names.  This pair is the schema
# table's own witness.
crucible_neg_compile_multi_test(neg_schema_mint_mutable_view_to_sealed_overload
  "invalid initialization of reference of type .const crucible::SchemaTable::SealedView&."
  "from expression of type .const fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Mutable>.")
crucible_neg_compile_multi_test(neg_schema_mint_mutable_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingSchemaContainer>\\(\\). evaluates to false")

# A sealed table mints no mutable view.  The view comes back optional, so
# a registration that forgets the seal does not compile.
crucible_neg_compile_multi_test(neg_schema_mutable_view_unchecked
  "\\(type .std::optional<fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Mutable> >.\\) to type .const crucible::SchemaTable::MutableView&."
  "neg_schema_mutable_view_unchecked.cpp:12")
crucible_neg_compile_multi_test(neg_ckernel_mutable_view_unchecked
  "cannot convert .std::optional<fixy::ScopedView<crucible::CKernelTable, crucible::ckernel_state::Mutable> >."
  "neg_ckernel_mutable_view_unchecked.cpp:12")

# Lock-free readers read a sealed table, so every write goes through the
# view and the seal phase.  The entry arrays are private, and clear(), which
# opens a sealed table again, takes the test context.
crucible_neg_compile_multi_test(neg_schema_table_entries_private
  "is private within this context"
  "crucible::SchemaTable::entries_")
crucible_neg_compile_multi_test(neg_ckernel_table_entries_private
  "is private within this context"
  "crucible::CKernelTable::entries_")
crucible_neg_compile_multi_test(neg_schema_table_clear_needs_test_context
  "no matching function for call to"
  "crucible::SchemaTable::clear..")
crucible_neg_compile_multi_test(neg_ckernel_table_clear_needs_test_context
  "no matching function for call to"
  "crucible::CKernelTable::clear..")

# The names and schemas in the two tables come from the ops that a Vigil
# records, so a mutable view, and a trace load that registers names, ask
# for the context of a Vigil's producer claim.
crucible_neg_compile_multi_test(neg_schema_mint_mutable_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_schema_mint_mutable_view_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_schema_mint_mutable_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_schema_mint_mutable_view_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_ckernel_mint_mutable_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_ckernel_mint_mutable_view_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_ckernel_mint_mutable_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_ckernel_mint_mutable_view_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_trace_loader_names_unbranded_ctx
  "no matching function for call to .load_trace.const fixy::TestRunnerCtx&, foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "to type .const crucible::VigilFgCtx&."
  "neg_trace_loader_names_unbranded_ctx.cpp:12")
crucible_neg_compile_multi_test(neg_trace_loader_names_other_brand
  "no matching function for call to .load_trace.const fixy::TestRunnerCtx&, foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >"
  "to type .const crucible::VigilFgCtx&."
  "neg_trace_loader_names_other_brand.cpp:17")
# A trace load reads a file, so it asks for a context that owns IO and
# Block.  The foreground context of a Vigil owns neither.
crucible_neg_compile_multi_test(neg_trace_loader_refuses_foreground_io
  "no matching function for call to .load_trace.foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<crucible::Vigil>, foundation::effects::Row<> >"
  "required for the satisfaction of .CtxFitsFileOpen<Ctx>."
  "neg_trace_loader_refuses_foreground_io.cpp:10")

# The view mints of CrucibleContext, PoolAllocator and ReplayEngine.  Each
# carrier has one typed view, so the per-state mismatch used for
# CKernelTable and SchemaTable does not apply.  Two fixtures per mint:
#   * _cross_carrier: the view is offered where a CKernelTable view is
#     required, and the two fixy::ScopedView instantiations do not
#     convert, so the carrier is locked into the type.
#   * _in_field: a class that stores the view fails the field audit
#     no_scoped_view_field_check, which sees through the view alias.
crucible_neg_compile_multi_test(neg_crucible_context_mint_compiled_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::CrucibleContext, crucible::ctx_mode::Compiled>.")
crucible_neg_compile_multi_test(neg_crucible_context_mint_compiled_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingCtxContainer>\\(\\). evaluates to false")

crucible_neg_compile_multi_test(neg_pool_allocator_mint_initialized_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::PoolAllocator, crucible::pool_state::Initialized>.")
crucible_neg_compile_multi_test(neg_pool_allocator_mint_initialized_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingPoolContainer>\\(\\). evaluates to false")

crucible_neg_compile_multi_test(neg_replay_engine_mint_active_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::ReplayEngine, crucible::engine_state::Active>.")
crucible_neg_compile_multi_test(neg_replay_engine_mint_active_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingEngineContainer>\\(\\). evaluates to false")

# The foreground views are read only by the thread that holds the claim
# of the Vigil, so each gate asks for the context of the Vigil's brand.
# A context with no brand and a context of another state are refused.
crucible_neg_compile_multi_test(neg_crucible_context_compiled_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_crucible_context_compiled_view_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_crucible_context_compiled_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_crucible_context_compiled_view_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_replay_engine_active_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_replay_engine_active_view_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_replay_engine_active_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_replay_engine_active_view_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_pool_allocator_initialized_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_pool_allocator_initialized_view_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_pool_allocator_initialized_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_pool_allocator_initialized_view_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_vigil_output_ptr_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_output_ptr_unbranded_ctx.cpp:11")
# ring() and meta_log() hand out the producer surface of the ring and of the
# metadata log, so each asks for the context of the Vigil's producer claim.
crucible_neg_compile_multi_test(neg_vigil_ring_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_ring_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_vigil_ring_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_ring_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_vigil_meta_log_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_meta_log_unbranded_ctx.cpp:11")
crucible_neg_compile_multi_test(neg_vigil_meta_log_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_meta_log_other_brand.cpp:15")
crucible_neg_compile_multi_test(neg_vigil_mint_producer_context_const
  "passing .const crucible::Vigil. as .this. argument discards qualifiers"
  "neg_vigil_mint_producer_context_const.cpp:10")
crucible_neg_compile_multi_test(neg_vigil_producer_context_to_unbranded_gate
  "invalid initialization of reference of type .const fixy::HotFgCtx&."
  "from expression of type .crucible::VigilFgCtx."
  "neg_vigil_producer_context_to_unbranded_gate.cpp:17")

# [GCC-WRAPPER-TEXT] Same class as schema/ckernel above — production-
# side overload-set rejection in Cipher.  Same accepted-intrinsic
# rationale.
crucible_neg_compile_multi_test(neg_cipher_wrong_tag
  "cannot convert .fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Sealed>. to .const crucible::Cipher::OpenView&."
  "aka .const fixy::ScopedView<crucible::Cipher, crucible::cipher_state::Open>&.")

# deserialize_federation_entry takes the peer token of the reading
# organization.  A call with no token, or with the token of another
# organization, does not compile.
crucible_neg_compile_test(neg_federation_deserialize_without_permission
    "no matching function|candidate expects")
crucible_neg_compile_test(neg_federation_deserialize_wrong_org_permission
    "no matching function|could not convert|invalid initialization")

# compose_at_branch_t<P, I, Q> — branch-asymmetric composition (#378).
# Replaces only branch I's continuation in the first reachable
# Select<Bs...> / Offer<Bs...> in P's spine.  Two named diagnostics
# fire on misuse: out-of-range I and no-choice-in-spine.
crucible_test(test_session_compose_at_branch)

crucible_neg_compile_multi_test(neg_vigil_copy_attempt
    "use of deleted function .crucible::Vigil::Vigil\\(const crucible::Vigil&\\)."
    "Vigil owns the runtime organism. not copyable")

# Vigil::AlignmentPos is fixy::Refined<bounded_above<5>, uint8_t>, and
# Vigil::alignment_pos_at is its one door.  A constant above the bound
# stops the constant evaluation of the mint.  The first fixture passes 6,
# one past the bound.  The second passes 255, the largest byte.
crucible_neg_compile_multi_test(neg_vigil_alignment_pos_above_max
    "in .constexpr. expansion of .crucible::Vigil::alignment_pos_at\\(6\\)."
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<5>\\(\\), unsigned char>\\(pos\\)."
    "error: .__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_multi_test(neg_vigil_alignment_pos_uint8_max
    "in .constexpr. expansion of .crucible::Vigil::alignment_pos_at\\(255\\)."
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<5>\\(\\), unsigned char>\\(pos\\)."
    "error: .__builtin_trap\\(\\). is not a constant expression")

# ── GAPS-061 — PermissionedMetaLog role-discrimination tests ───────
#
# MetaLog is append-only on the foreground side and drain-only on the
# background side.  The permissioned facade makes that distinction
# structural rather than convention-based.
crucible_neg_compile_test(neg_metalog_producer_drain
    "has no member named.*try_drain_one")

crucible_neg_compile_test(neg_metalog_consumer_append
    "has no member named.*try_append")

# HS14 for the two MetaLog session mints.  Each gate is one concept, and
# each fixture fails one clause of it: the context, the handle passed as
# an lvalue, a type that is not a permissioned log, and the handle of the
# other role.
crucible_neg_compile_multi_test(neg_metalog_session_producer_not_a_context
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int."
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_producer_lvalue_handle
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .same_as<Handle, typename Log::ProducerHandle>. .with Handle = crucible::PermissionedMetaLog<producer_lvalue_fixture::Tag>::ProducerHandle&"
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_consumer_not_a_log
    "no matching function for call to .mint_metalog_consumer_session<int>"
    "satisfaction of .MetaLogSessionSurface<Log>. .with Log = int."
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_consumer_wrong_role
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .same_as<Handle, typename Log::ConsumerHandle>. .with Handle = crucible::PermissionedMetaLog<consumer_wrong_role_fixture::Tag>::ProducerHandle"
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_producer_not_a_log
    "no matching function for call to .mint_metalog_producer_session<int>"
    "satisfaction of .MetaLogSessionSurface<Log>. .with Log = int."
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_consumer_not_a_context
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int."
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_consumer_lvalue_handle
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .same_as<Handle, typename Log::ConsumerHandle>. .with Handle = crucible::PermissionedMetaLog<consumer_lvalue_fixture::Tag>::ConsumerHandle&"
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_multi_test(neg_metalog_session_producer_wrong_role
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .same_as<Handle, typename Log::ProducerHandle>. .with Handle = crucible::PermissionedMetaLog<producer_wrong_role_fixture::Tag>::ConsumerHandle"
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")

# The accessor that makes each handle takes the Permission of its own
# role, and that parameter type is its whole admission.  Each fixture
# passes the Permission of the other role.
crucible_neg_compile_multi_test(neg_metalog_producer_wrong_permission
    "cannot convert .*Permission<crucible::metalog_tag::Consumer<producer_wrong_permission_fixture::Tag>, .* to .foundation::permissions::Permission<crucible::metalog_tag::Producer<producer_wrong_permission_fixture::Tag>, foundation::brand::DefaultBrand>&&"
    "initializing argument 1 of .*PermissionedMetaLog<UserTag>::producer.foundation::permissions::Permission<crucible::metalog_tag::Producer<UserTag> >&&. .with UserTag = producer_wrong_permission_fixture::Tag.")
crucible_neg_compile_multi_test(neg_metalog_consumer_wrong_permission
    "cannot convert .*Permission<crucible::metalog_tag::Producer<consumer_wrong_permission_fixture::Tag>, .* to .foundation::permissions::Permission<crucible::metalog_tag::Consumer<consumer_wrong_permission_fixture::Tag>, foundation::brand::DefaultBrand>&&"
    "initializing argument 1 of .*PermissionedMetaLog<UserTag>::consumer.foundation::permissions::Permission<crucible::metalog_tag::Consumer<UserTag> >&&. .with UserTag = consumer_wrong_permission_fixture::Tag.")

# SingletonInitGraph.h: the gate refuses a cycle, and it refuses an edge
# to a singleton that the registry does not hold.
crucible_neg_compile_multi_test(neg_singleton_init_graph_cycle
    "template constraint failure for .template<class Registry, class Edges>  requires  AcyclicInitGraph<Registry, Edges> struct crucible::singleton_init_graph::init_graph."
    "the expression .\\(is_acyclic_<Registry>\\)\\(static_cast<Edges\\*>\\(nullptr\\)\\) \\[with Registry = std::tuple<fixy::atom::global::singleton<neg_singleton_init_graph_cycle_types::first_tag>.*\\]. evaluated to .false.")
crucible_neg_compile_multi_test(neg_singleton_init_graph_unregistered_edge
    "template constraint failure for .template<class Registry, class Edges>  requires  AcyclicInitGraph<Registry, Edges> struct crucible::singleton_init_graph::init_graph."
    "required for the satisfaction of .EdgesNameRegisteredSingletons<Registry, Edges>. \\[with Registry = std::tuple<fixy::atom::global::singleton<neg_singleton_init_graph_unregistered_edge_types::registered_tag> >.*\\]"
    "the expression .\\(edges_name_registered_<Registry>\\)")

# ─── GAPS-094 audit follow-up — Vessel ABI Tagged direction fence ──
#
# from_typed() takes TypedHandle = Tagged<Vigil*, source::ABIBoundary>
# only.  This fixture witnesses that calling it with any other source
# tag (here: source::External) is rejected by the type system, so a
# refactor "promoting" the helper to accept any Tagged<Vigil*, *> would
# turn red here.  Closes the audit's GAPS-094 HS14 deficit.
crucible_neg_compile_test(neg_vessel_typed_handle_wrong_tag
    "could not convert .wrong_tag. from .*source::External.* to .*source::ABIBoundary")

# ─── GAPS-095 — Vessel ABI typed-meta direction fence ─────────────
#
# The matching neg-compile witness for as_meta_typed / metas_from_typed
# (the second helper added by GAPS-095 to migrate the TensorMeta cast
# alongside the Vigil handle).  Pins that wrong-tag values cannot
# launder as ABIBoundary-tagged metas — same architectural rationale
# as the handle fence above, applied to the meta-array entry point.
crucible_neg_compile_test(neg_vessel_typed_meta_wrong_tag
    "could not convert .wrong_tag. from .*source::External.* to .*source::ABIBoundary")

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
crucible_neg_compile_multi_test(neg_schema_name_typed_wrong_tag
    "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Interned>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. requested"
    "neg_schema_name_typed_wrong_tag.cpp:25")

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
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (2) Send Transferable<T, X> from a handle whose PS does not contain X.
crucible_neg_compile_session_test(neg_psh_send_transferable_without_holding
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*payload type T requires")

# (3) Send Returned<T, X> from a handle whose PS does not contain X.
crucible_neg_compile_session_test(neg_psh_send_returned_without_holding
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*payload type T requires")

# (4) Loop body that DRAINS a permission per iteration without surrender.
#     Continue's balance assert fires.
crucible_neg_compile_session_test(neg_psh_loop_drains_perm_per_iteration
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (5) Loop body that GAINS a permission per iteration without surrender
#     (the symmetric inverse of #4).
crucible_neg_compile_session_test(neg_psh_loop_gains_perm_per_iteration
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (6) Recv Transferable then close at End without surrender.
crucible_neg_compile_session_test(neg_psh_recv_imbalance_at_end
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (7) Select with branch that closes without surrendering — Decision D4
#     STRUCTURAL convergence: close on the imbalanced branch fires.
crucible_neg_compile_session_test(neg_psh_select_diverging_branch
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*reached End")

# (8) Nested Loop where the OUTER body balances but the INNER body does
#     not — exercises Decision D3's loop-context shadowing.
crucible_neg_compile_session_test(neg_psh_nested_loop_inner_imbalance
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (9) Multi-step body where MOST operations balance but the LAST step
#     leaves PS imbalanced.  Subtle: balanced sub-chains hide a trailing
#     half-pair that the type system catches via Continue's balance assert.
crucible_neg_compile_session_test(neg_psh_subtle_chain_imbalance
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*Loop body's terminal PermSet")

# (10) perm_set_union_t with overlapping operands — disjoint-union assert
#      fires.  CSL frame rule violation: a permission cannot be held by
#      two participants simultaneously.
crucible_neg_compile_session_test(neg_perm_set_union_overlap
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermSet\\.h.*static assertion failed.*PermissionImbalance.*disjoint operands")

# GAPS-058: PermissionedSessionHandle × Delegate/Accept bridge.  These
# pin the DelegatedSession<P, InnerPS> handoff boundary so higher-order
# session transfer cannot fabricate, duplicate, or leak InnerPS.
crucible_neg_compile_session_test(neg_psh_delegate_inner_ps_not_in_sender
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*PermSet does not contain inner_ps tokens")

crucible_neg_compile_session_test(neg_psh_accept_inner_ps_already_owned
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*inner_ps already owned")

crucible_neg_compile_session_test(neg_psh_delegate_inner_proto_unbalanced
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermissionedSession\\.h.*static assertion failed.*PermissionImbalance.*is_permission_balanced_v<InnerProto> against InnerPS rejects")

crucible_neg_compile_session_test(neg_psh_delegate_to_self
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ProtocolViolation_Self_Loop|self-loop")

# GAPS-059: ctx-bound permissioned session mint.  These fixtures pin the
# unified mint boundary: Ctx is mandatory, row admission sees through
# permission payload wrappers, initial PermSet must locally close, and a
# Permission<Tag> cannot be consumed twice.
crucible_neg_compile_session_test(neg_mint_session_no_ctx
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "mint_session<Proto>\\(resource\\) is removed")

crucible_neg_compile_session_test(neg_mint_permissioned_session_no_ctx
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "no matching function for call to.*mint_permissioned_session|candidate expects at least 2 arguments")

crucible_neg_compile_session_test(neg_psh_direct_construct
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "no matching function for call to.*PermissionedSessionHandle")

crucible_neg_compile_session_test(neg_psh_fabricate_permset
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "no matching function for call to.*PermissionedSessionHandle")

crucible_neg_compile_session_test(neg_mint_session_ctx_no_admit_proto
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_transferable_io_payload_to_pure_ctx
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_borrowed_io_payload_to_pure_ctx
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_send_returned_io_payload_to_pure_ctx
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsPermissionedProtocol|CtxFitsProtocol")

# GAPS-076: paired channel minting validates both local protocols.  Endpoint A
# runs Proto; endpoint B runs dual(Proto).  These fixtures pin sender-side row
# admission, receiver-side row admission, continuation drift, and Capability
# payload rows.
crucible_neg_compile_session_test(neg_mint_channel_receiver_no_admit
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_sender_no_admit
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_recv_row_drift
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_capability_unauthorized_receiver
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsChannel|CtxFitsPermissionedProtocol|CtxFitsProtocol")

crucible_neg_compile_session_test(neg_mint_channel_requires_ctx
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "mint_channel<Proto>\\(resource_a, resource_b\\) is removed")

# fixy-M-17 — HS14 floor for mint_session_view<Tag>(handle).
# Two fixtures × distinct mismatch classes: #1 wrong protocol position
# (handle at Send, request AtRecv); #2 unrecognized position tag
# (BogusPositionTag).  Both fire mint_session_view's
# `requires HandleIsAt<Handle, Tag>` clause.
crucible_neg_compile_session_test(neg_mint_session_view_wrong_position
    "constraints.*not satisfied|no matching function"
    "HandleIsAt|handle_is_at|mint_session_view")
crucible_neg_compile_session_test(neg_mint_session_view_invalid_tag
    "constraints.*not satisfied|no matching function"
    "HandleIsAt|handle_is_at|mint_session_view")

crucible_neg_compile_session_test(neg_mint_session_perms_unbalanced
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "CtxFitsPermissionedProtocol|permission_flow_closes")

crucible_neg_compile_session_test(neg_mint_session_perm_double_consume
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "PermSet\\.h.*static assertion failed.*PermissionImbalance.*unique Tags")

# GAPS-060: Chase-Lev owner/thief typed-session facade.  The thief
# protocol is Recv-only; the owner Recv branch is pop_bottom, not the
# thief borrowed-steal transport.
crucible_neg_compile_session_test(neg_chaselev_thief_session_push
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_chaselev_owner_session_steal
    "no matching function|constraints.*not satisfied"
    "try_steal")

# GAPS-081: generic SubstrateSessionBridge and Endpoint preserve the
# Chase-Lev owner/thief protocol split.  The thief direction remains
# Recv-only through both factories.
crucible_neg_compile_session_test(neg_chaselev_substrate_thief_send
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_chaselev_endpoint_thief_try_send
    "constraints.*not satisfied"
    "no matching function for call|candidate")

# GAPS-082: indexed ShardedGrid substrate sessions.  ShardId<I>
# selects the statically-indexed producer/consumer handle, and role
# misuse remains rejected through the generic bridge.
crucible_neg_compile_session_test(neg_sharded_grid_substrate_consumer_send
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_sharded_grid_substrate_producer_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_sharded_grid_bridge_producer_shard_oob
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ShardedGridBridge_ProducerShardOutOfRange|producer shard I")

# GAPS-083: indexed CalendarGrid substrate sessions.  CalendarProducerId<P>
# selects a producer row; CalendarConsumerId selects the single drain
# consumer.  Role misuse remains rejected through the generic bridge.
crucible_neg_compile_session_test(neg_calendar_grid_substrate_consumer_send
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_calendar_grid_substrate_producer_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_calendar_grid_bridge_producer_row_oob
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "CalendarGridBridge_ProducerRowOutOfRange|producer row P")

# GAPS-084: indexed ShardedCalendarGrid substrate sessions.  ShardId<S>
# selects the shard-local producer/consumer calendar handle; role misuse
# and out-of-range shard IDs remain compile-time failures.
crucible_neg_compile_session_test(neg_sharded_calendar_grid_substrate_consumer_send
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_sharded_calendar_grid_substrate_producer_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_sharded_calendar_grid_bridge_shard_oob
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "ShardedCalendarGridBridge_ShardOutOfRange|shard S")

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
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*send")

crucible_neg_compile_session_test(neg_mpmc_session_producer_session_recv
    "static assertion failed|constraints.*not satisfied|no matching function|has no member named"
    "has no member named.*recv")

crucible_neg_compile_session_test(neg_mpmc_session_surface_rejects_non_channel
    "constraints.*not satisfied|no type named"
    "MpmcChannelSessionSurface|the required expression")

crucible_neg_compile_session_test(neg_mpmc_session_handle_copy
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "ProducerHandle owns a producer-pool refcount share")

# Each endpoint session mint states its context gate in its clause: the
# context must admit the protocol with no permission in hand.  The test asks
# every mint whether a context fits.  Each fixture proves the mint itself
# refuses a foreground context for a payload that carries the background row,
# one on the send side and one on the receive side of each channel family.
crucible_test(test_session_endpoint_ctx_gate)
crucible_neg_compile_session_test(neg_endpoint_gate_spsc_producer_bg_row
    "no matching function for call to .mint_producer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_spsc_consumer_bg_row
    "no matching function for call to .mint_consumer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_mpmc_producer_bg_row
    "no matching function for call to .mint_mpmc_producer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_mpmc_consumer_bg_row
    "no matching function for call to .mint_mpmc_consumer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_snapshot_writer_bg_row
    "no matching function for call to .mint_snapshot_writer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_snapshot_reader_bg_row
    "no matching function for call to .mint_snapshot_reader_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_swmr_writer_bg_row
    "no matching function for call to .mint_writer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_swmr_reader_bg_row
    "no matching function for call to .mint_reader_runtime_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_deque_owner_bg_row
    "no matching function for call to .mint_owner_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_deque_thief_bg_row
    "no matching function for call to .mint_thief_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_sharded_grid_producer_bg_row
    "no matching function for call to .mint_producer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_sharded_grid_consumer_bg_row
    "no matching function for call to .mint_consumer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_calendar_producer_bg_row
    "no matching function for call to .mint_producer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_calendar_consumer_bg_row
    "no matching function for call to .mint_consumer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_sharded_calendar_producer_bg_row
    "no matching function for call to .mint_producer_session<"
    "proto_row_admitted_by")
crucible_neg_compile_session_test(neg_endpoint_gate_sharded_calendar_consumer_bg_row
    "no matching function for call to .mint_consumer_session<"
    "proto_row_admitted_by")

# A context that claims a session epoch takes the claim from the one live
# epoch source.  The test proves a true claim is admitted, and a false, stale
# or orphaned claim and a second live source end the process.
crucible_test(test_session_epoch_source)
# Two advancers at once must not lose an advance or leave the sequence odd.
crucible_test(test_session_epoch_attacks)

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

# fix-10 — DispatchResult member-predicate pre() migrated to CRUCIBLE_PRE so
# it fires at consteval toolchain-independently (vanilla pre() was silently
# bypassed on stock GCC 16.1.1).  Same predicate shape across both bodies;
# the mode_/is_open/is_sealed migrations can't be consteval-witnessed
# (non-constexpr atomic/optional state — documented architectural limit).
crucible_neg_compile_test(neg_dispatch_result_compiled_status_on_record
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")
crucible_neg_compile_test(neg_dispatch_result_compiled_op_index_on_record
    "non-constant condition|not a constant expression|__builtin_trap|call to non-.constexpr. function")

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
    "static assertion failed"
    "static assert|fixy-A2-001")
crucible_neg_compile_session_test(neg_stop_g_crashclass_dropped_in_role_projection
    "static assertion failed"
    "static assert|fixy-A2-001")

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
    "static assertion failed"
    "static assert|fixy-A2-002")
crucible_neg_compile_session_test(neg_compose_epoched_accept_stop_g_missing_bottom
    "static assertion failed"
    "static assert|fixy-A2-002")

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
    "static assertion failed"
    "static assert|fixy-A2-028")
crucible_neg_compile_session_test(neg_protocol_effect_row_accept_stop_g_narrows_k
    "static assertion failed"
    "static assert|fixy-A2-028")

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
    "static assertion failed"
    "static assert|fixy-A2-029")
crucible_neg_compile_session_test(neg_checkpointed_session_terminal_rollback_nonterminal
    "static assertion failed"
    "static assert|fixy-A2-029")

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
    "static assertion failed"
    "static assert|fixy-A2-003")
crucible_neg_compile_session_test(neg_is_dual_involutive_checkpointed_wraps_sender_offer
    "static assertion failed"
    "static assert|fixy-A2-003")

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
    "static assertion failed"
    "static assert|fixy-A2-004")
crucible_neg_compile_session_test(neg_is_empty_choice_checkpointed_recovery_empty_offer
    "static assertion failed"
    "static assert|fixy-A2-004")

# ─────────────────────────────────────────────────────────────────
# fix-04 — SessionHandle value-ctor §XXI authorization-bypass closure
# ─────────────────────────────────────────────────────────────────
#
# Every SessionHandle specialization's value ctor is now private +
# friends only detail::make_session_handle (the sole authorized
# constructor; mint_session_handle / step_to_next route through it).
# These two fixtures prove a direct `SessionHandle<Proto, Res, Ctx>{res}`
# user call site is rejected ("is private"), across two distinct headers:
#   #1 Session.h Send<T, R> specialization.
#   #2 SessionCrash.h Stop_g<C> crash-stop specialization (cross-header
#      friend reach — the factory is forward-declared in Session.h).
crucible_neg_compile_session_test(neg_session_handle_ctor_public_bypass
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "is private")
crucible_neg_compile_session_test(neg_session_crash_handle_ctor_public_bypass
    "use of deleted function|no matching function|no previous declaration|static assertion failed|constraints.*not satisfied|is private within this context|has no member named"
    "is private")

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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|staleness_secret_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_ghost_observable
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|ghost_runtime_observable")

# fixy-CR-01 — staleness-secret strict-default closure.
# Second fixture mirroring the classified_io / classified_bg pair
# above, this time anchored on the staleness_secret_without_declassify
# corpus entry.  See those fixtures' commentary for the bypass shape.
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret_strict_default
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|staleness_secret_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_staleness_secret_wrong_policy_wire
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|internal_io_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_io_multi_effect
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|internal_bg_without_declassify")
crucible_neg_compile_fixy_test(neg_fixy_theory_internal_bg_multi_effect
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "NotInTheoryCorpus|internal_bg_without_declassify")

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
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxAdmitsPermission")
crucible_neg_compile_fixy_test(neg_fixy_perm_share_bare_row_bearing
    "static assertion failed"
    "permission_row|Row<>")
crucible_neg_compile_fixy_test(neg_fixy_perm_inherit_without_specialization
    "static assertion failed"
    "mint_permission_inherit requires inherits_from")
crucible_neg_compile_fixy_test(neg_fixy_perm_inherit_self_cycle
    "static assertion failed"
    "mint_permission_inherit forbids circular inheritance")

# ── FIXY-U-014 HS14 floor — mint_read_view + mint_grid_permissions ──
# Eight using-decls added to fixy/Perm.h surface the
# Pool/Guard/ReadView/Fair/Grid machinery.  Two of those are §XXI
# mints — `mint_read_view` and `mint_grid_permissions` — so HS14
# requires ≥2 fixy_neg fixtures per mint demonstrating the
# substrate's gates fire IDENTICALLY through the fixy::perm:: alias.
# Mirrors the safety_neg companions for the substrate symbols.
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_read_view_non_permission_arg
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|passing "
    "mint_read_view|Permission|deduction|no matching|substitution failure")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_read_view_tag_mismatch
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "mint_read_view|Permission|no matching|conversion")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_grid_permissions_zero_m
    "constraints.*not satisfied"
    "can_split_grid|mint_grid_permissions|no matching")
crucible_neg_compile_fixy_test(neg_fixy_perm_mint_grid_permissions_zero_n
    "constraints.*not satisfied"
    "can_split_grid|mint_grid_permissions|no matching")

# Cipher::open takes the root as fixy::Path<fixy::tags::source::External>,
# so every operator-supplied path is tagged at the boundary, and open runs
# the sanitizer itself.  It refuses a bare string and a path that a caller
# tagged Sanitized.  The third fixture refuses sanitize_path on a path of
# the wrong source.
crucible_neg_compile_fixy_test(neg_fixy_cipher_open_rejects_bare_string
    "no matching function for call to .crucible::Cipher::open.const fixy::TestRunnerCtx&, const char .35.."
    "to type .fixy::Path<fixy::tags::source::External>.")
crucible_neg_compile_fixy_test(neg_fixy_cipher_open_rejects_sanitized_input
    "no matching function for call to .crucible::Cipher::open.const fixy::TestRunnerCtx&, std::remove_reference<fixy::Tagged<std::filesystem::__cxx11::path, fixy::tags::source::Sanitized>&>::type."
    "aka .fixy::Tagged<std::filesystem::__cxx11::path, fixy::tags::source::Sanitized>... to type .fixy::Path<fixy::tags::source::External>.")
crucible_neg_compile_fixy_test(neg_fixy_sanitize_path_rejects_wrong_source
    "invalid initialization|could not convert|cannot convert|no matching function"
    "source::External|source::FromUser")

# ── FIXY-V-224 — fixy/Fs.h §XXI ctx-bound mint discipline (HS14) ────
# Six fixtures cover six distinct mismatch classes, satisfying the
# HS14 ≥2-fixtures-per-mint floor with rooom to spare:
#   #1 mint_file Path<External>           → tag mismatch on path
#   #2 mint_file<> (empty Grants)         → has_mode_v=false
#   #3 mint_file<mode<>, mode<>>          → has_duplicate_mode_v=true
#   #4 sync<sync_op::None>                → CtxFitsSync rejects None
#   #5 commit_atomic Path<External> target → target tag mismatch
#   #6 mint_file<>(ColdInitCtx, …)        → ctx row lacks Effect::Block
# Together they witness the §XXI fit-check fires at construction
# along EVERY load-bearing predicate (path source × Grants engagement
# × Grants ceiling × SyncOp ≠ None × ctx row Effect::Block).
crucible_neg_compile_fixy_test(neg_fixy_v_224_mint_file_external_path
    "cannot convert|could not convert|no matching function"
    "mint_file")
crucible_neg_compile_fixy_test(neg_fixy_v_224_mint_file_empty_grants
    "no matching function|constraints.*not satisfied"
    "CtxFitsFileMint|has_mode")
crucible_neg_compile_fixy_test(neg_fixy_v_224_mint_file_duplicate_mode
    "no matching function|constraints.*not satisfied"
    "CtxFitsFileMint|has_duplicate_mode")
crucible_neg_compile_fixy_test(neg_fixy_v_224_sync_none
    "no matching function|constraints.*not satisfied"
    "CtxFitsSync|sync_op::None")
crucible_neg_compile_fixy_test(neg_fixy_v_224_commit_atomic_external_target
    "cannot convert|could not convert|no matching function"
    "commit_atomic")
crucible_neg_compile_fixy_test(neg_fixy_v_224_mint_file_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxAdmitsIoBlock|CtxFitsFileMint|row_contains")

# ── FIXY-FOUND-091 — durable-mint HS14 neg-compile fixtures (#2246) ──
#
# mint_durable_truncate_file and mint_durable_append_file delegate to
# the underlying mint_file<...> with pinned grant stacks.  Per HS14,
# every mint factory ships ≥2 neg fixtures.  Four classes (2 per mint):
#   #1/3 Path<External>     — path-source-tag mismatch at outer signature
#   #2/4 ColdInitCtx no Block — ctx-row gap propagates through delegation
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_durable_truncate_file_external_path
    "cannot convert|could not convert|no matching function"
    "mint_durable_truncate_file")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_durable_truncate_file_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxAdmitsIoBlock|CtxFitsFileMint|row_contains")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_durable_append_file_external_path
    "cannot convert|could not convert|no matching function"
    "mint_durable_append_file")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_durable_append_file_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxAdmitsIoBlock|CtxFitsFileMint|row_contains")

# ── FIXY-V-225 — fixy/Mmap.h HS14 neg-compile fixtures (#2103) ──────
#
# Seven mismatch classes for the V-225 substrate's three concept gates
# (CtxFitsMmapMint × CtxFitsAnonMmapMint × CtxFitsSafeAdvise):
#   #1 mint_mmap empty Grants            → no prot, no share engagement
#   #2 mint_mmap Exec without trusted_jit → W^X gating
#   #3 mint_mmap duplicate with_prot<>   → prot-axis uniqueness
#   #4 mint_mmap duplicate primary share → share-axis primary uniqueness
#   #5 mint_mmap_anon w/o Anonymous      → anon-variant adds anon clause
#   #6 mint_mmap_anon(ColdInitCtx, …)    → ctx row lacks Effect::Block
#   #7 advise<DontNeed> on safe surface  → dangerous-advice gate
# Together they witness the §XXI fit-check fires at construction along
# EVERY load-bearing predicate (prot engagement × Exec gating × prot
# uniqueness × primary-share uniqueness × anon-variant additionality ×
# ctx row Effect::Block × dangerous-advice channeling).
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_empty_grants
    "no matching function|constraints.*not satisfied"
    "CtxFitsMmapMint|has_prot_grant|has_primary_share")
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_exec_no_jit
    "no matching function|constraints.*not satisfied"
    "CtxFitsMmapMint|has_exec_prot|has_trusted_jit")
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_duplicate_prot
    "no matching function|constraints.*not satisfied"
    "CtxFitsMmapMint|has_duplicate_prot")
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_duplicate_share
    "no matching function|constraints.*not satisfied"
    "CtxFitsMmapMint|has_duplicate_primary_share")
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_anon_missing_anonymous
    "no matching function|constraints.*not satisfied"
    "CtxFitsAnonMmapMint|pack_has_anonymous")
crucible_neg_compile_fixy_test(neg_fixy_v_225_mint_mmap_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxAdmitsIoBlock|CtxFitsMmapMint|row_contains")
crucible_neg_compile_fixy_test(neg_fixy_v_225_advise_dontneed_safe_surface
    "no matching function|constraints.*not satisfied"
    "CtxFitsSafeAdvise|is_dangerous_advice")

# ── FIXY-V-234 — advise_release_aware + SharedPermission composition ─
#
# V-225 shipped the dangerous-Advice gate + the release_aware<RegionTag>
# tag.  V-234 closes the loop by adding `Permission<RegionTag> const&`
# as a required borrow proof on advise_release_aware<>.  Two new HS14
# fixtures (the V-225 safe-surface fixture above is the third gate's
# witness — together they form the full distinct-mismatch-class set):
#
#   #2 missing-permission-proof — call without Permission arg.  Compiler
#      rejects via "too few arguments" against the new 3-param signature.
#   #3 cross-tag-permission     — call with Permission<RegionB> where
#      RegionTag = RegionA.  Compiler rejects via reference-binding
#      mismatch between Permission<A> const& and Permission<B>.
#
# Catalog rule `M001_DontNeedRequiresReleaseAware` names this collision
# class in `safety/CollisionCatalog.h` for fleet-wide audit.
crucible_neg_compile_fixy_test(neg_fixy_v_234_advise_release_aware_missing_permission
    "no matching function|too few arguments|constraints.*not satisfied"
    "Permission")
crucible_neg_compile_fixy_test(neg_fixy_v_234_advise_release_aware_cross_tag_permission
    "no matching function|constraints.*not satisfied"
    "advise_release_aware")

# ── FIXY-V-257 — fixy/Hw.h §XXI hw-grant mint discipline (HS14) ─────
# Ten fixtures cover two distinct mismatch classes per NEW mint (5 mints
# → ≥2 each, per HS14):
#   asm_            : empty-rationale (rationale_nonempty) + non-ExecCtx
#   simd_width      : invalid-width (valid_simd_width)     + non-ExecCtx
#   vendor_intrinsic: empty-id (rationale_nonempty)        + non-ExecCtx
#   tsc             : Mode==NotAllowed (strict default)    + missing CpuPinProof
#   msr             : missing Permission<root>             + wrong-tag Permission
# Together they witness the §XXI fit-check fires along EVERY load-bearing
# gate (rationale × width × strict-default posture × proof-token presence
# × authority-tag identity × ExecCtx floor).
crucible_neg_compile_fixy_test(neg_fixy_v_257_asm_empty_rationale
    "no matching function|constraints.*not satisfied"
    "CtxFitsAsmMint|rationale_nonempty")
crucible_neg_compile_fixy_test(neg_fixy_v_257_asm_non_ctx
    "no matching function|constraints.*not satisfied"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_v_257_simd_width_invalid
    "no matching function|constraints.*not satisfied"
    "CtxFitsSimdWidthMint|valid_simd_width")
crucible_neg_compile_fixy_test(neg_fixy_v_257_simd_width_non_ctx
    "no matching function|constraints.*not satisfied"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_v_257_vendor_intrinsic_empty_id
    "no matching function|constraints.*not satisfied"
    "CtxFitsVendorIntrinsicMint|rationale_nonempty")
crucible_neg_compile_fixy_test(neg_fixy_v_257_vendor_intrinsic_non_ctx
    "no matching function|constraints.*not satisfied"
    "IsExecCtx")
crucible_neg_compile_fixy_test(neg_fixy_v_257_tsc_not_allowed
    "no matching function|constraints.*not satisfied"
    "CtxFitsTscMint|NotAllowed")
crucible_neg_compile_fixy_test(neg_fixy_v_257_tsc_missing_proof
    "no matching function|too few arguments"
    "CpuPinProof")
crucible_neg_compile_fixy_test(neg_fixy_v_257_msr_missing_permission
    "no matching function|too few arguments"
    "Permission")
crucible_neg_compile_fixy_test(neg_fixy_v_257_msr_wrong_permission
    "no matching function|could not convert|cannot bind"
    "Permission")

# ── FIXY-V-264 — TraceRing prefetch + ChaseLevDeque fence grants ────
# Two fixtures witness the HwInstruction cache grant's NTTP-range gate
# and the BarrierStrength fence grant's axis routing (distinct classes).
crucible_neg_compile_fixy_test(neg_fixy_v_264_cache_locality_overflow
    "constraints.*not satisfied|associated constraints"
    "Locality|cache")
crucible_neg_compile_fixy_test(neg_fixy_v_264_barrier_axis_mismatch
    "static assertion"
    "FIXY-V-264|SimdIsa|BarrierStrength")

# ── FIXY-V-226 — fixy/Io.h HS14 neg-compile fixtures (#2104) ────────
#
# Seven mismatch classes for the V-226 substrate's three concept gates
# (CtxAdmitsIoBlock × CtxFitsIoUringMint × CtxFitsZerocopyMint):
#   #1 mint_io_uring_ring empty Grants     → no engine, no sq_entries
#   #2 mint_io_uring_ring engine<Synchronous> → engine_is_io_uring gate
#   #3 mint_io_uring_ring sq_entries<3>    → power-of-2 gate on sq_entries
#   #4 mint_io_uring_ring dup engine<>     → engine-axis uniqueness
#   #5 mint_io_uring_ring(ColdInitCtx, …)  → ctx row lacks Effect::Block
#   #6 mint_zerocopy_transfer zerocopy<None>   → sentinel rejected
#   #7 mint_zerocopy_transfer zerocopy<Splice> → simple-transfer subset
# Together they witness the §XXI fit-check fires at construction along
# EVERY load-bearing predicate (engine engagement × engine enumerator ×
# sq_entries pow2 × engine uniqueness × ctx row Effect::Block ×
# zerocopy sentinel × simple-transfer channeling).
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_io_uring_ring_empty_grants
    "no matching function|constraints.*not satisfied"
    "CtxFitsIoUringMint|has_engine_grant|has_sq_entries_grant")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_io_uring_ring_wrong_engine
    "no matching function|constraints.*not satisfied"
    "CtxFitsIoUringMint|engine_is_io_uring")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_io_uring_ring_sq_not_pow2
    "no matching function|constraints.*not satisfied"
    "CtxFitsIoUringMint|sq_entries_is_pow2")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_io_uring_ring_duplicate_engine
    "no matching function|constraints.*not satisfied"
    "CtxFitsIoUringMint|has_duplicate_engine")
crucible_neg_compile_fixy_test(neg_fixy_v_226_mint_io_uring_ring_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxAdmitsIoBlock|CtxFitsIoUringMint|row_contains")
crucible_neg_compile_fixy_test(neg_fixy_v_226_zerocopy_none_sentinel
    "no matching function|constraints.*not satisfied"
    "CtxFitsZerocopyMint|pack_zerocopy_is_none")
crucible_neg_compile_fixy_test(neg_fixy_v_226_zerocopy_splice_not_simple
    "no matching function|constraints.*not satisfied"
    "CtxFitsZerocopyMint|pack_zerocopy_is_simple_transfer")

# ── FIXY-V-228 — fixy/CipherDurable.h HS14 neg-compile fixtures (#2106)
#
# Six mismatch classes for the V-228 substrate's three §XXI mint
# factories — two distinct mismatch classes per mint (ctx-row gap on
# Effect::Block × extras shadowing a stance-pinned axis):
#   #1 mint_warm_writer   (ColdInitCtx)              → row lacks Block
#   #2 mint_warm_writer   <grant::mode<…>>           → extras_engage_mode
#   #3 mint_cold_writer   (ColdInitCtx)              → row lacks Block
#   #4 mint_cold_writer   <grant::durable<…>>        → extras_engage_durable
#   #5 mint_head_advancer (ColdInitCtx)              → row lacks Block
#   #6 mint_head_advancer <grant::atomic_write<…>>   → extras_engage_atomic_write
# Together they witness the §XXI fit-check fires at construction along
# EVERY load-bearing predicate (CtxAdmitsIoBlock × three stance-axis
# shadow predicates) for all three Cipher durability postures.
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_warm_writer_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxFitsWarmWriterMint|CtxAdmitsIoBlock|row_contains|Block")
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_warm_writer_extras_engage_mode
    "no matching function|constraints.*not satisfied"
    "CtxFitsWarmWriterMint|extras_engage_mode")
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_cold_writer_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxFitsColdWriterMint|CtxAdmitsIoBlock|row_contains|Block")
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_cold_writer_extras_engage_durable
    "no matching function|constraints.*not satisfied"
    "CtxFitsColdWriterMint|extras_engage_durable")
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_head_advancer_ctx_lacks_block
    "no matching function|constraints.*not satisfied"
    "CtxFitsHeadAdvancerMint|CtxAdmitsIoBlock|row_contains|Block")
crucible_neg_compile_fixy_test(neg_fixy_v_228_mint_head_advancer_extras_engage_atomic_write
    "no matching function|constraints.*not satisfied"
    "CtxFitsHeadAdvancerMint|extras_engage_atomic_write")

# ── FIXY-V-233 — sanitize::path_traversal runtime predicates ────────
# Runtime smoke test exercising both V-233 predicates
# (check_no_dotdot + check_absolute_root_locked) plus the two
# compositional sanitize_path_* entry-points across every V-232 source
# tag → Sanitized admittance.  The two HS14 fixtures called out in the
# V-233 task description are pinned at the top of main():
#   * ../etc/passwd                → DotDotComponent
#   * /abs/path under /home/user   → EscapesAnchor
# Followed by per-rule coverage for every PathTraversalError enumerator
# and per-source-tag happy-path coverage (External + FromUserPath +
# FromEnvPath + FromConfigPath).
crucible_test(test_path_traversal_predicates)

# ── FIXY-IS round 2 — concept-gate coverage extension ───────────────
# Existing Is.h coverage pins 3 wrappers (Linear, Secret, SessionHandle);
# this round adds 6 more, covering canonical wrapper-nesting order from
# CLAUDE.md §XVI (HotPath, DetSafe, Stale, Tagged, Refined, Permission).
# Each fixture instantiates the IsX concept-as-requires-clause and
# confirms a plain unrelated type is rejected — alias identity check.
crucible_neg_compile_fixy_test(neg_fixy_is_tagged_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsTagged")
crucible_neg_compile_fixy_test(neg_fixy_is_refined_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsRefined")
crucible_neg_compile_fixy_test(neg_fixy_is_hotpath_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsHotPath")
crucible_neg_compile_fixy_test(neg_fixy_is_detsafe_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsDetSafe")
crucible_neg_compile_fixy_test(neg_fixy_is_stale_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsStale")
crucible_neg_compile_fixy_test(neg_fixy_is_permission_rejects_plain
    "constraints.*not satisfied|associated constraints|no matching function"
    "IsPermission")

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
    "incomplete type"
    "which_dim|no member named .value.|primary template")

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
    "static assertion failed|constraints.*not satisfied"
    "FixyNotEngaged|IsAcceptedActive")

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
    "use of deleted function|no matching function|static assertion failed|constraints.*not satisfied|is private within this context|incomplete type|has no member named|could not convert|cannot convert|cannot bind|wrong number of template arguments|template argument|deduced conflicting|invalid initialization|passing |no match for"
    "FederatedPeer")
crucible_neg_compile_fixy_test(neg_fixy_federation_handshake_wrong_type
    "cannot convert|no matching function"
    "FederationHandshake")

# fixy-CR-05 — defensive splits_into partial in FederationPermission.h
# rejects accidental cross-org splits (no malicious user-side
# specialization in scope).  The binary and N-ary fixtures together
# satisfy the HS14 ≥2 floor for fixy-CR-05's defensive partial.
crucible_neg_compile_fixy_test(neg_fixy_federation_cross_org_split
    "static assertion"
    "splits_into|requires")
crucible_neg_compile_fixy_test(neg_fixy_federation_cross_org_split_n
    "static assertion"
    "splits_into_pack|requires")

# fixy-CR-06 — once-per-program federation root-mint gap closed.
# Both `mint_permission_root<FederatedPeer<Org>>()` and the ctx-bound
# `mint_permission_root<FederatedPeer<Org>>(ctx)` overloads in
# `crucible::safety` are concept-deleted for any tag satisfying
# `is_federated_peer_tag_v<Tag>`.  The only legitimate path is
# `mint_federation_admittance<Org, Policy>(local_cipher, handshake)`.
# The no-ctx and ctx fixtures together satisfy the HS14 ≥2 floor.
crucible_neg_compile_fixy_test(neg_fixy_federation_root_mint_disallowed
    "deleted|use of deleted function"
    "mint_permission_root")
crucible_neg_compile_fixy_test(neg_fixy_federation_root_mint_disallowed_ctx
    "deleted|use of deleted function"
    "mint_permission_root")

# fixy-CR-07 — federation session mints require admittance witness.
# `mint_sender<Org, KeyTag>(ctx, endpoint, admittance)` (and its
# receiver / coord / channel siblings in
# `crucible::safety::proto::federation`) take a
# `Permission<FederatedPeer<Org>> const&` as the load-bearing soundness
# rail.  Two distinct failure classes prove the gate:
#
#   no_admittance — 3-arg call site (ctx, endpoint) omits the witness;
#                   arity check refuses (no overload takes 2 args).
#   wrong_org     — the admittance token is for `OrgA`, and the mint is for
#                   `OrgB`.  The context and the endpoint fit, so the only
#                   failure is the token that does not convert.
crucible_neg_compile_fixy_test(neg_fixy_federation_session_no_admittance
    "no matching function|too few arguments"
    "requires 3 arguments|candidate")
crucible_neg_compile_fixy_test(neg_fixy_federation_session_wrong_org
    "no matching function for call to .mint_sender<NegFedWrongOrg_OrgB>\\(FederationFitCtx&, NegFedWrongOrg_Endpoint, crucible::safety::SharedPermission<crucible::permissions::tag::FederatedPeer<NegFedWrongOrg_OrgA> >\\)."
    "\\(type .crucible::safety::SharedPermission<crucible::permissions::tag::FederatedPeer<NegFedWrongOrg_OrgA> >.\\) to type .crucible::safety::SharedPermission<crucible::permissions::tag::FederatedPeer<NegFedWrongOrg_OrgB> >.")

# fixy-A2-016 — 12 negative-compile fixtures across 6 bridges/* mint
# factories (≥2 per factory).  Each fixture's expected diagnostic
# regex is wide enough to tolerate GCC phrasing drift across patch
# levels but specific enough to fail when the underlying gate goes
# missing.  Driver: test/neg_compile_driver.py.
crucible_neg_compile_bridges_test(neg_mint_recording_session_non_handle
    "no matching function for call to|could not match"
    "mint_recording_session")
crucible_neg_compile_bridges_test(neg_mint_recording_session_wrong_log_type
    "could not convert|no matching function for call to|cannot bind"
    "mint_recording_session")
crucible_neg_compile_bridges_test(neg_mint_recording_session_crash_watched_non_handle
    "no matching function for call to|could not match"
    "mint_recording_session")
crucible_neg_compile_bridges_test(neg_mint_recording_session_crash_watched_wrong_log_type
    "could not convert|no matching function for call to|could not match"
    "mint_recording_session")
crucible_neg_compile_bridges_test(neg_mint_recording_endpoint_non_endpoint
    "no matching function for call to|could not match|cannot bind rvalue"
    "mint_recording_endpoint")
crucible_neg_compile_bridges_test(neg_mint_recording_endpoint_wrong_log_type
    "no matching function for call to|could not match|template argument deduction"
    "mint_recording_endpoint")
crucible_neg_compile_bridges_test(neg_mint_crash_watched_endpoint_non_endpoint
    "no matching function for call to|could not match|cannot bind rvalue"
    "mint_crash_watched_endpoint")
crucible_neg_compile_bridges_test(neg_mint_crash_watched_endpoint_missing_peer_tag
    "no matching function for call to|couldn't infer|could not deduce"
    "mint_crash_watched_endpoint|PeerTag")

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
    "no matching function|cannot convert"
    "PermissionedSessionHandle")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_delegate_transport_not_invocable
    "constraints.*not satisfied|no matching function"
    "is_invocable")

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
    "constraints.*not satisfied|no matching function"
    "is_invocable")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_accept_transport_returns_void
    "incomplete type|no matching function|cannot declare"
    "accept")

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
    "no matching function|cannot convert"
    "PermissionedSessionHandle")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_delegate_transport_not_invocable
    "constraints.*not satisfied|no matching function"
    "is_invocable")

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
    "constraints.*not satisfied|no matching function"
    "is_invocable")
crucible_neg_compile_bridges_test(neg_mint_recording_session_psh_epoched_accept_transport_returns_void
    "incomplete type|no matching function|cannot declare"
    "accept")

# fixy-A2-019 — HS14 floor for the renamed mint_atomic_session<Proto>(cell).
# Two fixtures witness the two-conjunct requires-clause:
#   AtomicMachineCell<Cell>          — non-cell argument fails the concept
#   safety::proto::is_well_formed_v<Proto>  — free Continue at top level fails
crucible_neg_compile_bridges_test(neg_mint_atomic_session_non_atomic_machine_cell
    "constraints.*not satisfied|no matching function for call to"
    "AtomicMachineCell")
crucible_neg_compile_bridges_test(neg_mint_atomic_session_malformed_proto
    "constraints.*not satisfied|no matching function for call to"
    "is_well_formed")
# FIXY-V-020: CanMintVigilModeBridge<Cell> rejects non-ModeCell types.
crucible_neg_compile_bridges_test(neg_vigil_mode_bridge_wrong_cell
    "constraints.*not satisfied"
    "CanMintVigilModeBridge")

# fixy-A2-021 — HS14 floor for the WrapCrashReturnKey pass-key idiom.
# Two fixtures witness that external code cannot mint or use the key:
#   neg_wrap_crash_return_key_external_construction — bare `Key{}` rejected
#   neg_crash_event_external_construction          — `CrashEvent<P,R>{}` rejected
# The non-template `detail::WrapCrashReturnAuthorizer` friend is immune to
# parameter-pack drift on `wrap_crash_return` (template-friend brittleness).
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_external_construction
    "is private within this context"
    "WrapCrashReturnKey")
crucible_neg_compile_bridges_test(neg_crash_event_external_construction
    "is private within this context"
    "WrapCrashReturnKey")

# FIXY-V-013 — HS14 fixture for §XXI Universal Mint Pattern compliance.
# Closes a different bypass class than the two above: a caller who
# legitimately mints a `WrapCrashReturnKey` through the public
# `detail::WrapCrashReturnAuthorizer::mint()` surface, then attempts to
# feed it to `CrashEvent<P,R>{key, r, perms}` directly, bypassing
# `wrap_crash_return`'s `inner.detach(reason_tag)` side effect.  V-013
# moves the ctor to `private:` with friendship on the authorizer; the
# access check fires here, leaving `mint_event_<>` as the sole
# grep-discoverable construction site for CrashEvent in the tree.
crucible_neg_compile_bridges_test(neg_crash_event_ctor_public_bypass
    "use of deleted function|is private within this context"
    "CrashEvent|WrapCrashReturnKey")

# fixy-A2-032 — HS14 floor for the WrapCrashReturnKey non-fungibility
# property.  Two fixtures witness that the passkey cannot be duplicated
# even when held by reference: the copy-ctor and move-ctor are both
# `= delete("fixy-A2-032: ...")`.  C++17 mandatory copy-elision keeps
# the production path (mint() prvalue → CrashEvent value parameter)
# intact, so the deletion is defense-in-depth at zero call-site cost.
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_copy_construct
    "use of deleted function"
    "passkey cannot be copied|fixy-A2-032")
crucible_neg_compile_bridges_test(neg_wrap_crash_return_key_move_construct
    "use of deleted function"
    "passkey cannot be moved|fixy-A2-032")

# fix-15 — RecordingPermissionedSessionHandle passkey closure: direct
# brace-construction of any specialization is rejected (the ctor's leading
# recording_session_construct_key is unconstructable outside mint_recording_session).
crucible_neg_compile_bridges_test(neg_recording_psh_ctor_public_bypass
    "no matching function"
    "recording_session_construct_key")
crucible_neg_compile_bridges_test(neg_recording_psh_offer_ctor_public_bypass
    "no matching function"
    "recording_session_construct_key")

# ── FIXY-V-071 — fixy::concurrent::SpinLock<Tag> HS14 floor (#1949)
# ConnectionPoolRuntime.h 11 spin-gate sites migrated to the typed
# wrapper.  Two HS14 fixtures witness the two soundness gates:
#   (#1) constructor without Permission<Tag>& — no matching overload
#   (#2) lock_in<BgDrainCtx> — Bg context owns Effect::Bg, constraint
#        rejects via `!CtxOwnsCapability<Ctx, Effect::Bg>`
crucible_neg_compile_fixy_test(neg_fixy_concurrent_spinlock_no_permission
    "no matching function|could not convert|too few arguments|cannot bind"
    "SpinGuard")
crucible_neg_compile_fixy_test(neg_fixy_concurrent_spinlock_bg_context
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxOwnsCapability|lock_in|Bg")

# ── FIXY-V-083 — fixy::spawn::mint_spawn / mint_parallel_for HS14 ───
#
# First file in the fixy/spawn/ family.  Two §XXI ctx-bound mints
# facade the substrate's structured-concurrency primitives:
#   mint_spawn        → safety::mint_permission_fork (CSL parallel rule)
#   mint_parallel_for → safety::parallel_for_views<N> (range fan-out)
#
# Four HS14 fixtures (2 per mint, distinct rejection classes):
#   #1 mint_spawn × HotFgCtx (row missing Bg) → CtxFitsSpawn fails on
#      the row-admission axis even though splits_into_pack is declared.
#   #2 mint_spawn × BgDrainCtx + no splits_into_pack → CtxFitsSpawn
#      fails on the structural-disjointness axis even though Bg is
#      admitted.
#   #3 mint_parallel_for × HotFgCtx → CtxFitsParallelFor fails on
#      CtxOwnsCapability<Ctx, Effect::Bg>.
#   #4 mint_parallel_for × BgDrainCtx + wrong-shape body (Whole-tag
#      instead of Slice<Whole, 0>) → CtxFitsParallelFor fails on the
#      is_nothrow_invocable axis.
crucible_neg_compile_fixy_test(neg_fixy_spawn_mint_spawn_ctx_no_bg
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsSpawn|CtxFitsPermissionFork|row_contains_v")
crucible_neg_compile_fixy_test(neg_fixy_spawn_mint_spawn_no_splits_into_pack
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsSpawn|CtxFitsPermissionFork|splits_into_pack")
crucible_neg_compile_fixy_test(neg_fixy_spawn_mint_parallel_for_ctx_no_bg
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsParallelFor|CtxOwnsCapability|row_contains_v")
crucible_neg_compile_fixy_test(neg_fixy_spawn_mint_parallel_for_body_signature
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsParallelFor|is_nothrow_invocable|Slice")

# ── FIXY-V-203 — fixy/spawn/JoinPolicy.h spawn-MECHANISM tag tree
#                 HS14 floor (Agent 7 §fixy::spawn::).
#
# V-203 ships six phantom tags + IsJoinMechanismTag identity-allowlist
# concept.  Two fixtures cover orthogonal rejection axes:
#   1. join_concept_rejects_non_tag — `int` has no `mechanism` member;
#      structural rejection at the first allowlist clause (i.e. every
#      `is_same_v<int, Tag>` is false).
#   2. join_imposter_struct — an imposter struct DOES carry
#      `static constexpr JoinMechanism mechanism = JoinMechanism::AutoJoin`
#      (field-shape would pass) but is NOT one of the six declared
#      tag types — the identity allowlist fires.
#
# Distinct mismatch axes ⇒ HS14 floor satisfied.
crucible_neg_compile_fixy_test(neg_fixy_v_203_join_concept_rejects_non_tag
    "constraints.*not satisfied|constraints.*not satisfied"
    "IsJoinMechanismTag")
crucible_neg_compile_fixy_test(neg_fixy_v_203_join_imposter_struct
    "constraints.*not satisfied|constraints.*not satisfied"
    "IsJoinMechanismTag")

# ── FIXY-V-180 — warden/Hardening.h syscall-grant annotation
#                 HS14 floor (Agent 3 Phase Σ4).
#
# Two fixtures cover distinct Init-rejection axes:
#   1. bg_drain_reject — Bg-cap context (admits Bg+Alloc+IO+Block, no
#      Init).  Hardening's privileged syscalls are startup-only.
#   2. hot_fg_reject — Fg-cap context (admits Fg only, no Init).
#      Blocking syscalls don't belong on the hot recording path.
crucible_neg_compile_fixy_test(neg_fixy_v_180_hardening_bg_drain_reject
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsHardeningMint|CtxOwnsCapability")
crucible_neg_compile_fixy_test(neg_fixy_v_180_hardening_hot_fg_reject
    "constraints.*not satisfied|constraints.*not satisfied"
    "CtxFitsHardeningMint|CtxOwnsCapability")

# ── FIXY-V-179 — perf hub syscall-grant annotations HS14 floor ──────
#
# Two fixtures for two perf hub mints, each with a different context:
#   1. bg_drain_reject: a background drain context at the mint_sense_hub
#      call site.  It claims Row<Bg, Alloc>, and the gate demands Alloc,
#      IO and Block, because the load waits on the kernel verifier.
#   2. hot_fg_reject: a hot foreground context at the mint_pmu_sample
#      call site.  It claims the empty row, so a blocking load cannot run
#      on the hot recording path.
crucible_neg_compile_fixy_test(neg_fixy_v_179_perf_hubs_bg_drain_reject
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_fixy_test(neg_fixy_v_179_perf_hubs_hot_fg_reject
    "is_subrow_v<R1, R2> .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")

# ── Stage A1 — the sibling substrate and its three guards ─────────
#
# include/foundation/ and include/fixy/ are the canonical substrate,
# extracted beside the old tree.  Three guards hold the extraction to its
# rules from its first commit:
#
#   layer_boundary — foundation < fixy < crucible; a file names only its
#                    own layer and the layers below it.  No allowlist.
#   frozen_tree    — the old substrate under include/crucible/ only
#                    shrinks until Stage D deletes it.  Exit 3 (freeze
#                    base absent, shallow clone) is a SKIP here; the
#                    `layers` CI job checks out full history and enforces.
#   derived_pins   — a cardinality static_assert must have a derived
#                    operand; a literal pinned to itself cannot fire.
add_test(NAME layer_boundary_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-layer-boundary.py --self-test)
add_test(NAME layer_boundary
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-layer-boundary.py)
set_tests_properties(layer_boundary layer_boundary_self_test PROPERTIES
    LABELS "ci_guard;stage-A1"
    SKIP_RETURN_CODE 3)
add_test(NAME frozen_tree_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-frozen-tree.py --self-test)
# The scan preprocesses a unified frozen header on the flags of this build.
add_test(NAME frozen_tree
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-frozen-tree.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(frozen_tree frozen_tree_self_test PROPERTIES
    LABELS "ci_guard;stage-A1")
set_tests_properties(frozen_tree frozen_tree_self_test PROPERTIES SKIP_RETURN_CODE 3)
set_tests_properties(frozen_tree_self_test PROPERTIES
    ENVIRONMENT "FROZEN_TREE_CXX=${CMAKE_CXX_COMPILER}")
add_test(NAME derived_pins_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-derived-pins.py --self-test)
add_test(NAME derived_pins
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-derived-pins.py)
set_tests_properties(derived_pins derived_pins_self_test PROPERTIES
    LABELS "ci_guard;stage-A1"
    SKIP_RETURN_CODE 3)

# flip_list holds the drain of the old substrate's consumers.  Every file
# under the scan roots that names the frozen tree is on scripts/flip-list.txt,
# every entry still names it, and no entry is missing.  A new consumer fails,
# and so does an entry left behind after its file drains.
add_test(NAME flip_list_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-flip-list.py --self-test)
add_test(NAME flip_list
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-flip-list.py)
set_tests_properties(flip_list flip_list_self_test PROPERTIES
    LABELS "ci_guard;stage-C;ast")
set_tests_properties(flip_list flip_list_self_test PROPERTIES SKIP_RETURN_CODE 3)

# vessel_acceptance is the acceptance gate of the PyTorch vessel under
# vessel/torch.  scripts/check-vessel-acceptance.py builds the vessel targets
# and accepts the vessel of this build only when the vessel compiles against a
# PyTorch with DispatchKey::Crucible at -Werror, no vessel source names the old
# substrate, each vessel/torch/test_*.py exits 0, and replay gives the bytes of
# PyTorch alone.  The self-test needs no PyTorch, so it is a guard of every
# build.
#
# A build that cannot operate the gate does not get a test that passes or
# skips.  It gets vessel_acceptance_unavailable, which ctest shows as
# "Not Run (Disabled)" with the others that did not run.  A build that must
# operate the gate sets CRUCIBLE_REQUIRE_VESSEL, and the configure step then
# stops with the reason.  CRUCIBLE_VESSEL_PYTHON is the interpreter of the
# PyTorch that TORCH_DIR names.  The default is the interpreter of the
# virtual environment that holds TORCH_DIR, when one exists.
add_test(NAME vessel_acceptance_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-vessel-acceptance.py --self-test)
set_tests_properties(vessel_acceptance_self_test PROPERTIES LABELS "ci_guard")
option(CRUCIBLE_REQUIRE_VESSEL
  "Fail the configure step when the build cannot operate the vessel acceptance gate" OFF)
set(_vessel_gate_blocker "")
if(NOT TARGET crucible_dispatch)
  set(_vessel_gate_blocker
    "the build has no crucible_dispatch target, because TORCH_DIR names no PyTorch tree with torch/include")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug" AND (CRUCIBLE_SANITIZE OR CRUCIBLE_TSAN))
  set(_vessel_gate_blocker
    "the vessel libraries of this build carry a sanitizer runtime, and the gate runs Python with no sanitizer runtime preloaded")
elseif(NOT CRUCIBLE_WERROR)
  set(_vessel_gate_blocker "the build sets CRUCIBLE_WERROR to OFF, and the gate accepts only a -Werror build")
endif()
if(_vessel_gate_blocker)
  if(CRUCIBLE_REQUIRE_VESSEL)
    message(FATAL_ERROR "vessel_acceptance: ${_vessel_gate_blocker}.  Set CRUCIBLE_REQUIRE_VESSEL=OFF, or "
                        "configure a build of the release or bench preset with -DTORCH_DIR=<the PyTorch fork>.")
  endif()
  message(STATUS "vessel_acceptance: ${_vessel_gate_blocker}, so the gate is registered as disabled")
  # The ci_guard label puts the disabled entry in the list of tests that did
  # not run when `ctest -L ci_guard` operates, so the pre-merge gate shows it.
  add_test(NAME vessel_acceptance_unavailable COMMAND ${CMAKE_COMMAND} -E false)
  set_tests_properties(vessel_acceptance_unavailable PROPERTIES DISABLED TRUE LABELS "ci_guard;vessel")
else()
  get_filename_component(_vessel_venv_python "${TORCH_DIR}/../../../bin/python" ABSOLUTE)
  if(EXISTS "${_vessel_venv_python}")
    set(_vessel_default_python "${_vessel_venv_python}")
  else()
    set(_vessel_default_python "python3")
  endif()
  set(CRUCIBLE_VESSEL_PYTHON "${_vessel_default_python}" CACHE FILEPATH
    "The interpreter of the PyTorch that TORCH_DIR names, for the vessel acceptance gate")
  add_test(NAME vessel_acceptance
    COMMAND python3 ${CMAKE_SOURCE_DIR}/scripts/check-vessel-acceptance.py
            --build-dir ${CMAKE_BINARY_DIR} --torch-dir ${TORCH_DIR} --python ${CRUCIBLE_VESSEL_PYTHON})
  # The gate builds the vessel targets in this build directory, so no other
  # test operates beside it.  It invokes a script, so it carries ci_guard, and
  # `ctest -L ci_guard` operates it in a build that has the vessel.
  set_tests_properties(vessel_acceptance PROPERTIES LABELS "ci_guard;vessel" TIMEOUT 3600 RUN_SERIAL TRUE)
endif()

# allowlist_keys runs over every scripts/*allowlist*.txt and holds the one
# property none of the owning guards can hold once it breaks: the path half
# of each key names a file that exists.  When a port underscores a header
# and adds a copy beside it, a key on the old path exempts nothing AND the
# owning guard can no longer locate the site it was written for, so the
# guard goes quiet rather than loud.  That happened twice in Stage A before
# this ran.
add_test(NAME allowlist_keys_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-allowlist-keys.sh --self-test)
add_test(NAME allowlist_keys
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-allowlist-keys.sh)
set_tests_properties(allowlist_keys allowlist_keys_self_test PROPERTIES
    LABELS "ci_guard;stage-A1")

# atom_roster_joined supplies the one thing fixy/Collision.h cannot supply
# for itself: a vantage point that has seen every header.  The relation is
# in the header — roster_declared_but_not_joined<Site>() and
# sample_set_wrongly_joined<Site>() — and members_of answers as of the
# point where a walk is instantiated, so a walk written in a header can
# never see a roster declared by a header included after it.  The script
# generates a sentinel over every header under include/fixy/, from the
# directory rather than from a list, and instantiates the same two
# templates from its own tag.  Stage A's acceptance gate check 6 consumes
# the same two templates rather than building a second set of atoms.
add_test(NAME atom_roster_joined_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-atom-roster-joined.sh --self-test)
add_test(NAME atom_roster_joined
  COMMAND bash ${CMAKE_SOURCE_DIR}/scripts/check-atom-roster-joined.sh --quiet)
set_tests_properties(atom_roster_joined atom_roster_joined_self_test PROPERTIES
  ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}"
  LABELS "ci_guard;stage-A1")

# parse_clean owns the one question every AST gate depends on: does the file
# parse at all.  The AST gates read a tree-sitter parse from the pinned kit
# rather than scan text, so a file that does not parse is a silent hole in all
# of them at once.  The gate fails in three directions — an unadmitted parse
# error, a roster entry whose file parses clean now, and a roster entry naming
# a path that no longer exists — so the roster in scripts/tsast.py cannot rot.
#
# Exit 3 (the pinned kit is absent) is a SKIP here, the way frozen_tree skips a
# shallow clone.  A developer who has not run scripts/install-tree-sitter.sh
# gets the instruction rather than a failure.  CI installs the kit in both the
# guards job and the build job, so CI always enforces.
add_test(NAME parse_clean_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-parse-clean.py --self-test)
add_test(NAME parse_clean
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/check-parse-clean.py)
add_test(NAME tsast_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/tsast.py --self-test)
# mintmodel derives the §XXI mint surface from the parse tree, for the guard and
# for the inventory. Its self-test plants one fixture carrying every shape the
# model must tell apart, and four of its cases are negative controls: a deleted
# overload, a constructor of a class whose name starts with mint_, a
# trailing-underscore helper, and a friend re-declaration that must not count
# as a second site.
add_test(NAME mintmodel_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/scripts/mintmodel.py --self-test)
set_tests_properties(parse_clean parse_clean_self_test tsast_self_test mintmodel_self_test PROPERTIES
    LABELS "ci_guard;ast")
set_tests_properties(parse_clean tsast_self_test mintmodel_self_test PROPERTIES SKIP_RETURN_CODE 3)
# The mutation tool weakens one gate of a header at a time and asks whether a
# test notices.  Its self-test plants a header with five gates.  Negative
# fixtures witness three of them, a test that ninja builds and runs witnesses
# a contract, and nothing witnesses the fifth.  One witnessed gate is written
# `requires(` with no space, so its mutant must stay apart from `requires`.
# The tool must report four kills and one survivor, which proves that it
# tells a witnessed gate from a bare one in each tier.  The self-test also
# proves that an invalid mutant stops a run unless a reviewed exemption names
# its gate.  Exit 3 (no tree-sitter kit) is a skip.
set(_mutation_ninja "")
if(CMAKE_GENERATOR MATCHES "Ninja")
  set(_mutation_ninja --ninja ${CMAKE_MAKE_PROGRAM})
endif()
add_test(NAME mutation_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/tools/mutation/run.py selftest
          --compiler ${CMAKE_CXX_COMPILER} ${_mutation_ninja})
set_tests_properties(mutation_self_test PROPERTIES LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)

add_subdirectory(foundation)
add_subdirectory(fixy)
add_subdirectory(session_oracle)
add_subdirectory(ct)
add_subdirectory(layer)
