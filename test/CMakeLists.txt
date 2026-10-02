# ═══════════════════════════════════════════════════════════════
# Test targets — inherit 200+ warning/hardening flags from the
# crucible INTERFACE library, plus sanitizers below.
# ═══════════════════════════════════════════════════════════════

# cmake/TestLauncher.cmake sets CRUCIBLE_CHECK_WARNINGS_DIR, the directory where
# a compile-time check writes the warnings that do not fail it
# (utils/scripts/check_report.py).  Give it to each such check as --warnings-dir.

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

# A regression test for the constant-evaluator defect that the patch
# series in utils/toolchain/gcc fixes: memchr, strchr, strrchr and strstr from
# an offset pointer.  A compiler without the fix cannot compile it.
crucible_test(test_gcc_memchr_fold)

crucible_test(test_fp_strict_floor)  # the crucible_fp_strict floor reaches a crucible consumer: compile definition and runtime witness
crucible_test(test_philox_box_muller_polynomial)  # Philox::box_muller_polynomial_det runtime witness and tier
crucible_test(test_welford_variance)  # bench_harness Welford-on-double variance — type-shape + ill-conditioned stability witness
target_include_directories(test_welford_variance SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/bench)
crucible_test(test_fence_lowering)  # mimic/Fence.h — lower_fence<Strength,Scope,Arch>() consteval lowering table (V401/V402 gated) + fence_mnemonic
crucible_test(test_hardening_syscall_atoms) # warden/Hardening.h: the syscall atoms of the apply path, their ordinals and the rows they lift to
crucible_test(test_perf_syscall_atoms) # perf/*.h: the gate row of each of the seven hub mints covers the rows of the calls its load makes
crucible_test(test_hardware_axis_pins)  # the hot headers pin their compile-time hardware constructs as fixy atoms
crucible_test(test_hdr_histogram)
crucible_test(test_synthetic_probe)

crucible_test(test_simd)

crucible_test(test_types)
crucible_test(test_kernel_cache_key)
crucible_test(test_cost_model)
crucible_test(test_call_site_table)
crucible_test(test_swiss_table)
# The AVX2 arm of crucible/SwissTable.h.  The default build selects the SSE2
# arm, and -march=native on an AVX-512 host selects the AVX-512 arm.  The x86
# floor of the tree is AVX2, so each x86 test host can run this binary.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  crucible_test(test_swiss_table_avx2 test_swiss_table.cpp)
  target_compile_options(test_swiss_table_avx2 PRIVATE -mavx2 -mno-avx512f)
endif()
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
crucible_test(test_cntp_mtls_transport)
crucible_test(test_cntp_path_swap)
crucible_test(test_cntp_pacing)
crucible_test(test_cntp_roce_config)
crucible_test(test_cntp_integrity)
# The two tests run the kernels of cntp/Fec.h that the flags of the build
# select: the AVX2 kernels under -march=native on an AVX2 host, the NEON
# kernels on aarch64, and the scalar kernels otherwise.
crucible_test(test_cntp_fec)
crucible_test(test_cntp_fountain)
crucible_test(test_cntp_overlay_multicast)
crucible_test(test_cntp_gossip_multicast)
crucible_test(test_trace_graph)
crucible_test(test_trace_loader)
crucible_test(test_schema_table)
crucible_test(test_iteration_detector)
crucible_test(test_arena)
crucible_test(test_trace_ring)
crucible_test(test_merkle_dag)
crucible_test(test_merkle_dag_visualizer)
set_target_properties(test_merkle_dag_visualizer PROPERTIES CRUCIBLE_NO_WERROR TRUE)
crucible_test(test_vis_layout)  # vis/NetworkSimplex.h optimality, vis/SugiyamaLayout.h placement and layering, detect_u_shape
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
# Release-mode (NDEBUG) witness: malformed wire bytes are rejected
# by Serialize.h's hard read_gated branch even when the precondition of the
# Refined door collapses to [[assume]].  Uses exit codes, not assert() (a no-op under NDEBUG).
crucible_test(test_serialize_release_gate)
set_tests_properties(test_serialize test_serialize_release_gate PROPERTIES
  LABELS "determinism;axiom-detsafe")
crucible_test(test_cipher)
crucible_test(test_transaction)
# The executable has its own clock_gettime, so a case can make a clock read fail.
crucible_test(test_clock_failed_read)
crucible_test(test_transaction_owner)
# Each group of tests is a source file of its own, so that no translation
# unit compiles all of them.
crucible_test(test_vigil test_vigil.cpp test_vigil_divergence.cpp test_vigil_producer.cpp)
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
# The migration of a region switch refuses a plan wider than its bitset,
# under the contract semantic of the preset and under the ignore semantic,
# where a CRUCIBLE_PRE gives its condition to the optimizer as an assumption.
crucible_test(test_crucible_context_migration)
crucible_test(test_crucible_context_migration_ignore test_crucible_context_migration.cpp)
crucible_contract_ignore_target(test_crucible_context_migration_ignore)
# Each group of tests is a source file of its own, so that no translation
# unit compiles all of them.
crucible_test(test_vigil_dispatch test_vigil_dispatch.cpp test_vigil_dispatch_threads.cpp
  test_vigil_dispatch_replay.cpp test_vigil_dispatch_pool.cpp test_vigil_dispatch_pure.cpp
  test_vigil_dispatch_pure_recovery.cpp)
# The run of each model test is in several source files: the op packets,
# the compiled replay, and the run up to the memory plan with main.
crucible_test(test_mlp_trace test_mlp_trace.cpp test_mlp_trace_ops.cpp test_mlp_trace_replay.cpp)
crucible_test(test_resnet test_resnet.cpp test_resnet_model.cpp test_resnet_replay.cpp)
crucible_test(test_vit test_vit.cpp test_vit_ops.cpp test_vit_replay.cpp)
crucible_test(test_compute)
crucible_test(test_compute_vit test_compute_vit.cpp test_compute_vit_reference.cpp test_compute_vit_attention.cpp
  test_compute_vit_head.cpp)
crucible_test(test_end_to_end test_end_to_end.cpp test_end_to_end_replay.cpp test_end_to_end_pool.cpp
  test_end_to_end_multi_iteration.cpp)
crucible_test(test_philox)
crucible_test(test_philox_simd)
crucible_test(test_philox_det)
crucible_test(test_arena_alloc_class)
crucible_test(test_pool_allocator_alloc_class)
crucible_test(test_cipher_wait)
crucible_test(test_cipher_publish_tier)
crucible_test(test_cipher_tier_promotion)
crucible_test(test_kernel_cache_levels)
crucible_test(test_trace_ring_hot_path)
crucible_test(test_cipher_commit_lifetime)
crucible_test(test_chase_lev_deque)
# The two properties -fno-exceptions and -fno-rtti stood for, held on the
# artifact of THIS build rather than asserted by flags neither preset has
# ever carried.  The gate reads each static library of the tree: each
# STATIC_LIBRARY target of the BUILDSYSTEM_TARGETS property of the root
# directory and of each directory that the root adds before this one, other
# than test/.  A new library joins the gate with no edit here.  A directory
# that the root adds after this one (bench/, examples/) holds no library of
# the tree.  utils/scripts/no-throw-no-rtti-allowlist.txt admits the throws
# and the classes that a library header makes.  The self-test needs the
# project compiler, because the system g++ may be too old for the fixtures.
add_test(NAME no_throw_no_rtti_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-throw-no-rtti.sh --self-test)
set_tests_properties(no_throw_no_rtti_self_test PROPERTIES
  ENVIRONMENT "CRUCIBLE_CXX=${CMAKE_CXX_COMPILER}"
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
set(_no_throw_artifacts "")
set(_no_throw_directories "${CMAKE_SOURCE_DIR}")
while(_no_throw_directories)
  list(POP_FRONT _no_throw_directories _no_throw_directory)
  if(_no_throw_directory STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    continue()
  endif()
  get_property(_no_throw_targets DIRECTORY "${_no_throw_directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(_no_throw_target IN LISTS _no_throw_targets)
    get_target_property(_no_throw_type ${_no_throw_target} TYPE)
    if(_no_throw_type STREQUAL "STATIC_LIBRARY")
      list(APPEND _no_throw_artifacts "$<TARGET_FILE:${_no_throw_target}>")
    endif()
  endforeach()
  get_property(_no_throw_subdirectories DIRECTORY "${_no_throw_directory}" PROPERTY SUBDIRECTORIES)
  list(APPEND _no_throw_directories ${_no_throw_subdirectories})
endwhile()
if(NOT "$<TARGET_FILE:crucible>" IN_LIST _no_throw_artifacts
   OR NOT "$<TARGET_FILE:foundation>" IN_LIST _no_throw_artifacts)
  message(FATAL_ERROR "test/CMakeLists.txt: the no-throw gate found no static library crucible or foundation in "
                      "the root directory, so it would read the wrong set of artifacts.  Define the libraries of "
                      "the tree in the root CMakeLists.txt, before add_subdirectory(test).")
endif()
add_test(NAME no_throw_no_rtti
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-throw-no-rtti.sh ${_no_throw_artifacts})
set_tests_properties(no_throw_no_rtti PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The gate reads the output of the compiler.  The strict UBSan checks test
# the dynamic type of each polymorphic object, so an artifact of that preset
# defines the typeinfo of library classes such as std::_Sp_counted_base.
# The release, default, tsan and verify presets run the gate.
if(CRUCIBLE_UBSAN_STRICT)
  set_tests_properties(no_throw_no_rtti PROPERTIES DISABLED TRUE)
endif()

# Every configure refuses a compiler that does not have each fix in
# utils/toolchain/gcc/patches.  This test runs the probes on the compiler of the
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

# The test of cmake/Ccache.cmake.  It configures a small project for each case:
# CRUCIBLE_USE_CCACHE=OFF with ccache in the environment and in -D, a launcher
# that is not ccache, and the ccache that PATH holds.
add_test(NAME ccache_launcher_self_test
  COMMAND ${CMAKE_COMMAND} -DCXX=${CRUCIBLE_REAL_CXX}
          -DWORK=${CMAKE_CURRENT_BINARY_DIR}/ccache_launcher_self_test
          -P ${CMAKE_SOURCE_DIR}/cmake/CcacheSelfTest.cmake)
set_tests_properties(ccache_launcher_self_test PROPERTIES LABELS "ci_guard")

# Every site that claims a lifetime bound carries the deleted overload
# that enforces it.  CRUCIBLE_LIFETIMEBOUND expands to nothing on the
# only compiler this tree builds with, so the claim is checked here
# rather than by an attribute no compiler reads.
add_test(NAME lifetime_twin_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-lifetime-twin.py --self-test)
add_test(NAME lifetime_twin
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-lifetime-twin.py --quiet)
set_tests_properties(lifetime_twin lifetime_twin_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)

# A substrate trait or a fail-closed relation is specialized only beside
# the declarations it names.  utils/scripts/check-trait-injection.py reads each
# specialization and each reopening of a relation namespace from the parse
# tree.  Exit 3 when the kit is absent reports a skip.
add_test(NAME trait_guard_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-trait-injection.py
          --self-test)
add_test(NAME trait_guard
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-trait-injection.py)
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
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-escape-doors.py --self-test)
add_test(NAME escape_doors
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-escape-doors.py)
set_tests_properties(escape_doors escape_doors_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The contract cites of include/ and src/, counted from the parse tree.
# --check compares the live counts with the committed baseline.  A counter
# that rises passes, and a counter that falls fails the test.  After a
# deliberate removal, write the baseline again with
# `python3 utils/scripts/audit-pre-callsite-count.py --baseline
# utils/scripts/contracts-baseline.json` and give the reason in the commit.
add_test(NAME contract_cite_regression_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/audit-pre-callsite-count.py
          --self-test)
add_test(NAME contract_cite_regression
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/audit-pre-callsite-count.py
          --check ${CMAKE_SOURCE_DIR}/utils/scripts/contracts-baseline.json)
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
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/audit-decide-cite-ratio.py
          --self-test)
add_test(NAME contract_decide_cite_ratio
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/audit-decide-cite-ratio.py)
set_tests_properties(contract_decide_cite_ratio contract_decide_cite_ratio_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# A Refined or SealedRefined parameter carries the proof of its predicate.
# A precondition in the same function that cites the decide:: procedure of
# that predicate on that parameter re-tests a proved fact, and the guard
# refuses it.  The guard reads parameters, aliases and CRUCIBLE_PRE calls
# from the parse tree.
add_test(NAME refined_pre_subsumption_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-refined-pre-subsumption.py
          --self-test)
add_test(NAME refined_pre_subsumption
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-refined-pre-subsumption.py)
set_tests_properties(refined_pre_subsumption refined_pre_subsumption_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# The contract rule: no file has a P2900 pre or post specifier.  The
# quarantine plugin rejects one in each compile.  This guard reads the parse
# tree of each tracked C++ file, so it also reads each preprocessor arm and
# each file that no build compiles.  utils/scripts/contract-form-allowlist.txt
# admits the files whose subject is the specifier.
add_test(NAME contract_form_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-contract-form.py --self-test)
add_test(NAME contract_form
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-contract-form.py
          --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(contract_form contract_form_self_test PROPERTIES
  LABELS "ci_guard;ast"
  SKIP_RETURN_CODE 3)
# GCC 16 does not keep the P2900 specifier of a template in a header unit or
# in a precompiled header, and CRUCIBLE_PRE and CRUCIBLE_POST stay.  The test
# requires the same contract error from an include, a header unit and a
# precompiled header of a template in the macro form, and from a header unit
# of fixy/Mutation.h.  Its control requires that a header unit loses the
# specifier form, so it fails when a compiler keeps the specifier.
add_test(NAME contract_header_unit
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/contract_header_unit_test.py
          --cxx ${CRUCIBLE_REAL_CXX} --include ${CMAKE_SOURCE_DIR}/include)
set_tests_properties(contract_header_unit PROPERTIES LABELS "foundation")
# The §XXI mint pattern.  utils/scripts/check-mint-pattern.py holds each mint to
# the five axes nodiscard, constexpr, noexcept, requires and ctxfit, read
# from the parse tree, and it refuses a dead or malformed exemption.
add_test(NAME mint_pattern_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-mint-pattern.py
          --self-test)
add_test(NAME mint_pattern
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-mint-pattern.py)
set_tests_properties(mint_pattern mint_pattern_self_test PROPERTIES
  LABELS "ci_guard;ast")
set_tests_properties(mint_pattern mint_pattern_self_test PROPERTIES SKIP_RETURN_CODE 3)
# The witness roster.  Every type that attests to a fact it cannot see
# — a pin, a mapping, a descriptor, a permission, a context — is named
# in utils/scripts/witness-roster.txt with the expression a forger would
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
          python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-witness-roster.py --self-test)
add_test(NAME witness_roster
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-witness-roster.py --check)
set_tests_properties(witness_roster witness_roster_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# A Permission held as a class member is the [[no_unique_address]]
# member of a handle (CLAUDE.md §IX).  Without the mark, the holder can
# be aliased across threads and the type system does not see it.
# utils/scripts/check-permission-storage.py reads each member from the parse
# tree, so an initializer, two declarators, a member over two lines, an
# array and an alias do not hide one.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME permission_storage_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-permission-storage.py
          --self-test)
add_test(NAME permission_storage
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-permission-storage.py)
set_tests_properties(permission_storage permission_storage_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# Each object with static storage duration is one per process, through
# CRUCIBLE_PROCESS_WIDE, or utils/scripts/process-wide-roster.txt says why one copy
# in each shared library is correct.  The build hides symbols, so an inline
# object that two shared libraries compile has two copies without the marker.
# utils/scripts/check-process-wide-state.py reads each object from the parse tree.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME process_wide_state_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-process-wide-state.py --self-test)
add_test(NAME process_wide_state
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-process-wide-state.py)
set_tests_properties(process_wide_state process_wide_state_self_test PROPERTIES
  LABELS "ci_guard"
  SKIP_RETURN_CODE 3)
# The reinterpret_cast ban (CLAUDE.md §III), the std::vector::reserve ban
# (CLAUDE.md §IV), the file-stream ban of the fixy-only band-3 directories
# and the process-spawn ban of production code (CLAUDE.md §IX), read from
# the parse tree of the pinned kit.  Each ban has a content-keyed allowlist:
# utils/scripts/no-reinterpret-allowlist.txt, utils/scripts/no-reserve-allowlist.txt,
# utils/scripts/no-file-stream-allowlist.txt and
# utils/scripts/no-spawn-process-allowlist.txt.  The self-test plants each shape
# that a line regex misses or misreads.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME banned_calls_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-banned-calls.py
          --self-test)
add_test(NAME banned_calls
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-banned-calls.py)
set_tests_properties(banned_calls banned_calls_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The hardware-axis gate has two halves.  test_hardware_axis_pins.cpp holds
# one row for each block, and the compiler walks the block by reflection:
# the row fails when an axis of the row has no atom alias, or an atom alias
# has an axis that the row does not list.  utils/scripts/check-fixy-hw-discipline.py
# reads the rows from the parse tree and requires a row for every `*_hw`
# namespace under include/ and src/, with its block outside every
# preprocessor conditional.  Exit 3 when the kit is absent reports a skip.
add_test(NAME fixy_hw_discipline_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fixy-hw-discipline.py
          --self-test)
add_test(NAME fixy_hw_discipline
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fixy-hw-discipline.py)
set_tests_properties(fixy_hw_discipline fixy_hw_discipline_self_test PROPERTIES
    LABELS "ci_guard;ast"
    SKIP_RETURN_CODE 3)
# The hardening grant set.  utils/scripts/check-syscall-grant-coverage.py
# derives the system calls that the hardening apply path issues from the
# parse tree, and fails when that set and the declared
# hardening_syscall_atoms tuple disagree.  Exit 3 when the kit is absent
# reports a skip.
add_test(NAME syscall_grant_coverage_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-syscall-grant-coverage.py --self-test)
add_test(NAME syscall_grant_coverage
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-syscall-grant-coverage.py)
set_tests_properties(syscall_grant_coverage syscall_grant_coverage_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# A script that writes to a git repository names it and clears the
# caller's repository variables.  utils/scripts/check-git-isolation.py refuses a
# script under utils/ that runs git init, commit, update-ref, reset, checkout,
# fetch or apply without
# utils/scripts/throwaway_repo.py, or, in shell, without the unset and the
# --absolute-git-dir check.  It reads shell with the pinned ast-grep, and
# exit 3 when that binary is absent reports a skip.
add_test(NAME git_isolation_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-git-isolation.py --self-test)
add_test(NAME git_isolation
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-git-isolation.py)
set_tests_properties(git_isolation git_isolation_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# No script reads C++ source as text.  utils/scripts/guard-engines.txt gives the
# engine of each script under utils/scripts/, test/, utils/tools/mutation/ and the
# session oracle emitter, and utils/scripts/check-guard-engines.py holds each row
# to the code: no regex or string method on the text of a tsast node, no
# grep, rg, awk or sed over C++ in a shell script, and no cxx_lex.  It reads
# shell with the pinned ast-grep, and exit 3 when that binary is absent
# reports a skip.
add_test(NAME guard_engines_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-guard-engines.py --self-test)
add_test(NAME guard_engines
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-guard-engines.py)
set_tests_properties(guard_engines guard_engines_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# A spelling of a branded type that names no brand is the erased
# identity.  utils/scripts/check-brand-drain.py reads those spellings from the
# parse tree, and utils/scripts/brand-drain.txt holds one count per file.  A
# count can only decrease.  Exit 3 when the kit is absent reports a skip.
add_test(NAME brand_drain_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-brand-drain.py
          --self-test)
add_test(NAME brand_drain
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-brand-drain.py)
set_tests_properties(brand_drain brand_drain_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# std::start_lifetime_as over an array of proofs, over an aggregate that
# holds one, and std::start_lifetime_as_array over proofs each give a
# pointer to a proof whose lifetime never started, and no property of a
# type refuses them.  utils/scripts/check-start-lifetime.py refuses each use of
# the two library names outside a reviewed and counted list.  The guard
# reads the compile database of this build, so a generated source and a
# source with an unusual suffix are in scope.  The self-test plants each
# route and each known bypass.  Exit 3 (the pinned tree-sitter kit is
# absent) is a skip.
add_test(NAME start_lifetime_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-start-lifetime.py --self-test)
add_test(NAME start_lifetime
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-start-lifetime.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(start_lifetime start_lifetime_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# A union, a cast of a void pointer, an allocator, an explicit
# instantiation and a specialization of a door each build a proof type
# with no call to its door, and the language refuses none of them.
# utils/scripts/check-proof-routes.py refuses each one outside a reviewed and
# counted list.  It reads the names of the proof types from the witness
# roster and from the reflection walk of test_forgeable_proofs, and it
# reads the compile database, so a route that a macro forms is seen too.
add_test(NAME proof_routes_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-proof-routes.py --self-test)
add_test(NAME proof_routes
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-proof-routes.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json
          --proof-names-binary $<TARGET_FILE:test_forgeable_proofs>)
set_tests_properties(proof_routes proof_routes_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# utils/scripts/preprocessed.py preprocesses each unit of the compile database
# one time for every guard that reads the preprocessed output, in a store that
# every build directory and every work tree share.  The self-test proves that
# a warm run reads the store, that a second work tree reads the units of the
# first, that a changed header makes its units stale, that two runs at one
# time share work, and that an eviction keeps the store under its bound.
add_test(NAME preprocessed_store_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/preprocessed.py --self-test)
set_tests_properties(preprocessed_store_self_test PROPERTIES
    LABELS "ci_guard")
# A linear token is an empty class, so a moved-from token looks live, and
# a second std::move of one Permission gives two owners of one region.
# sizeof(Permission) stays 1, so no byte records the move.
# utils/scripts/check-use-after-move.py walks each control-flow path of each
# function body and refuses a read of a name after a move spends it, unless
# an assignment or a reset builds the value again first.  The reviewed and
# counted exceptions are in utils/scripts/use-after-move-allowlist.txt.  The
# self-test plants the uses that the guard must catch and the correct code
# that it must accept.
add_test(NAME use_after_move_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-use-after-move.py --self-test)
add_test(NAME use_after_move
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-use-after-move.py)
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
# utils/scripts/check-no-unchecked-access.py admits it only in the files that
# utils/scripts/unchecked-access-allowlist.txt names, each with its reason.  A
# bare splice of a static data member has no door token, so the guard also
# refuses a writable private or protected static data member.  It reads the
# macro expansions of this build too, so a door that ## builds and a member
# that a macro declares are seen.  Exit 3 (the pinned tree-sitter kit is
# absent) is a skip.
add_test(NAME no_unchecked_access_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-unchecked-access.py --self-test)
add_test(NAME no_unchecked_access
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-unchecked-access.py
          --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(no_unchecked_access no_unchecked_access_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A function in a detail namespace does the work of a door with no gate of
# its own.  utils/scripts/check-detail-namespace.py refuses a use of a detail
# namespace of foundation or fixy outside the two layers.  The reviewed uses
# in tests are in utils/scripts/detail-namespace-allowlist.txt, keyed by file and
# namespace.  Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME detail_namespace_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-detail-namespace.py --self-test)
add_test(NAME detail_namespace
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-detail-namespace.py)
set_tests_properties(detail_namespace detail_namespace_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A class of the namespace foundation::effects::host is a door to a context
# key, because its first definition in any translation unit is legal C++.
# utils/scripts/check-host-owners.py refuses a definition of one outside the file
# that utils/scripts/host-owner-roster.txt names.  Exit 3 (the pinned tree-sitter
# kit is absent) is a skip.
add_test(NAME host_owners_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-host-owners.py --self-test)
add_test(NAME host_owners
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-host-owners.py)
set_tests_properties(host_owners host_owners_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# host::InitOwner::mint_init_context() is the one production door of the
# init context, and host::BackgroundOwner::mint_background_context() is the
# one production door of the background context.  Each context permits
# effects that the hot path must not have.  utils/scripts/check-ctx-init-door.py
# rejects a use of either owner in production code outside the scopes of its
# allowlist (utils/scripts/ctx-init-door-allowlist.txt, utils/scripts/ctx-bg-door-allowlist.txt),
# and a stale row.  Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME ctx_init_door_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ctx-init-door.py --self-test)
add_test(NAME ctx_init_door
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ctx-init-door.py)
set_tests_properties(ctx_init_door ctx_init_door_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# A write past the end of a C array member lands in the next member of the
# same object, where the address sanitizer sees valid memory, and no build
# checks the subscript.  utils/scripts/check-no-raw-array-member.py refuses a C
# array data member and an alias of a C array type in include/foundation and
# include/fixy.  The reviewed exceptions are in
# utils/scripts/raw-array-member-allowlist.txt, and a stale entry fails the gate.
# Exit 3 (the pinned tree-sitter kit is absent) is a skip.
add_test(NAME raw_array_member_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-raw-array-member.py --self-test)
add_test(NAME raw_array_member
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-raw-array-member.py)
set_tests_properties(raw_array_member raw_array_member_self_test PROPERTIES
    LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)
# The fast-math family is banned (CLAUDE.md §V, cmake/FpStrict.cmake is
# the floor).  utils/scripts/check-no-ffast-math.py reads the compile flags of
# this build from its compile database, the link line of each target from
# the CMake file API, the flag variables of every configure preset, and each
# optimize attribute, GCC optimize pragma and _Pragma from the parse tree,
# macro bodies included.  The self-test configures a planted project, so a
# flag inside a generator expression is judged as the compiler receives it.
add_test(NAME no_ffast_math_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-ffast-math.py
          --self-test --cmake ${CMAKE_COMMAND} --cxx ${CMAKE_CXX_COMPILER})
add_test(NAME no_ffast_math
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-ffast-math.py
          --build-dir ${CMAKE_BINARY_DIR})
set_tests_properties(no_ffast_math no_ffast_math_self_test PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
# The fixy doc-blocks name an axis by the enum spelling
# "DimensionAxis::<Name> = <ordinal>", because the retired FX numbering no
# longer matches the enum ordinals.  utils/scripts/check-fixy-dim-prose.py reads the
# comments and string literals of include/fixy from the parse tree and refuses
# the FX spellings "Dim <N>" and "(dim <N>)".  Code is not prose.  Exit 3 when
# the kit is absent reports a skip.
add_test(NAME fixy_dim_prose_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fixy-dim-prose.py --self-test)
add_test(NAME fixy_dim_prose
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fixy-dim-prose.py)
set_tests_properties(fixy_dim_prose fixy_dim_prose_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# Each direct Linux syscall call in include/, src/ and vessel/ carries a
# `// SYSCALL-CAP-OK: <reason>` comment in its statement or an entry in
# utils/scripts/syscall-capability-allowlist.txt with its effects::* capability
# claim.  utils/scripts/check-syscall-capability.py reads the calls from the
# parse tree, and an entry that matches no live call is stale.  The guard
# also holds the prose of each entry to its sites: the syscall, the
# constants the file spells, and the function that holds each call.  Exit 3
# when the kit is absent reports a skip.
add_test(NAME syscall_capability_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-syscall-capability.py
          --self-test)
add_test(NAME syscall_capability
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-syscall-capability.py)
set_tests_properties(syscall_capability syscall_capability_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The fullness discipline.  `if (count == Capacity) return false;` reads as
# a bounds check, but it refuses only the exact bound: a count already past
# Capacity compares unequal, and the next push writes out of bounds.
# utils/scripts/check-fullness-guard.py reads each `==` between a counter and a
# capacity bound from the parse tree.  A counter whose name ends in `_` is
# protected by a class invariant, by the house convention.  The content-keyed
# rows of utils/scripts/fullness-guard-allowlist.txt are all not-found sentinels.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME fullness_guard_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fullness-guard.py --self-test)
add_test(NAME fullness_guard
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-fullness-guard.py)
set_tests_properties(fullness_guard fullness_guard_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# DetSafe on the hashing path.  A ledger verdict is a property of one
# machine, so no header of content_hash, merkle_hash, the memory plan or
# the BITEXACT recipe choice may reach the ledger.
# utils/scripts/check-detsafe-ledger.py walks the transitive include closure of
# the hashing-path roots on the parse tree, and it also catches a name of
# the ledger namespace without the include.  There is no allowlist.  Exit 3
# when the kit is absent reports a skip.
add_test(NAME detsafe_ledger_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-detsafe-ledger.py --self-test)
add_test(NAME detsafe_ledger
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-detsafe-ledger.py)
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
# is what every ctx-bound mint checks for.  utils/scripts/check-ctx-testing-boundary.py
# counts each use of the door in the code that ships, from the parse tree and
# through the aliases of every file of the scan, and each file with a use needs
# a counted entry in utils/scripts/ctx-testing-boundary-allowlist.txt.
add_test(NAME ctx_testing_boundary_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ctx-testing-boundary.py --self-test)
add_test(NAME ctx_testing_boundary
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ctx-testing-boundary.py)
set_tests_properties(ctx_testing_boundary ctx_testing_boundary_self_test
    PROPERTIES LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)

# A comment, a script or an allowlist states what the code does, not which
# task, stage or tracker entry produced it.
# utils/scripts/check-no-coordination-refs.py reads every file that git tracks,
# except the notes under misc/ and the guard itself: the comments, strings and
# directive arguments of C++ from the parse tree, the comments and strings of
# Python from its tokens, the target path of a symbolic link, and every line of
# any other file.  It has no allowlist.  The self-test plants one reference of
# each family, a file of clean look-alikes and one reference in each area that
# the scan once skipped.  It proves that each plant is reported, that no
# look-alike is, that the two paths out of scope and an untracked file are not
# read, and that a linked document is reported under its own name only.
# Exit 3 when the kit is absent reports a skip.
add_test(NAME no_coordination_refs_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-coordination-refs.py --self-test)
add_test(NAME no_coordination_refs
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-coordination-refs.py)
set_tests_properties(no_coordination_refs no_coordination_refs_self_test
    PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)

# Differential tests of the session relations against the projection
# oracle of Tirore, Bengtson and Carbone (ITP 2023).  The check emits the
# tests of test/session_oracle/ from golden.csv again and compares them
# with the committed files.  It needs no oracle and no compiler.  The
# self-test compiles the emitted tests, then a copy with one wrong row
# planted in each test, which must fail on that row.  The oracle itself
# runs only when a person regenerates golden.csv.
#
# The self-test has SESSION_ORACLE_SELF_TEST_PARTS disjoint parts, and each
# part is one test.  Part K compiles each unit whose position in the list of
# units, modulo the count of parts, is K.  The parts then hold each unit one
# time, and no part takes much more than one compile of its largest unit.
# A part runs about 30 compiles at the same time, and PROCESSORS gives that
# count to CTest.  CTest then starts no more parts and tests than its job
# count lets run, and a part keeps its time.
add_test(NAME session_oracle_check
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/session-oracle.sh --check)
set_tests_properties(session_oracle_check PROPERTIES LABELS "ci_guard;session_oracle")
set(SESSION_ORACLE_SELF_TEST_PARTS 6)
math(EXPR session_oracle_last_part "${SESSION_ORACLE_SELF_TEST_PARTS} - 1")
foreach(part RANGE ${session_oracle_last_part})
  add_test(NAME session_oracle_self_test_${part}
    COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/session-oracle.sh --self-test ${CMAKE_CXX_COMPILER}
            --part ${part}/${SESSION_ORACLE_SELF_TEST_PARTS})
  set_tests_properties(session_oracle_self_test_${part} PROPERTIES
    LABELS "ci_guard;session_oracle"
    PROCESSORS 30)
endforeach()

# The row-membership discipline.  utils/scripts/check-row-contains-discipline.py
# reads each call of row_contains whose first argument is the row of a context
# from the parse tree, with any qualifier, and requires the named lift
# (CtxOwnsCapability, CtxOwnsAnyOf or CtxOwnsAllOf) instead.  Exit 3 when
# the kit is absent reports a skip.
add_test(NAME row_contains_discipline_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-row-contains-discipline.py --self-test)
add_test(NAME row_contains_discipline
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-row-contains-discipline.py)
set_tests_properties(row_contains_discipline row_contains_discipline_self_test
    PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)
# The stub discipline.  utils/scripts/check-stub-discipline.py reads the parse tree
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
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-stub-discipline.py --self-test)
add_test(NAME stub_discipline
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-stub-discipline.py)
set_tests_properties(stub_discipline stub_discipline_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The lock-free discipline of the cross-thread atomics (CLAUDE.md §IX).
# utils/scripts/check-lock-free-asserts.py reads each std::atomic, atomic_ref and
# standard atomic alias of canopy, cntp, topology and warden from the parse
# tree, and fails when neither the file nor its check file under
# test/layer/checks has a static_assert(std::atomic<T>::is_always_lock_free)
# for its type.  Without
# the assert, libatomic can put a mutex behind the atomic on another target
# with no warning.  Exit 3 when the kit is absent reports a skip.
add_test(NAME lock_free_asserts_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-lock-free-asserts.py --self-test)
add_test(NAME lock_free_asserts
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-lock-free-asserts.py)
set_tests_properties(lock_free_asserts lock_free_asserts_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# The mint inventory: its self-test, the drift gate that fails when
# misc/mint-inventory.md was not regenerated after a mint changed, and the
# HS14 floor gate.  utils/scripts/gen-mint-inventory.py holds the floor value.
# The generator reads the parse tree, so it skips (exit 3) where the pinned
# tree-sitter kit is absent.  CI installs the kit, so CI enforces.
add_test(NAME mint_inventory_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/gen-mint-inventory.py
          --self-test
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME mint_inventory_check
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/gen-mint-inventory.py
          --check
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
add_test(NAME mint_inventory_check_floor
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/gen-mint-inventory.py
          --check-floor
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
set_tests_properties(mint_inventory_self_test
                     mint_inventory_check
                     mint_inventory_check_floor PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# One combine_ids body.  utils/scripts/check-no-combine-ids-duplicate.py reads
# the parse tree and fails on a second definition named combine_ids, on a
# name with combine_ids and an affix, and on a body with the shape of
# combine_ids under any name.  Exit 3 when the kit is absent reports a skip.
add_test(NAME no_combine_ids_duplicate_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-combine-ids-duplicate.py --self-test)
add_test(NAME no_combine_ids_duplicate
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-combine-ids-duplicate.py)
set_tests_properties(no_combine_ids_duplicate
                     no_combine_ids_duplicate_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
# ci_guard label coverage.  `ctest -L ci_guard` is the local pre-merge
# check, and it skips a guard that has no label, so it reports a pass for
# a guard that never ran.  This test fails on each test that runs a script
# and has no ci_guard label.  It reads the tests that ctest lists for this
# build directory, so it sees every subdirectory and every label change.
add_test(NAME ci_guard_labels_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ci-guard-labels.py
          --self-test --cmake ${CMAKE_COMMAND} --ctest ${CMAKE_CTEST_COMMAND})
add_test(NAME ci_guard_labels
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-ci-guard-labels.py
          --build-dir ${CMAKE_BINARY_DIR} --ctest ${CMAKE_CTEST_COMMAND})
set_tests_properties(ci_guard_labels ci_guard_labels_self_test PROPERTIES
    LABELS "ci_guard")
# The cross-build row_hash determinism witness.  The test runs
# dump_row_hashes and diffs its output against the committed golden.  A
# mismatch says either that the fold changed on purpose, and the golden
# moves in the same commit, or that a TU-context-fragility regression in
# display_string_of moved stable_type_id<T> for some wrapper, which is a
# latent federation-cache bug.
#
# The fold is one fold over the graded shape, and the lattice identity is
# one of its three inputs, so every graded wrapper folds a reflected name.
# The multi-axis binding in fixy/Fn.h folds such an identity for every
# axis but two.
#
# Gated on x86_64: the payload lines fold display_string_of, so the
# absolute matrix is bit-stable only within one (arch, compiler, stdlib)
# tuple. The golden's own header prints the toolchain tag, so a diff says
# which of the two it was. The arch-independent invariants (distinctness
# and nesting-order sensitivity) run on every arch in their own tests.
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|amd64|AMD64")
  add_test(NAME row_hash_golden_diff
    COMMAND bash -c
      "set -e; \"$<TARGET_FILE:dump_row_hashes>\" > \"${CMAKE_BINARY_DIR}/row_hash_dump_current.txt\"; diff -u \"${CMAKE_SOURCE_DIR}/utils/tools/row_hash_golden.txt\" \"${CMAKE_BINARY_DIR}/row_hash_dump_current.txt\""
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
  set_tests_properties(row_hash_golden_diff PROPERTIES
      LABELS "ci_guard")
endif()
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
crucible_test(test_cntp_tcam)
crucible_test(test_stub_discipline)
crucible_test(test_quarantine)
# The roster test reads each permission tag of include/ by reflection.  It
# includes each header that declares a permission row, or a parent tag that
# gives a row, found here, so a new header joins the walk with no hand edit.
file(GLOB_RECURSE _permission_row_candidates CONFIGURE_DEPENDS RELATIVE "${CMAKE_SOURCE_DIR}/include"
     "${CMAKE_SOURCE_DIR}/include/*.h")
set(_permission_row_includes "")
foreach(_candidate IN LISTS _permission_row_candidates)
  file(STRINGS "${CMAKE_SOURCE_DIR}/include/${_candidate}" _row_lines
       REGEX "using permission_row = |using parent_type = |namespace permission_rows")
  if(_row_lines)
    string(APPEND _permission_row_includes "#include <${_candidate}>\n")
  endif()
endforeach()
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/permission_row/permission_row_headers.h"
     CONTENT "#pragma once\n@_permission_row_includes@" @ONLY)
crucible_test(test_permission_row_roster)
# crucible/ledger/ProbeSupport.h declares a tag and includes bench_harness.h.
target_include_directories(test_permission_row_roster PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/permission_row"
                           ${CMAKE_SOURCE_DIR}/bench)
crucible_test(test_fits_cog)
crucible_test(test_cog_mimic)
crucible_test(test_ledger)
# The probes time through bench/bench_harness.h rather than carrying their
# own rig — the harness already reports the quantiles, the within-run
# coefficient of variation and the drift check a ledger evidence record
# needs.  Same include-path dependency crucible_hwprobe has, and an include
# path only: this is not a bench target and takes none of the bench tree's
# -O3 -march=native.
# The refresh policy and each probe family have a test and a translation
# unit of their own, so the streaming measurement of the vector width runs
# beside the other tests and not after them, and no unit compiles every probe.
foreach(_ledger_probe_test test_ledger_probes test_ledger_probes_refresh test_ledger_probes_cache_tier
        test_ledger_probes_huge_page test_ledger_probes_vector_width)
  crucible_test(${_ledger_probe_test})
  target_include_directories(${_ledger_probe_test} PRIVATE ${CMAKE_SOURCE_DIR}/bench)
endforeach()
crucible_test(test_topology_discovery)
crucible_test(test_topology_graph)
crucible_test(test_topology_health)
crucible_test(test_topology_pingmesh)
crucible_test(test_topology_ptp)
crucible_test(test_topology_telemetry)
crucible_test(test_congestion_telemetry)
crucible_test(test_asymmetric_failure)
crucible_test(test_computation_cache)
crucible_test(test_computation_cache_integration)
crucible_test(test_computation_cache_invalidation)
crucible_test(test_federation_protocol)
crucible_test(test_cipher_record_event)
crucible_test(test_background_thread_run_in_row test_background_thread_run_in_row.cpp
  test_background_thread_run_in_row_drain.cpp test_background_thread_run_in_row_publish.cpp)
crucible_test(test_computation_cache_federation)

# A specialization of a split trait, or of one of its variables, is legal
# only in the file that defines every tag it names, or in an authoring
# location that utils/scripts/check-splits-orphan.py lists with its reason.  C++
# has no orphan rule, so a foreign TU could otherwise forge cross-region
# authority (CLAUDE.md §IX).  The guard reads each specialization from the
# parse tree.  Exit 2 names an authoring location that no specialization
# needs.  Exit 3 when the kit is absent reports a skip.
add_test(NAME splits_orphan_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-splits-orphan.py --self-test)
add_test(NAME splits_orphan
         COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-splits-orphan.py)
set_tests_properties(splits_orphan splits_orphan_self_test PROPERTIES
    LABELS "forgery;ci_guard"
    SKIP_RETURN_CODE 3)

# FederationAdmission is the one friend of the key that builds a federation
# peer token.  The guard refuses, outside fixy/Federation.h, a definition or
# a specialization of it and a definition of one of its members, which would
# get the same friendship.  It reads the parse tree, each macro body, and the
# macro-expanded text of the compile database, so a macro of another file
# cannot hide a definition.  Exit 3 when the kit is absent reports a skip.
add_test(NAME federation_admission_self_test
         COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-federation-admission.py --self-test)
add_test(NAME federation_admission
         COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-federation-admission.py
                 --compile-db ${CMAKE_BINARY_DIR}/compile_commands.json)
set_tests_properties(federation_admission federation_admission_self_test PROPERTIES
    LABELS "forgery;ci_guard;ast"
    SKIP_RETURN_CODE 3)

# The four guards that read the preprocessed store start on a full store.
# Eight setup tests fill it, each one the entries whose index is its shard
# modulo eight, so no one test preprocesses the whole database.  ctest adds
# the setup tests to a run that selects one of the four guards.
set(CRUCIBLE_STORE_SHARDS 8)
math(EXPR crucible_last_store_shard "${CRUCIBLE_STORE_SHARDS} - 1")
foreach(crucible_store_shard RANGE ${crucible_last_store_shard})
  add_test(NAME preprocessed_store_fill_${crucible_store_shard}
           COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/preprocessed.py
                   --fill ${CMAKE_BINARY_DIR}/compile_commands.json
                   --shard ${crucible_store_shard} --of ${CRUCIBLE_STORE_SHARDS})
  set_tests_properties(preprocessed_store_fill_${crucible_store_shard} PROPERTIES
      LABELS "ci_guard"
      FIXTURES_SETUP preprocessed_store)
endforeach()
set_tests_properties(start_lifetime proof_routes no_unchecked_access federation_admission PROPERTIES
    FIXTURES_REQUIRED preprocessed_store)

crucible_test(test_vessel_api_typed)
crucible_test(test_storage_nbytes_simd)
crucible_test(test_trace_ring_pop_batch)
crucible_test(test_expr_pool_fast_symbol)
# A reserve past the capacity bound is refused before the table is allocated,
# under the contract semantic of the preset and under the ignore semantic.
crucible_test(test_expr_pool_capacity)
crucible_test(test_expr_pool_capacity_ignore test_expr_pool_capacity.cpp)
crucible_contract_ignore_target(test_expr_pool_capacity_ignore)
# Each group of tests that drives a Vigil is a source file of its own, so
# that no translation unit compiles all of them.
crucible_test(test_region_cache test_region_cache.cpp test_region_cache_switch.cpp test_region_cache_migration.cpp
  test_region_cache_repeated.cpp)
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
# Snapshot lives in <crucible/perf/SenseHub.h>, and the standard
# `crucible` include path reaches it.  The bench-local include below
# carries only bench_harness.h itself.  The test checks the statistical
# primitives, Snapshot::operator- and the affinity that a Run sets on the
# calling thread.  Each short Run::measure loads the BPF sense hub when
# the crucible target defines CRUCIBLE_HAVE_BPF, and a host without the
# capabilities only prints a warning.  The SYSTEM include silences
# -Werror diagnostics from the bench headers.  The .cpp of the test still
# honours -Werror.
crucible_test(test_bench_harness)
# Exercises the crucible::warden module — bounds-enforcement policy rails
# (SCHED_FIFO/RR priority clamp, mlock, CPU-pin helpers). Header-only,
# no special capabilities required at test time (the smoke checks the
# policy arithmetic / guard object semantics, not live scheduler calls).
crucible_test(test_warden_policy)
# Each affinity change outside fixy/os goes through the recording door, so
# an older pin proof stops being in force.  ledger/ProbeSupport.h includes
# bench_harness.h.
crucible_test(test_pin_event_door)
target_include_directories(test_pin_event_door PRIVATE ${CMAKE_SOURCE_DIR}/bench)
crucible_test(test_cntp_dataplane_tc_ebpf)
crucible_test(test_cntp_dataplane_xdp)

# crucible::perf::SenseHub sentinel TU.
# Verifies the observability substrate compiles + links
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
# warden::DeadlineWatchdog wires SchedSwitch context-
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
# Vigil ↔ DeadlineWatchdog wiring: verifies the
# bg-thread-write / fg-thread-read atomic counter publication path
# does not block the main thread, and that disabled/enabled Config
# behave correctly across a region transition.
crucible_test(test_vigil_deadline_watchdog)
target_include_directories(test_bench_harness SYSTEM PRIVATE
  ${CMAKE_SOURCE_DIR}/bench)

# MetaLogSession.h — the typed-session facade over the production MetaLog
# TensorMeta side channel.  Covers the permissioned endpoints, a session
# round trip, and the handle that a session gives back at End.
crucible_test(test_metalog_session)

# SingletonInitGraph.h: the registry of runtime singletons and the gate
# that refuses a cycle in their lazy-initialization graph.
crucible_test(test_singleton_init_graph)

crucible_test(test_observe_metrics_swmr)
crucible_test(test_observe_health)
crucible_test(test_observe_sdc_detect)

# Registration against the seal on the schema and kernel tables.  Writers
# register through a view minted before the seal, and a reader reads the
# sealed table with no lock.  Under the tsan preset, a write after the seal
# races the reader and stops the test.
crucible_test(test_registration_seal_race
  tsan/test_registration_seal_race.cpp)

# ── Negative-compile fixtures of the runtime ────────────────────────
#
# A fixture lives in test/<subject>_neg/.  The subject is the part of the
# runtime that the fixture checks: a directory of include/crucible
# (canopy_neg, cntp_neg, cog_neg), or a family of its top-level headers
# (trace_neg, vigil_neg, dag_neg).  A registration names the fixture by its
# file name only.  The harness finds the one subject directory that holds
# the file, and the name of that directory is the ctest label.  A fixture
# that moves to a different subject keeps its registration.
#
# The target of a fixture is not part of the default build.  The driver
# replays the compile command of the target from compile_commands.json into
# an isolated object path.  The fixtures then run in parallel with no nested
# Ninja writer on the shared build tree.  The test passes only when the
# diagnostics of the compiler match each regex.  A fixture that compiles
# fails.  This gate is stronger than WILL_FAIL.
#
# The driver removes the caret display of GCC before it matches.  A regex
# then cannot match the source text that the compiler quotes back.  The driver
# also reports how many lines of the fixture give an error.  Set
# CRUCIBLE_NEG_STRICT_SINGLE_ERROR=1 in the environment to make a fixture
# that fails for more than one reason a failure.
#
# ── The two-regex floor ──────────────────────────────────────────────
#
# The driver requires EVERY regex that it gets.  Two regexes are an AND.
# The first says that a rejection occurred, and the second says that the
# rejection occurred to the item under test.  One regex cannot say both,
# because a regex matches through its weakest alternative, and the weakest
# alternative of `SomeConcept|constraints.*not satisfied` is text that each
# failed concept check gives.  A fixture gated on `is private within this
# context` alone passes when any member of any class in the TU is touched
# from outside.  Each registration passes two or more regexes, and the
# harness refuses a registration with fewer.
#
# Write the generic half as `.*`-joined fragments of the wording, never as
# one exact phrase.  GCC says `associated constraints are not satisfied` in
# some rejections and `constraints not satisfied` in others, and neither is
# a substring of the other.  An alternation that lists one of them stops
# matching when the rejection path of a gate changes, while each fixture
# still rejects correctly, and the ctest summary for that looks the same as
# a real regression.  `constraints.*not satisfied` covers the two spellings.
#
# The specific half is the opposite discipline: it must be the exact token
# that the compile output of the fixture carries.  Take it from the
# diagnostic, not from the name of the fixture or its comment.
#
# A token from an identifier that the fixture uses is not sufficient.  The
# fixture can fail because it cannot find that identifier, and the output
# then contains the token.
function(crucible_neg_compile_test name)
  file(GLOB sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*_neg/${name}.cpp")
  list(LENGTH sources source_count)
  if(NOT source_count EQUAL 1)
    message(FATAL_ERROR
      "crucible_neg_compile_test(${name}): ${source_count} files named ${name}.cpp are under "
      "test/*_neg/, and a registration must find exactly one.  Put the fixture in the directory of "
      "its subject, or give two fixtures different names.")
  endif()
  list(GET sources 0 source)
  get_filename_component(subject_dir "${source}" DIRECTORY)
  get_filename_component(subject "${subject_dir}" NAME)
  set(expected_diags ${ARGN})
  list(LENGTH expected_diags regex_count)
  if(regex_count LESS 2)
    message(FATAL_ERROR
      "crucible_neg_compile_test(${name}): the registration passes ${regex_count} regex, and the "
      "floor is two.  Split the regex into the generic half that shows the rejection and the "
      "project-specific half that shows WHICH gate rejected.  The driver requires both.  If the "
      "fixture has no honest second token, say so rather than invent one: a fixture that cannot "
      "name what it proves is the defect that the floor exists to find.")
  endif()
  # CMake counts brackets when it splits a list.  A regex with an unbalanced
  # `[` swallows the separator and the regex after it.  A `;` outside
  # brackets splits one regex into two.  ARGC counts what the caller wrote,
  # and the list length counts what survived.  The two disagree exactly when
  # a merge or a split occurred.
  math(EXPR written "${ARGC} - 1")
  if(NOT regex_count EQUAL written)
    message(FATAL_ERROR
      "crucible_neg_compile_test(${name}): ${written} regexes were written and ${regex_count} "
      "survived.  An unbalanced `[` merged a regex with the next one, or a `;` split a regex in "
      "two.  Balance the bracket or match it with `.`, and match a `;` with `.`")
  endif()
  add_executable(${name} EXCLUDE_FROM_ALL ${source})
  target_link_libraries(${name} PRIVATE crucible)
  add_test(NAME ${name}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver.py
            --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR}
            ${CMAKE_BINARY_DIR}
            ${source}
            ${name}
            ${expected_diags})
  set_tests_properties(${name} PROPERTIES LABELS "${subject}")
  set_property(DIRECTORY APPEND PROPERTY CRUCIBLE_NEG_REGISTERED_SOURCES "${source}")
  set_property(GLOBAL APPEND PROPERTY CRUCIBLE_NEG_FIXTURE_TESTS ${name})
endfunction()

# The driver keeps the result of each fixture compile in a store, and it uses
# a stored result only while each input of the compile is the same.  The test
# compiles small fixtures in temporary trees, each with a store of its own.  It
# changes a header, adds a header earlier in the search list, beside a fixture
# and at an earlier path of a name with `..`, adds a file that a __has_include
# probe names and the header that a fatal error misses, puts a shared object of
# cc1plus on LD_LIBRARY_PATH, changes the compiler during a compile, compiles
# with a host language that has a GCC catalog, and damages and changes an
# entry.  It also gives the driver a budget table and ledgers of its own, and
# checks the warning, the error and the ledger rows of the CPU budget and of
# the instruction budget of a compile, the record of the inputs of each run, the
# kept command of a fixture, and the earlier result that the undo of an edit
# uses.  Each check waits about one second for
# the files of its tree to age, and the checks run as
# FIXTURE_STORE_SELF_TEST_PARTS parts at the same time.  Part K runs each check
# whose position modulo the count of parts is K.  Exit 3 reports a skip when
# the compiler cannot run, or when the host cannot give the condition of each
# check of the part, for example an instruction counter.
set(FIXTURE_STORE_SELF_TEST_PARTS 20)
math(EXPR fixture_store_last_part "${FIXTURE_STORE_SELF_TEST_PARTS} - 1")
foreach(part RANGE ${fixture_store_last_part})
  add_test(NAME fixture_store_self_test_${part}
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/neg_compile_driver_test.py --cxx ${CRUCIBLE_REAL_CXX}
            --part ${part}/${FIXTURE_STORE_SELF_TEST_PARTS})
  set_tests_properties(fixture_store_self_test_${part} PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)
endforeach()

# ─── DeadlineWatchdog::observe negative fixtures (HS14) ──────────────
#   The Bg/Init/Test gate keeps a hot-foreground replay-bound thread
#   from polling the warden watchdog.  Internally the watchdog now
#   reads CLOCK_BOOTTIME (NOT CLOCK_MONOTONIC) so a host suspend
#   doesn't freeze the window.  Two distinct-mismatch-class fixtures:
#   HotFgCtx (row-content) and a bare `int` (ctx-type).
crucible_neg_compile_test(neg_deadline_watchdog_hot_ctx
  "no matching function for call to .crucible::warden::DeadlineWatchdog::observe\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsDeadlineWatchdog<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_deadline_watchdog_non_exec_ctx
  "no matching function for call to .crucible::warden::DeadlineWatchdog::observe\\(int\\)."
    "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = int.")

# ─── RdtscPinned witness negative fixtures (mint discipline) ─────────
#   RdtscPinned is the bench-local move-only proof that the
#   measurement thread was pinned to a single core via
#   BenchHarness::pin_().  Two distinct mismatch classes prove the
#   forge-resistance:
#     (1) no public default ctor (custom-ctor suppression)
#     (2) private nested mint_tag (friend-only access)
#   Both fixtures live in bench_neg/ and include <bench_harness.h>.  The
#   include directive below puts bench/ on the path of each target.
crucible_neg_compile_test(neg_bench_rdtsc_pinned_default_ctor
  "no matching function for call to .bench::RdtscPinned::RdtscPinned\\(\\)."
    "candidate 2: .constexpr bench::RdtscPinned::RdtscPinned\\(bench::CpuId, mint_tag\\).")
target_include_directories(neg_bench_rdtsc_pinned_default_ctor
  SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/bench)
crucible_neg_compile_test(neg_bench_rdtsc_pinned_mint_tag_access
  "is private within this context"
    ".struct bench::RdtscPinned::mint_tag. is private within this context")
target_include_directories(neg_bench_rdtsc_pinned_mint_tag_access
  SYSTEM PRIVATE ${CMAKE_SOURCE_DIR}/bench)

# The federation publish boundary gates on ArgsCanonicallyOrdered, which
# makes CanonicallyOrdered load-bearing: a non-canonical Args stack
# (Tagged<Stale<int>>) is rejected at federation_key, not silently mapped to
# a divergent cache slot.
crucible_neg_compile_test(neg_federation_key_noncanonical_args
  "no matching function for call to .federation_key<target, foundation::effects::Row<>, InvertedStack>\\(\\)."
    "required for the satisfaction of .ArgsCanonicallyOrdered<Args ...>. .with Args = .fixy::Tagged<fixy::Stale<int>, fixy::tags::source::FromUser>..")

# The consistency gates of lower_fence in mimic/Fence.h.  Four fixtures
# cover the distinct failure classes that the consteval lowering table
# refuses: three cross-trunk scopes (X86×accel, Arm×accel, Gpu×ARM) and one
# device-or-wider scope guarded below AcqRel.
crucible_neg_compile_test(neg_fence_x86_accel_scope
    "static assertion failed: cross-trunk fence"
    ".crucible::mimic::fence_arch_scope_consistent\\(crucible::mimic::FenceArch::X86, foundation::algebra::lattices::MemoryScope::Cta\\). evaluates to false")
crucible_neg_compile_test(neg_fence_arm_accel_scope
    "static assertion failed: cross-trunk fence"
    ".crucible::mimic::fence_arch_scope_consistent\\(crucible::mimic::FenceArch::Arm, foundation::algebra::lattices::MemoryScope::Gpu\\). evaluates to false")
crucible_neg_compile_test(neg_fence_gpu_arm_scope
    "static assertion failed: cross-trunk fence"
    ".crucible::mimic::fence_arch_scope_consistent\\(crucible::mimic::FenceArch::Gpu, foundation::algebra::lattices::MemoryScope::Inner\\). evaluates to false")
crucible_neg_compile_test(neg_fence_gpu_weak_strength
    "static assertion failed: device-or-wider GPU scope \\(Gpu / System\\) requires BarrierStrength >= AcqRel"
    "crucible::mimic::fence_strength_meets_scope\\(foundation::algebra::lattices::BarrierStrength::ReleaseStore, foundation::algebra::lattices::MemoryScope::Gpu\\)\\). evaluates to false")

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
crucible_neg_compile_test(neg_philox_box_muller_det_cipher_fence
    "constraints.*not satisfied"
    "admissible_at_cipher_fence<.*At<foundation::algebra::lattices::DetSafeTier::MonotonicClockRead>")
crucible_neg_compile_test(neg_philox_box_muller_polynomial_det_wrong_tier_consume
    "constraints.*not satisfied"
    "requires_pure_tier<.*At<foundation::algebra::lattices::DetSafeTier::PhiloxRng>")

# dispatch_workload_decision has two gates, and each fixture reaches
# one.  hot_fg passes a well-formed ProfiledDecision under the hot
# foreground context, whose empty row lacks Bg.  raw_decision passes a
# context the gate admits with a decision written by hand, which the
# parameter type refuses.
crucible_neg_compile_test(neg_workload_decision_dispatch_hot_fg
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Bg>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadDecisionDispatch<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_workload_decision_dispatch_raw_decision
    "no matching function for call to .dispatch_workload_decision\\(const fixy::BgDrainCtx&, const fixy::concurrent::ParallelismDecision&,"
    "cannot convert .raw_decision. \\(type .const fixy::concurrent::ParallelismDecision.\\) to type .const crucible::perf::ProfiledDecision&.")

# ─── The carrier width of the effect row lattice ────────────────────
#
# The lattice carries a row as a uint64_t bitmask.  The check file of
# foundation/effects/Row.h bounds the Effect enum at 64 atoms, and the
# check file of foundation/effects/Effect.h pins its underlying type to
# uint8_t.  Neither assertion can fire from a
# fixture.  Each fixture puts the same form of assertion on a test
# universe: 65 atoms, and an atom with a uint16_t underlying type.  The
# fixtures check the form of the two assertions, not the live code.
crucible_neg_compile_test(neg_effect_row_cardinality_over_carrier
    "static assertion failed: .EffectRowCarrier_Overflow."
    "the comparison reduces to .\\(65 <= 64\\).")
crucible_neg_compile_test(neg_effect_row_atom_underlying_widened
    "static assertion failed: .EffectRowCarrier_Underlying."
    ".short unsigned int. is not the same as .unsigned char.")

# ─── cog/CogIdentity.h soundness gates ─────────────────────────────
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
crucible_neg_compile_test(neg_cog_identity_content_hash_zero_uuid
    "in .constexpr. expansion of .crucible::cog::content_hash\\(ZERO_UUID_FIXTURE\\)."
    "cog/CogIdentity.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
crucible_neg_compile_test(neg_cog_identity_compute_kind_rejects_nic
    "no matching function for call to .kernel_count_for<crucible::cog::CogKind::NicPort>\\(\\)."
    "required for the satisfaction of .IsComputeKind<K>. \\[with K = crucible::cog::CogKind::NicPort\\]")

# ─── cog/TargetCaps.h soundness gates (HS14) ───────────────────────
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
crucible_neg_compile_test(neg_target_caps_caps_for_rejects_psu_rail
    "no matching function for call to .lookup_caps<crucible::cog::CogKind::PsuRail>\\(\\)."
    "required for the satisfaction of .HasCaps<K>. \\[with K = crucible::cog::CogKind::PsuRail\\]")
crucible_neg_compile_test(neg_target_caps_warp_size_not_power_of_two
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<(fixy::)?IsPowerOfTwo\\(\\), (fixy::)?BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(33\\)")

# cog/OffloadTarget.h, the one check that a Cog can carry an offload.
#   * neg_offload_target_feature_of_other_schema: a feature names a bit of
#     the schema of its own enum.
#   * neg_offload_target_no_kind: an offload names a kind of Cog.
crucible_neg_compile_test(neg_offload_target_feature_of_other_schema
    "no matching function for call to .validate_offload_target<crucible::cog::GpuFeature::GpuDirectRdma, crucible::cog::CogKind::NvSwitch>\\("
    "required for the satisfaction of .AdvertisesFeatureOf<Caps, Feature>. \\[with Caps = crucible::cog::NvSwitchTargetCaps")
crucible_neg_compile_test(neg_offload_target_no_kind
    "no matching function for call to .validate_offload_target<crucible::cog::SwitchFeature::AdaptiveRouting>\\("
    "the expression .sizeof \\.\\.\\. \\(\\(Kinds \\.\\.\\.\\)\\) > 0 \\[with Kinds = \\{\\}\\]. evaluated to .false.")

# ─── ledger/Ledger.h HS14 fixtures ────────────────────────────────
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
crucible_neg_compile_test(neg_ledger_view_hot_fg_ctx
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsLedgerStore")
crucible_neg_compile_test(neg_ledger_view_init_ctx_cannot_block
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsLedgerStore")

# The host probe reads procfs and sysfs, so CtxFitsHostProbe admits a
# context only when it owns IO and Block.  The foreground context owns
# neither, and the startup context owns IO but not Block.
crucible_neg_compile_test(neg_host_probe_hot_fg_ctx
    "no matching function for call to .probe_host_facts\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsHostProbe<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_host_probe_init_ctx_cannot_block
    "no matching function for call to .probe_competence\\(const fixy::ColdInitCtx&\\)."
    "required for the satisfaction of .CtxFitsHostProbe<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")

# ─── ledger/RefreshDaemon.h HS14 fixtures ─────────────────────────
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
crucible_neg_compile_test(neg_refresh_daemon_test_ctx_lacks_bg
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")
crucible_neg_compile_test(neg_refresh_daemon_compile_ctx_cannot_block
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")
crucible_neg_compile_test(neg_refresh_daemon_hot_fg_ctx
    "constraint|constraints.*not satisfied|no matching function"
    "CtxFitsRefreshDaemon")

# ledger/ProbeSupport.h includes <bench_harness.h> for bench::Report,
# bench::Compare and bench::compare.  Each ledger fixture needs bench/ on
# its include path.  Without it a fixture fails on the missing include,
# and the driver can report that failure as a pass for a reason that has
# nothing to do with the constraint under test.
foreach(ledger_fixture IN ITEMS
        neg_ledger_view_hot_fg_ctx neg_ledger_view_init_ctx_cannot_block
        neg_host_probe_hot_fg_ctx neg_host_probe_init_ctx_cannot_block
        neg_refresh_daemon_test_ctx_lacks_bg neg_refresh_daemon_compile_ctx_cannot_block
        neg_refresh_daemon_hot_fg_ctx)
  target_include_directories(${ledger_fixture} PRIVATE ${CMAKE_SOURCE_DIR}/bench)
endforeach()

# ─── OpcodeLatencyTable HS14 fixtures ─────────────────────────────
#
# Two fixtures, distinct mismatch classes:
#   * Concept gate (HasOpcodeTable<K>) — refuses non-substrate Cogs
#     (PsuRail) at template substitution.  It mirrors the HasCaps
#     concept gate; double-rejection on HasCaps + opcodes_for-defined.
#   * Data-invariant gate (OrderedLatencyQuantiles) — the checked mint
#     refuses a p50/p99/p999 triple out of order.  The second pattern
#     names the predicate type and the refused triple, so only this
#     refusal matches.
crucible_neg_compile_test(neg_opcode_latency_rejects_psu_rail
    "no matching function for call to .lookup_opcodes<crucible::cog::CogKind::PsuRail>\\(\\)."
    "required for the satisfaction of .HasOpcodeTable<K>. \\[with K = crucible::cog::CogKind::PsuRail\\]")
crucible_neg_compile_test(neg_opcode_latency_quantiles_misordered
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{99, 50, 30\\}\\)")

# ─── cog/FitsCog.h soundness gates (HS14) ─────────────────────────
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
crucible_neg_compile_test(neg_fits_cog_oversubscribed
    "no matching function for call to .schedule_kernel<OversubscribedSmRow, crucible::cog::CogKind::Gpu>\\(\\)."
    "the expression .row_fits_cog_v<Row, K> \\[with Row = foundation::effects::ConcurrentRow<foundation::effects::resource::SmBudget<999> >. K = crucible::cog::CogKind::Gpu\\]. evaluated to .false.")
crucible_neg_compile_test(neg_fits_cog_non_substrate
    "no matching function for call to .schedule_kernel<TrivialRow, crucible::cog::CogKind::PsuRail>\\(\\)."
    "required for the satisfaction of .HasCogCapacity<K>. \\[with K = crucible::cog::CogKind::PsuRail\\]")

# cog/NicOffloadAudit.h, the read-only NIC offload audit.
#   * neg_nic_offload_audit_rejects_gpu: the audit refuses a kind other
#     than a NIC port in its constraint.
#   * neg_nic_offload_audit_raw_required_offloads: a raw integer mask does
#     not convert to the typed offload set.
crucible_neg_compile_test(neg_nic_offload_audit_rejects_gpu
    "no matching function for call to .audit_nic_offloads<crucible::cog::CogKind::Gpu>"
    "required for the satisfaction of .NicOffloadAuditableCog<K>. \\[with K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_test(neg_nic_offload_audit_raw_required_offloads
    "could not convert .1. from .unsigned int."
    "to .fixy::Bits<crucible::cog::NicFeature>.")

# cog/NumaNic.h, the read-only NUMA placement check of a NIC.
#   * neg_numa_nic_rejects_gpu: the check refuses a kind other than a NIC
#     port in its constraint.
#   * neg_numa_nic_raw_node: a raw integer does not convert to a NUMA
#     node id.
crucible_neg_compile_test(neg_numa_nic_rejects_gpu
    "no matching function for call to .verify_numa_pinning<crucible::cog::CogKind::Gpu>"
    "required for the satisfaction of .NumaNicAuditableCog<K>. \\[with K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_test(neg_numa_nic_raw_node
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
crucible_neg_compile_test(neg_quarantine_mint_bg_ctx
    "no matching function for call to .mint_quarantine_policy<2>\\(fixy::BgDrainCtx\\)."
    "required for the satisfaction of .CtxFitsQuarantineMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_quarantine_mint_hot_fg_ctx
    "no matching function for call to .mint_quarantine_policy<2>\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsQuarantineMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_quarantine_direct_construct
    ".constexpr crucible::warden::QuarantinePolicy<MaxCogs, MaxEvents>::QuarantinePolicy\\(crucible::warden::QuarantineConfig\\) \\[with long unsigned int MaxCogs = 2. long unsigned int MaxEvents = 8\\]. is private within this context"
    "declared private here")
crucible_neg_compile_test(neg_quarantine_record_fg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2[^>]*>::on_health_event\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::topology::HealthSnapshot&, int\\)."
    "required for the satisfaction of .CtxFitsQuarantineRecord<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_quarantine_record_init_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2[^>]*>::record_recovery_probe\\(const fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::observe::ProbeOutcome&, int\\)."
    "required for the satisfaction of .CtxFitsQuarantineRecord<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >.")
crucible_neg_compile_test(neg_quarantine_override_bg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2[^>]*>::operator_override\\(fixy::BgDrainCtx, "
    "required for the satisfaction of .CtxFitsQuarantineOverride<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_quarantine_override_hot_fg_ctx
    "no matching function for call to .crucible::warden::QuarantinePolicy<2[^>]*>::operator_override\\(const fixy::HotFgCtx&, "
    "required for the satisfaction of .CtxFitsQuarantineOverride<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_quarantine_override_missing_permission
    "no matching function for call to .crucible::warden::QuarantinePolicy<2[^>]*>::operator_override\\(const fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::warden::QuarantineState, int, int\\)."
    "candidate expects 6 arguments, 5 provided")
crucible_neg_compile_test(neg_quarantine_operator_authority_no_ctx
    "static assertion failed: mint_permission_root<Tag>\\(\\) without an ExecCtx is only valid for permission_row<Tag> == Row<>"
    "In instantiation of .constexpr foundation::permissions::Permission<Tag, Brand> foundation::permissions::mint_permission_root\\(const Args& \\.\\.\\.\\) .with Tag = crucible::warden::quarantine_tag::OperatorOverride. Args = \\{\\}")
crucible_neg_compile_test(neg_quarantine_operator_authority_bg_ctx
    "no matching function for call to .mint_permission_root<crucible::warden::quarantine_tag::OperatorOverride>\\(const fixy::BgDrainCtx&\\)."
    "required for the satisfaction of .PermissionRootArgs<Tag, Args \\.\\.\\.>. .with Tag = crucible::warden::quarantine_tag::OperatorOverride. Args = \\{foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_test(neg_quarantine_zero_slots
    "In instantiation of .class crucible::warden::QuarantinePolicy<0, 4>."
    "static assertion failed: QuarantinePolicy requires Cog slots")
crucible_neg_compile_test(neg_quarantine_zero_events
    "In instantiation of .class crucible::warden::QuarantinePolicy<2, 0>."
    "static assertion failed: QuarantinePolicy requires audit-event slots")

# ─── §XXI Universal Mint Pattern for warden/ hubs ─────────────────
# Three mints (mint_hardening / mint_hot_region_registry_handle /
# mint_deadline_watchdog) each ship two HS14 neg-compile fixtures
# proving the CtxFits* concept gates fire on missing Init.
crucible_neg_compile_test(neg_hardening_mint_bg_ctx
    "no matching function for call to .mint_hardening\\(const fixy::BgDrainCtx&, crucible::warden::Policy&\\)."
    "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_hardening_mint_hot_fg_ctx
    "no matching function for call to .mint_hardening\\(const fixy::HotFgCtx&, crucible::warden::Policy&\\)."
    "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
# The cold init context owns Init but no Block, and the choice of the hot
# CPU reads sysfs.
crucible_neg_compile_test(neg_hardening_mint_cold_init_ctx
    "required for the satisfaction of .CtxFitsFileOpen<Ctx>."
    "required for the satisfaction of .CtxFitsHardeningMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_hot_region_registry_mint_bg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_test(neg_hot_region_registry_mint_hot_fg_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_test(neg_hot_region_registry_handle_direct_ctor
    "HotRegionRegistryHandle::HotRegionRegistryHandle\\(\\). is private within this context"
    "declared private here")
crucible_neg_compile_test(neg_deadline_watchdog_mint_bg_ctx
    "no matching function for call to .mint_deadline_watchdog\\(fixy::BgLoadCtx, std::nullptr_t, crucible::warden::Policy&\\)."
    "required for the satisfaction of .CtxFitsDeadlineWatchdogMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_deadline_watchdog_mint_hot_fg_ctx
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
crucible_neg_compile_test(neg_sense_hub_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sense_hub_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_sense_hub_v2_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sense_hub_v2_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")

# The six other BPF hubs (LockContention, PmuSample, SchedSwitch,
# SchedTpBtf, SyscallLatency and SyscallTpBtf) have the same gate as the
# SenseHub mint.  WorkloadProfiler borrows a hub that is already loaded,
# and its gate is a different one.
crucible_neg_compile_test(neg_lock_contention_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_lock_contention_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_pmu_sample_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_pmu_sample_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_sched_switch_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sched_switch_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_sched_tp_btf_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sched_tp_btf_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_syscall_latency_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_syscall_latency_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_syscall_tp_btf_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_syscall_tp_btf_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
crucible_neg_compile_test(neg_workload_profiler_mint_bg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Init>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadProfilerMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_workload_profiler_mint_hot_fg_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Init>. R2 = foundation::effects::Row<>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsWorkloadProfilerMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg,")
# The two constructors are private: a profiler comes only from its
# mint, and a ProfiledDecision only from a profiler.  The decision is
# not trivially copyable either, so std::bit_cast cannot forge one.
crucible_neg_compile_test(neg_workload_profiler_constructor_private
    "is private within this context"
    ".constexpr crucible::perf::WorkloadProfiler::WorkloadProfiler\\(const crucible::perf::Senses\\*, crucible::perf::WorkloadProfilerConfig\\). is private")
crucible_neg_compile_test(neg_workload_profiler_decision_constructor_private
    "is private within this context"
    ".constexpr crucible::perf::ProfiledDecision::ProfiledDecision\\(fixy::concurrent::ParallelismDecision\\). is private")
crucible_neg_compile_test(neg_workload_profiler_decision_bit_cast
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
crucible_neg_compile_test(neg_lock_contention_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_lock_contention_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsLockContentionMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_pmu_sample_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_pmu_sample_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsPmuSampleMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sched_switch_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_sched_switch_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedSwitchMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sched_tp_btf_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_sched_tp_btf_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSchedTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sense_hub_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_sense_hub_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_syscall_latency_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_syscall_latency_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallLatencyMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_syscall_tp_btf_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_syscall_tp_btf_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSyscallTpBtfMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_sense_hub_v2_mint_cold_init_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init,")
crucible_neg_compile_test(neg_sense_hub_v2_mint_bg_compile_ctx
    "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) .with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block>. R2 = foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO>.. evaluated to .false."
    "required for the satisfaction of .CtxFitsSenseHubV2Mint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")

# 3 strengthening fixtures: SECOND mismatch class per mint = non-IsExecCtx.
# CtxFitsXMint = IsExecCtx<Ctx> ∧ CtxOwnsCapability<Ctx, Init>; fixtures
# above exercise the row half (different Init-less ExecCtx shapes);
# fixtures below exercise the IsExecCtx half via raw struct rejection.
crucible_neg_compile_test(neg_hardening_mint_not_exec_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHardeningMint")
crucible_neg_compile_test(neg_hot_region_registry_mint_not_exec_ctx
    "constraint|constraints.*not satisfied"
    "CtxFitsHotRegionRegistryMint")
crucible_neg_compile_test(neg_deadline_watchdog_mint_not_exec_ctx
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
crucible_neg_compile_test(neg_xdp_raw_program_spec
    "could not convert .raw. from .crucible::cntp::dataplane::XdpProgramSpec. to .crucible::cntp::dataplane::DeclaredXdpProgram."
    "fixy::Tagged<crucible::cntp::dataplane::XdpProgramSpec, fixy::tags::source::Xdp>")
crucible_neg_compile_test(neg_xdp_mint_bg_ctx
    "no matching function for call to .mint_xdp_program\\(fixy::BgDrainCtx&"
    "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>.")
crucible_neg_compile_test(neg_xdp_mint_not_exec_ctx
    "no matching function for call to .mint_xdp_program\\(NotAnExecCtx"
    "required for the satisfaction of .IsExecCtx<Ctx>. .with Ctx = NotAnExecCtx.")
crucible_neg_compile_test(neg_xdp_map_non_pod_value
    "no matching function for call to .mint_bpf_map_spec<Key, std::string>"
    "required for the satisfaction of .BpfScalar<T>. .with T = std::__cxx11::basic_string<char")
crucible_neg_compile_test(neg_xdp_map_oversized_value
    "no matching function for call to .mint_bpf_map_spec<Key, WideValue>"
    "requires \\(BpfMapElement<Key>\\) && \\(BpfMapElement<Value>\\)")
crucible_neg_compile_test(neg_xdp_map_key_without_equality
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
crucible_neg_compile_test(neg_tc_ebpf_raw_program_spec
    "could not convert .raw. from .crucible::cntp::dataplane::TcProgramSpec."
    "to .crucible::cntp::dataplane::DeclaredTcProgram. \\{aka .fixy::Tagged<crucible::cntp::dataplane::TcProgramSpec, fixy::tags::source::TcEbpf>.\\}")
crucible_neg_compile_test(neg_tc_ebpf_mint_bg_ctx
    "no matching function for call to .mint_tc_program\\(fixy::BgDrainCtx&, "
    "required for the satisfaction of .CtxFitsTcMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_test(neg_tc_ebpf_mint_raw_ifindex
    "no matching function for call to .mint_tc_program\\(fixy::ColdInitCtx&, crucible::cntp::NicInterfaceName&, uint32_t, "
    "cannot convert .11. \\(type .uint32_t. \\{aka .unsigned int.\\}\\) to type .crucible::cntp::dataplane::TcIfIndex.")
crucible_neg_compile_test(neg_tc_ebpf_raw_flow_class
    "cannot convert .crucible::cntp::dataplane::TcFlowClass. to .crucible::cntp::dataplane::DeclaredTcFlowClass."
    "initializing argument 2 of .constexpr std::expected<void, crucible::cntp::dataplane::XdpError> crucible::cntp::dataplane::TcFlowClassMap<MaxFlows>::update\\(")
crucible_neg_compile_test(neg_tc_ebpf_flow_class_raw_dscp
    "could not convert .46. from .uint8_t. \\{aka .unsigned char.\\} to .crucible::cntp::dataplane::TcDscp."
    "fixy::Refinement<fixy::InRange<0, 63>\\(\\), unsigned char, false>")
crucible_neg_compile_test(neg_tc_ebpf_flow_class_raw_classid
    "could not convert .65537. from .uint32_t. \\{aka .unsigned int.\\} to .crucible::cntp::dataplane::TcClassId."
    "fixy::Refinement<fixy::IsPositive\\(\\), unsigned int, false>")
crucible_neg_compile_test(neg_tc_ebpf_zero_flow_map
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
crucible_neg_compile_test(neg_cog_mimic_non_substrate
    "no matching function for call to .allocate_cog_mimic_slot<crucible::cog::CogKind::PsuRail, fixy::ColdInitCtx>\\(\\)."
    "required for the satisfaction of .IsMimicSubstrate<K>. \\[with K = crucible::cog::CogKind::PsuRail\\]")
crucible_neg_compile_test(neg_cog_mimic_ctx_row_missing
    "no matching function for call to .mint_cog_mimic<crucible::cog::CogKind::Gpu>\\(const fixy::TestRunnerCtx&,"
    "required for the satisfaction of .CtxFitsCogMimic<Ctx, K>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test,")
crucible_neg_compile_test(neg_cog_mimic_mint_hot_fg_ctx
    "no matching function for call to .mint_cog_mimic<crucible::cog::CogKind::Gpu>\\(const fixy::HotFgCtx&,"
    "required for the satisfaction of .CtxFitsCogMimic<Ctx, K>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. K = crucible::cog::CogKind::Gpu\\]")
crucible_neg_compile_test(neg_cog_mimic_aggregate_build
    "no matching function for call to .crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>::CogMimic\\(<brace-enclosed initializer list>\\)."
    "no known conversion for argument 1 from .crucible::cog::CogIdentity\\*. to .const crucible::cog::CogIdentity&.")
crucible_neg_compile_test(neg_cog_mimic_private_constructor
    "is private within this context"
    ".constexpr crucible::mimic::CogMimic<K>::CogMimic\\(const crucible::cog::CogIdentity&, CalibratedCaps, OpcodeTable\\) \\[with crucible::cog::CogKind K = crucible::cog::CogKind::Gpu.")
crucible_neg_compile_test(neg_cog_mimic_default_construct
    "use of deleted function .crucible::mimic::CogMimic<K>::CogMimic\\(\\) \\[with crucible::cog::CogKind K = crucible::cog::CogKind::Gpu\\]."
    "a CogMimic binds a calibrated identity. take one from mint_cog_mimic")
crucible_neg_compile_test(neg_cog_mimic_bit_cast
    "the expression .is_trivially_copyable_v<_To> \\[with _To = crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>\\]. evaluated to .false."
    ".crucible::mimic::CogMimic<crucible::cog::CogKind::Gpu>. is not trivially copyable")
crucible_neg_compile_test(neg_cog_mimic_temporary_identity
    "use of deleted function .constexpr crucible::mimic::CogMimic<K> crucible::mimic::mint_cog_mimic\\(const Ctx&, const crucible::cog::CogIdentity&&,"
    "the CogMimic borrows its identity, so a temporary dangles at the end of the full expression. bind the identity to a name first")

# ─── topology::TopologyGraph ──────────────────────────────────────────
#
#   * neg_topology_graph_non_ctx_first_arg — an int in the context slot is
#     refused by the IsExecCtx constraint, not deep inside the factory.
#   * neg_topology_graph_test_ctx_refused — a test context carries no Init
#     row, so it cannot mint the fleet's topology graph.
crucible_neg_compile_test(neg_topology_graph_non_ctx_first_arg
    "no matching function for call to .mint_topology_graph.int,"
    "IsExecCtx<Ctx>. \\[with Ctx = int\\]")
crucible_neg_compile_test(neg_topology_graph_test_ctx_refused
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
crucible_neg_compile_test(neg_topology_discovery_mint_test_ctx
    "no matching function for call to .mint_discovery_snapshot<1, 1>.fixy::TestRunnerCtx&"
    "CtxFitsDiscoveryInit<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test")
crucible_neg_compile_test(neg_topology_discovery_direct_construct
    "is private within this context"
    "DiscoverySnapshot<MaxNodes, MaxEdges>::DiscoverySnapshot\\(\\)")
crucible_neg_compile_test(neg_topology_discovery_raw_lspci_text
    "could not convert .\\(const char\\*\\)\"Slot:.* from .const char\\*. to .crucible::topology::ExternalDiscoveryText."
    "fixy::Tagged<std::basic_string_view<char>, fixy::tags::source::External>")
crucible_neg_compile_test(neg_topology_discovery_zero_nodes
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
crucible_neg_compile_test(neg_topology_health_mint_bg_ctx
    "no matching function for call to .mint_topology_health<fixy::BgDrainCtx, 2>"
    "CtxFitsHealthMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_test(neg_topology_health_direct_construct
    "is private within this context"
    "CompositeHealthScorer<MaxPeers, Window, MaxEvents>::CompositeHealthScorer\\(crucible::topology::HealthPolicy\\)")
crucible_neg_compile_test(neg_topology_health_update_fg_ctx
    "no matching function for call to .crucible::topology::CompositeHealthScorer<2, 32, 8>::update_thermal"
    "CtxFitsHealthUpdate<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_topology_health_raw_score
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
crucible_neg_compile_test(neg_topology_telemetry_mint_bg_ctx
    "no matching function for call to .mint_nic_telemetry_history<4>\\(fixy::BgDrainCtx\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_mint_hot_fg_ctx
    "no matching function for call to .mint_nic_telemetry_history<4>\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_record_fg_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::record\\(const fixy::HotFgCtx&, const crucible::topology::NicTelemetrySnapshot&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_record_init_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::record\\(fixy::ColdInitCtx&, const crucible::topology::NicTelemetrySnapshot&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_read_fg_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::current_snapshot\\(const fixy::HotFgCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRead<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_read_test_ctx
    "no matching function for call to .crucible::topology::NicTelemetryHistory<2>::current_snapshot\\(fixy::TestRunnerCtx&\\)."
    "required for the satisfaction of .CtxFitsNicTelemetryRead<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >\\]")
crucible_neg_compile_test(neg_topology_telemetry_window_overflow
    "static assertion failed: NicTelemetryHistory stores bounded history indices in uint16_t"
    "In instantiation of .class crucible::topology::NicTelemetryHistory<65536>.")
crucible_neg_compile_test(neg_topology_telemetry_history_direct_construct
    ".constexpr crucible::topology::NicTelemetryHistory<Window>::NicTelemetryHistory\\(\\) \\[with long unsigned int Window = 4\\]. is private within this context"
    "topology/Telemetry.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_topology_telemetry_snapshot_direct_construct
    ".constexpr crucible::topology::NicTelemetrySnapshot::NicTelemetrySnapshot\\(\\). is private within this context"
    "topology/Telemetry.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_topology_telemetry_raw_netdev_text
    "could not convert .\\(const char\\*\\).rx_bytes: 1.012.. from .const char\\*."
    "to .crucible::topology::ExternalTelemetryText. \\{aka .fixy::Tagged<std::basic_string_view<char>, fixy::tags::source::External>.\\}")
crucible_neg_compile_test(neg_topology_telemetry_snapshot_raw_netdev
    "could not convert .crucible::topology::NetdevCounters\\{0, 0, 0, 0, 0, 0, 0, 0, 0, 0\\}. from .crucible::topology::NetdevCounters."
    "to .crucible::topology::DeclaredNetdevCounters. \\{aka .fixy::Tagged<crucible::topology::NetdevCounters, fixy::tags::source::KernelTelemetry>.\\}")
crucible_neg_compile_test(neg_topology_telemetry_snapshot_raw_tcp
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
crucible_neg_compile_test(neg_asymmetric_failure_mint_bg_ctx
    "no matching function for call to .mint_asymmetric_failure_detector<fixy::BgDrainCtx, 2>"
    "CtxFitsAsymmetricFailureMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_test(neg_asymmetric_failure_direct_construct
    "is private within this context"
    "AsymmetricFailureDetector<MaxPeers, Window, MaxWitnesses>::AsymmetricFailureDetector\\(crucible::topology::AsymmetricFailurePolicy\\)")
crucible_neg_compile_test(neg_asymmetric_failure_record_fg_ctx
    "no matching function for call to .crucible::topology::AsymmetricFailureDetector<2, 8, 8>::record_outbound"
    "CtxFitsAsymmetricFailureRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_asymmetric_failure_zero_window
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
crucible_neg_compile_test(neg_pingmesh_mint_bg_ctx
    "no matching function for call to .mint_pingmesh<fixy::BgDrainCtx, 2>\\(fixy::BgDrainCtx\\)"
    "CtxFitsPingmeshMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_test(neg_pingmesh_direct_construct
    "is private within this context"
    "Pingmesh<MaxPeers, Significant, MaxLatencyNs>::Pingmesh\\(crucible::topology::PingmeshConfig\\)")
crucible_neg_compile_test(neg_pingmesh_enable_pair_bg_ctx
    "no matching function for call to .crucible::topology::Pingmesh<2[^>]*>::enable_pair\\(fixy::BgDrainCtx, crucible::cog::Uuid, crucible::cog::Uuid\\)"
    "CtxFitsPingmeshMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg")
crucible_neg_compile_test(neg_pingmesh_record_fg_ctx
    "no matching function for call to .crucible::topology::Pingmesh<2[^>]*>::record_measurement\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg"
    "CtxFitsPingmeshRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_pingmesh_raw_measurement
    "no matching function for call to .crucible::topology::Pingmesh<2[^>]*>::record_measurement\\(fixy::BgDrainCtx, crucible::topology::PingmeshMeasurement&\\)"
    "cannot convert .raw. \\(type .crucible::topology::PingmeshMeasurement.\\) to type .const crucible::topology::DeclaredPingmeshMeasurement&.")
crucible_neg_compile_test(neg_pingmesh_zero_peer_capacity
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
crucible_neg_compile_test(neg_ptp_mint_bg_ctx
    "no matching function for call to .mint_ptp_handle\\(fixy::BgDrainCtx&,"
    "required for the satisfaction of .CtxFitsPtpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_ptp_handle_direct_construct
    "is private within this context"
    "crucible::topology::PtpHandle::PtpHandle\\(crucible::cog::CogIdentity, crucible::topology::PtpClockFd, const crucible::topology::PtpStatus&\\)")
crucible_neg_compile_test(neg_ptp_record_fg_ctx
    "no matching function for call to .crucible::topology::PtpHandle::record_timestamp\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
    "required for the satisfaction of .CtxFitsPtpRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_ptp_raw_fd
    "no matching function for call to .mint_ptp_handle\\(fixy::ColdInitCtx&, crucible::cog::CogIdentity&, int\\)"
    "cannot convert .3. \\(type .int.\\) to type .crucible::topology::PtpClockFd.")
crucible_neg_compile_test(neg_ptp_raw_timestamp
    "could not convert .42. from .unsigned int. to .crucible::topology::PtpTimestampNs."
    "fixy::Tagged<long unsigned int, fixy::tags::source::Ptp>")
crucible_neg_compile_test(neg_ptp_open_raw_device_index
    "no matching function for call to .open_ptp_clock\\(foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&, int\\)"
    "cannot convert .0. \\(type .int.\\) to type .crucible::topology::PtpDeviceIndex.")
crucible_neg_compile_test(neg_ptp_open_ctx_lacks_block
    "no matching function for call to .open_ptp_clock\\(fixy::BgDrainCtx&,"
    "required for the satisfaction of .CtxFitsPtpClockReaderMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_ptp_enable_raw_socket
    "could not convert .3. from .int. to .crucible::cntp::SocketFd."
    "fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>")
crucible_neg_compile_test(neg_ptp_raw_daemon_report_status
    "invalid initialization of reference of type .const crucible::topology::DeclaredPtpDaemonReport&. .aka .const fixy::Tagged<crucible::topology::PtpDaemonReport, fixy::tags::source::Ptp>&.. from expression of type .crucible::topology::PtpDaemonReport."
    "in passing argument 1 of .constexpr crucible::topology::DeclaredPtpStatus crucible::topology::ptp_status_from_daemon_report")
crucible_neg_compile_test(neg_ptp_daemon_report_fg_ctx
    "no matching function for call to .admit_ptp_daemon_report\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
    "required for the satisfaction of .CtxFitsPtpRecord<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")

# Each perf program load, and the Senses aggregate over the seven of
# them, takes the startup load context as its one parameter.  A
# background load context also claims Block, but no conversion makes it
# a startup load context, and the parameter has no default.  The two
# fixtures of each load pin these two refusals.
crucible_neg_compile_test(neg_perf_sense_hub_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SenseHub> crucible::perf::SenseHub::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_sense_hub_load_no_cap
    "no matching function for call to .crucible::perf::SenseHub::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_test(neg_perf_sched_switch_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SchedSwitch> crucible::perf::SchedSwitch::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_sched_switch_load_no_cap
    "no matching function for call to .crucible::perf::SchedSwitch::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_test(neg_perf_pmu_sample_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::PmuSample> crucible::perf::PmuSample::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_pmu_sample_load_no_cap
    "no matching function for call to .crucible::perf::PmuSample::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_test(neg_perf_lock_contention_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::LockContention> crucible::perf::LockContention::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_lock_contention_load_no_cap
    "no matching function for call to .crucible::perf::LockContention::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_test(neg_perf_syscall_latency_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SyscallLatency> crucible::perf::SyscallLatency::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_syscall_latency_load_no_cap
    "no matching function for call to .crucible::perf::SyscallLatency::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

crucible_neg_compile_test(neg_perf_senses_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static crucible::perf::Senses crucible::perf::Senses::load_all\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_senses_load_no_cap
    "no matching function for call to .crucible::perf::Senses::load_all\\(\\)."
    "candidate expects 1 argument, 0 provided")
crucible_neg_compile_test(neg_perf_sched_tp_btf_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SchedTpBtf> crucible::perf::SchedTpBtf::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_sched_tp_btf_load_no_cap
    "no matching function for call to .crucible::perf::SchedTpBtf::load\\(\\)."
    "candidate expects 1 argument, 0 provided")
crucible_neg_compile_test(neg_perf_syscall_tp_btf_load_wrong_cap
    "cannot convert .const fixy::BgLoadCtx. .aka .const foundation::effects::ExecCtx<foundation::effects::Bg,"
    "initializing argument 1 of .static std::optional<crucible::perf::SyscallTpBtf> crucible::perf::SyscallTpBtf::load\\(const fixy::InitLoadCtx&\\).")
crucible_neg_compile_test(neg_perf_syscall_tp_btf_load_no_cap
    "no matching function for call to .crucible::perf::SyscallTpBtf::load\\(\\)."
    "candidate expects 1 argument, 0 provided")

# The constructor of warden::DeadlineWatchdog is private, and
# mint_deadline_watchdog is its one route.  One fixture builds a
# watchdog with the constructor, and one calls the mint with no context.
crucible_neg_compile_test(neg_warden_deadline_watchdog_constructor_private
    "is private within this context"
    ".constexpr crucible::warden::DeadlineWatchdog::DeadlineWatchdog\\(const crucible::perf::Senses\\*, const crucible::warden::Policy&\\). is private")
crucible_neg_compile_test(neg_warden_deadline_watchdog_mint_no_ctx
    "no matching function for call to .mint_deadline_watchdog\\(std::nullptr_t, crucible::warden::Policy&\\)."
    "candidate expects 3 arguments, 2 provided")

# ─── Pattern stability classification ───────────────────────────────
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

# ElementBytes::value_ wraps Refined<bounded_above<16>,
# uint8_t>; constructing ElementBytes{17} in constexpr context drives the
# bounded_above<16>(17) contract assertion through constant evaluation.  The
# violation is rejected as a non-constant expression (P1494R5 + P2900R14),
# pinning the structural bound at the storage type rather than relying on
# the source dtype enum being well-formed.
crucible_neg_compile_test(neg_element_bytes_above_max
  "contract predicate is false in constant expression"
  "crucible/Types.h:[0-9]+:[0-9]+: error: contract predicate")

# IterationDetector::match_pos_ is the fixy Refined<bounded_above<
# MATCH_POS_MAX = K-1 = 4>, uint8_t>, and mint_refined<kMatchPosBound> is
# its one door.  Minting a value above 4 in a constant evaluation fails the
# door's precondition.  Two fixtures cover distinct forbidden values:
#   * _above_max — boundary edge (5 = K = smallest forbidden), catches
#     off-by-one drift in MATCH_POS_MAX.
#   * _uint8_max — wide miss (255 = uint8_t max), catches a wider
#     intermediate truncated to uint8_t before the mint.
# Each pins the door, the predicate and the refused value.
crucible_neg_compile_test(neg_iter_det_match_pos_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4>\\(\\), unsigned char>\\(5\\)")
crucible_neg_compile_test(neg_iter_det_match_pos_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4>\\(\\), unsigned char>\\(255\\)")

# CallSiteTable::Lineno is fixy::NonNegative<int32_t>, and the checked
# mint is its only door.  The two fixtures give the mint -1, the largest
# refused value, and the smallest int32_t.  The second regex names the
# mint, its predicate and the refused value.
crucible_neg_compile_test(neg_call_site_lineno_negative
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonNegative\\(\\), int>\\(-1\\)")
crucible_neg_compile_test(neg_call_site_lineno_int_min
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
crucible_neg_compile_test(neg_call_site_filename_raw_string
  "could not convert .* to .crucible::CallSiteTable::InternedName. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::Interned>.\\}"
  "fixy/Tagged.h:[0-9]+:[0-9]+: note: explicit conversion function was not considered")
crucible_neg_compile_test(neg_call_site_filename_external_passback
  "invalid initialization of reference of type .const ExternalName&. .*from expression of type .const crucible::CallSiteTable::InternedName."
  "in passing argument 1 of .void wants_external_only\\(const ExternalName&\\).")
crucible_neg_compile_test(neg_call_site_interned_name_to_sanitized
  "no matching function for call to .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::Interned>::retag<fixy::tags::source::Sanitized>\\(\\)."
  "required for the satisfaction of .RetagAllowed<Tag, NewTag>. \\[with Tag = fixy::tags::source::Interned. NewTag = fixy::tags::source::Sanitized\\]")

# IterationDetector::SignatureLen is the fixy BoundedMonotonic<uint32_t,
# K = 5>.  Its constructor, reached only through mint_bounded_monotonic,
# checks that the initial value is at most K, and a value above K fails
# that precondition in a constant evaluation.  Two fixtures:
#   * _above_k     — boundary edge (6 = K+1), catches off-by-one drift
#     in the bound.
#   * _uint32_max  — wide miss (UINT32_MAX), catches a regression that
#     replaces BoundedMonotonic with plain Monotonic and loses the bound.
# Each pins the door, the refused value and the precondition of the
# constructor.
crucible_neg_compile_test(neg_iter_det_signature_len_above_k
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 5>\\(6\\)"
  "BoundedMonotonic<unsigned int, 5>::within_bound_")
crucible_neg_compile_test(neg_iter_det_signature_len_uint32_max
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 5>\\(4294967295\\)"
  "BoundedMonotonic<unsigned int, 5>::within_bound_")

# IterationDetector::OpsSinceBoundary is the fixy Monotonic<uint32_t>.
# advance(smaller) and bump() at numeric_limits::max() fail the two
# mutation preconditions (monotonicity and overflow) in a constant
# evaluation.  Two fixtures:
#   * _regression — boundary edge (advance(5) from 10), catches a
#     monotonicity predicate that weakens.
#   * _overflow   — wide miss (bump() at UINT32_MAX), catches a bump()
#     that loses its != max() check and wraps to 0.
# Each pins the mutation that failed and the value it was given.
crucible_neg_compile_test(neg_iter_det_ops_since_boundary_regression
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int(, std::less<unsigned int> )?>::advance\\(5\\)")
crucible_neg_compile_test(neg_iter_det_ops_since_boundary_overflow
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int(, std::less<unsigned int> )?>::bump\\(\\)")

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
crucible_neg_compile_test(neg_iter_det_view_steady_on_building
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_view<crucible::iter_det_state::Steady, crucible::IterationDetector>\\(detector\\)"
  "fixy/ScopedView.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
crucible_neg_compile_test(neg_iter_det_view_in_field
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
crucible_neg_compile_test(neg_merkle_dag_variant_id_regression
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int(, std::less<unsigned int> )?>::advance\\(5\\)")
crucible_neg_compile_test(neg_merkle_dag_variant_id_overflow
  "__builtin_trap.*is not a constant expression"
  "Monotonic<unsigned int(, std::less<unsigned int> )?>::bump\\(\\)")

# ValidCKernelIdRaw is Refined<bounded_above<NUM_KERNELS - 1>, uint8_t>.
# It is the gate at each widening of a uint8_t to a CKernelId.  The door
# mint_refined must refuse the NUM_KERNELS sentinel (the edge of the
# bound) and UINT8_MAX (the wide miss).  The first regex says that the
# precondition of the door failed.  The second names the door and the
# refused value.
crucible_neg_compile_test(neg_ckernel_id_raw_at_sentinel
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<[0-9]+>\\(\\), unsigned char>\\(\\(\\(uint8_t\\)crucible::CKernelId::NUM_KERNELS\\)\\)")
crucible_neg_compile_test(neg_ckernel_id_raw_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<[0-9]+>\\(\\), unsigned char>\\(255\\)")

# CKernelTable::SizeCounter is BoundedMonotonic<uint32_t,
# CKERNEL_TABLE_CAP>.  The door mint_bounded_monotonic must refuse CAP + 1
# (the edge of the bound) and UINT32_MAX (the wide miss).  The second
# regex names the door and the refused value, and the third names the
# precondition of the constructor.
crucible_neg_compile_test(neg_ckernel_table_size_above_cap
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 256>\\(\\(\\(\\(uint32_t\\)crucible::CKERNEL_TABLE_CAP\\) \\+ 1\\)\\)"
  "BoundedMonotonic<unsigned int, 256>::within_bound_")
crucible_neg_compile_test(neg_ckernel_table_size_uint32_max
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 256>\\(4294967295\\)"
  "BoundedMonotonic<unsigned int, 256>::within_bound_")

# The global CKernelTable carries source::Singleton provenance.  A raw
# pointer and a pointer with a different tag must not satisfy a consumer
# of the singleton.
crucible_neg_compile_test(neg_ckernel_singleton_raw_pointer
  "could not convert .& table. from .crucible::CKernelTable\\*. to .crucible::CKernelTableSingleton."
  ".aka .fixy::Tagged<crucible::CKernelTable\\*, fixy::tags::source::Singleton>..")
crucible_neg_compile_test(neg_ckernel_singleton_cross_tag
  "could not convert .external. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Singleton>.")

# SchemaTable::SizeCounter is BoundedMonotonic<uint32_t, SCHEMA_TABLE_CAP>,
# and the door mint_bounded_monotonic refuses a value past the bound in
# constant evaluation.  The check in register_name is a second defence.
#   * _above_cap: the edge of the bound (CAP + 1).  It fails when the bound
#     widens past SCHEMA_TABLE_CAP.
#   * _uint32_max: the wide miss (UINT32_MAX).  It fails when the counter
#     becomes a plain Monotonic<uint32_t>.
crucible_neg_compile_test(neg_schema_table_size_above_cap
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 512>\\(\\(\\(\\(uint32_t\\)crucible::SCHEMA_TABLE_CAP\\) \\+ 1\\)\\)"
  "BoundedMonotonic<unsigned int, 512>::within_bound_")
crucible_neg_compile_test(neg_schema_table_size_uint32_max
  "__builtin_trap.*is not a constant expression"
  "fixy::mint_bounded_monotonic<unsigned int, 512>\\(4294967295\\)"
  "BoundedMonotonic<unsigned int, 512>::within_bound_")

# SchemaTable::lookup() returns
# LookupName = Tagged<Borrowed<const char, SchemaTable>, source::Interned>.
#   * neg_schema_table_lookup_raw_const_char: the typed borrow does not
#     convert to a raw const char* without .value().data().
#   * neg_schema_table_lookup_cross_tag: interned table output does not pass
#     as source::External.
crucible_neg_compile_test(neg_schema_table_lookup_raw_const_char
  "cannot convert .crucible::SchemaTable::LookupName. \\{aka .fixy::Tagged<fixy::Borrowed<const char, crucible::SchemaTable>, fixy::tags::source::Interned>.\\} to .const char\\*. in initialization"
  "neg_schema_table_lookup_raw_const_char.cpp:18")
crucible_neg_compile_test(neg_schema_table_lookup_cross_tag
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Interned>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. requested"
  "neg_schema_table_lookup_cross_tag.cpp:23")

# The schema table admits a name only when its tag satisfies
# SchemaNameSource: Sanitized (a boundary check retagged it) or
# FromInternal (it crossed no boundary).
#   * neg_schema_register_external_name: a trace-file name that the loader
#     did not retag is refused at register_name.
#   * neg_schema_register_abi_boundary_name: a C ABI name that the entry did
#     not retag is refused at register_schema_name.
crucible_neg_compile_test(neg_schema_register_external_name
  "neg_schema_register_external_name.cpp:24:31: error: no matching function for call to .crucible::SchemaTable::register_name"
  "required for the satisfaction of .SchemaNameSource<Tag>. \\[with Tag = fixy::tags::source::External\\]")
crucible_neg_compile_test(neg_schema_register_abi_boundary_name
  "neg_schema_register_abi_boundary_name.cpp:22:42: error: no matching function for call to .register_schema_name"
  "required for the satisfaction of .SchemaNameSource<Tag>. \\[with Tag = fixy::tags::source::ABIBoundary\\]")

# SymbolTable::add() returns
# InternalSymbolId = Tagged<SymbolId, source::FromInternal>.  Two HS14
# fixtures cover the mint boundary's two soundness directions:
#   * neg_symbol_table_add_raw_symbol_id — internal Tagged value cannot
#     decay to raw SymbolId without explicit .value().
#   * neg_symbol_table_add_cross_tag — internal provenance cannot pass
#     as source::External.
crucible_neg_compile_test(neg_symbol_table_add_raw_symbol_id
  "conversion from .crucible::InternalSymbolId. .aka .fixy::Tagged<crucible::SymbolId, fixy::tags::source::FromInternal>.."
  "to non-scalar type .crucible::SymbolId. requested")
crucible_neg_compile_test(neg_symbol_table_add_cross_tag
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::FromInternal>."
  "to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. requested")

# The ring storage of TransactionLog<N> is fixy::CyclicBuffer<Transaction,
# N>, and two gates hold the positive power-of-two capacity:
#   * _non_power_of_two (N = 7): the class-body static_assert and the
#     requires-clause of CyclicBuffer both refuse it.  A masked cursor over
#     a non-power-of-two ring aliases two logical positions onto one slot.
#   * _zero_capacity (N = 0): the power-of-two static_assert admits it,
#     because `0 & (0u - 1u)` is 0.  The minimum-size static_assert and
#     `requires (N > 0 ...)` of CyclicBuffer refuse it, and the fixture
#     pins the requires-clause.
# The bound of the fill counter lives inside CyclicBuffer.
# A claim in a full ring keeps the slots of the active transaction and the
# rollback target.  The class body also refuses a ring of less than 4
# slots.  The power-of-two gate admits N = 1 and N = 2, and only the
# minimum-size static_assert refuses them.
crucible_neg_compile_test(neg_transaction_log_non_power_of_two
  "Transaction.h:[0-9]+:[0-9]+: error: static assertion failed: N must be a power of 2"
  "the expression .\\(N & N - 1\\) == 0 \\[with N = 7\\]. evaluated to .false.")
crucible_neg_compile_test(neg_transaction_log_zero_capacity
  "Transaction.h:[0-9]+:[0-9]+: error: template constraint failure for .template<class T, long unsigned int N>  requires  N > 0"
  "the expression .N > 0 \\[with N = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_transaction_log_capacity_one
  "Transaction.h:[0-9]+:[0-9]+: error: static assertion failed: N must be at least 4: a claim keeps the slots of the active transaction and the rollback target"
  "the comparison reduces to .\\(1 >= 4\\).")
crucible_neg_compile_test(neg_transaction_log_capacity_two
  "Transaction.h:[0-9]+:[0-9]+: error: static assertion failed: N must be at least 4: a claim keeps the slots of the active transaction and the rollback target"
  "the comparison reduces to .\\(2 >= 4\\).")

# TransactionLog::commit takes the region as
# fixy::Tagged<RegionNode*, source::Arena>.  A raw RegionNode* and a region
# under the Loaded tag, which deserialize_region returns, both fail at the
# commit boundary.
crucible_neg_compile_test(neg_transaction_region_raw_pointer
  "neg_transaction_region_raw_pointer.cpp:31:33: error: cannot convert .crucible::RegionNode\\*. to .crucible::Transaction::ArenaRegion."
  "\\{aka .fixy::Tagged<crucible::RegionNode\\*, fixy::tags::source::Arena>.\\}")
crucible_neg_compile_test(neg_transaction_region_loaded_cross_tag
  "neg_transaction_region_loaded_cross_tag.cpp:33:33: error: cannot convert"
  ".Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Loaded>. to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Arena>.")

# lower_trace_to_graph requires Recorded/Replayed
# TraceGraph provenance and propagates that source to the returned Graph.
crucible_neg_compile_test(neg_lower_trace_graph_raw_input
  "no matching function for call to .lower_trace_to_graph<LowerBgAllocRow>\\(foundation::effects::cap::Alloc&, crucible::TraceGraph\\*,"
  "mismatched types .fixy::Tagged<const crucible::TraceGraph\\*, Source>. and .crucible::TraceGraph\\*.")
crucible_neg_compile_test(neg_lower_trace_graph_cross_source
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Replayed>."
  "to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Recorded>. requested")

# lower_trace_to_graph runs in the background
# compilation path and allocates Graph scratch.  Required row is
# Row<Bg, Alloc>; pure, Alloc-only, and Bg-only callers are rejected.
crucible_neg_compile_test(neg_lower_trace_graph_pure_row
  "no matching function for call to .lower_trace_to_graph<foundation::effects::Row<> >"
  "required for the satisfaction of .Subrow<foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>, CallerRow>. .with CallerRow = foundation::effects::Row<>.")
crucible_neg_compile_test(neg_lower_trace_graph_alloc_only_row
  "no matching function for call to .lower_trace_to_graph<foundation::effects::Row<foundation::effects::Effect::Alloc> >"
  "required for the satisfaction of .Subrow<foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>, CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>.")
crucible_neg_compile_test(neg_lower_trace_graph_bg_only_row
  "no matching function for call to .lower_trace_to_graph<foundation::effects::Row<foundation::effects::Effect::Bg> >"
  "required for the satisfaction of .Subrow<foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc>, CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>.")

# Transaction::step_id is fixy::Monotonic<uint64_t>.
#   * neg_transaction_step_id_assign_from_raw: Monotonic has no assignment
#     from uint64_t, so a raw write does not compile.
#   * neg_transaction_step_id_advance_retrograde: an advance to a smaller
#     value breaks the precondition of advance in a constant evaluation.
crucible_neg_compile_test(neg_transaction_step_id_assign_from_raw
  "neg_transaction_step_id_assign_from_raw.cpp:19:29: error: no match for .operator=."
  "\\(operand types are .fixy::Monotonic<long unsigned int(, std::less<long unsigned int> )?>. and .uint64_t.")
crucible_neg_compile_test(neg_transaction_step_id_advance_retrograde
  "in .constexpr. expansion of .tx.crucible::Transaction::step_id.fixy::Monotonic<long unsigned int(, std::less<long unsigned int> )?>::advance\\(50\\)."
  "error: .__builtin_trap\\(\\). is not a constant expression")

# The log holds a monotonic clock reader, so CtxFitsTransactionLog admits
# only a context whose row claims Bg, Init or Test.
#   * neg_transaction_log_foreground_ctx: the foreground context.
#   * neg_transaction_log_bg_cap_without_bg_row: a background capability
#     with a row that claims Alloc alone.
crucible_neg_compile_test(neg_transaction_log_foreground_ctx
  "neg_transaction_log_foreground_ctx.cpp:24:89: error: no matching function for call to .crucible::TransactionLog<16, Owner>::TransactionLog"
  "required for the satisfaction of .CtxFitsTransactionLog<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_transaction_log_bg_cap_without_bg_row
  "neg_transaction_log_bg_cap_without_bg_row.cpp:29:55: error: no matching function for call to .crucible::TransactionLog<16, Owner>::TransactionLog"
  "required for the satisfaction of .CtxFitsTransactionLog<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >\\]")

# The transaction log has one owner.  Each member takes the owner proof,
# a call with no proof does not compile, and a proof that could travel to
# a second thread is refused as an owner.  The publish-stage proof is
# built by the background thread alone, and neither bit_cast nor the
# checked lifetime start can make one from bytes.
crucible_neg_compile_test(neg_transaction_log_rollback_without_owner
  "neg_transaction_log_rollback_without_owner.cpp:26:23: error: no matching function for call to .crucible::TransactionLog<16, Owner>::rollback\\(\\)."
  "candidate expects 1 argument, 0 provided")
crucible_neg_compile_test(neg_transaction_log_copyable_owner
  "neg_transaction_log_copyable_owner.cpp:16:47: error: template constraint failure for .template<unsigned int N, class Owner>  requires  OwnerProof<Owner> class crucible::TransactionLog."
  "the expression .!\\(is_copy_constructible_v<P>\\) \\[with P = CopyableOwner\\]. evaluated to .false.")
crucible_neg_compile_test(neg_publish_stage_built_outside
  "is private within this context"
  ".crucible::BackgroundThread::PublishStage::PublishStage\\(\\). is private within this context")
crucible_neg_compile_test(neg_publish_stage_forged_by_bit_cast
  "no matching function for call to .bit_cast<crucible::BackgroundThread::PublishStage>\\(char\\)."
  ".crucible::BackgroundThread::PublishStage. is not trivially copyable")
crucible_neg_compile_test(neg_publish_stage_forged_by_lifetime_start
  "no matching function for call to .start_as_array<Stage>\\(unsigned char \\[1\\], int\\)."
  "required for the satisfaction of .ImplicitLifetimeThroughout<T>. .with T = crucible::BackgroundThread::PublishStage.")

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
crucible_neg_compile_test(neg_trace_node_kind_above_terminal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<3>\\(\\), unsigned char>\\(\\(\\(uint8_t\\)\\(\\(\\(int\\)\\(\\(uint8_t\\)crucible::TraceNodeKind::TERMINAL\\)\\) \\+ 1\\)\\)\\)")
crucible_neg_compile_test(neg_trace_node_kind_uint8_max
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
crucible_neg_compile_test(neg_cdag_version_raw_uint32
  "cannot convert .const crucible::CdagFormatVersion. \\{aka .const fixy::Tagged<unsigned int, fixy::tags::source::FormatVersion>.\\} to .uint32_t."
  "neg_cdag_version_raw_uint32.cpp:20")
crucible_neg_compile_test(neg_cdag_version_external_cross_tag
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
crucible_neg_compile_test(neg_deserialize_region_raw_region_ptr
  "cannot convert .std::optional<fixy::Tagged<crucible::RegionNode\\*, fixy::tags::source::Loaded> >. to .crucible::RegionNode\\*. in initialization"
  "neg_deserialize_region_raw_region_ptr.cpp:22")
crucible_neg_compile_test(neg_deserialize_region_cross_tag
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
crucible_neg_compile_test(neg_schema_name_len_below_min
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 256>\\(\\), short unsigned int>\\(0\\)")
crucible_neg_compile_test(neg_schema_name_len_above_max
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
crucible_neg_compile_test(neg_trace_num_ops_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4194304>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::MAX_OPS\\) \\+ 1\\)\\)")
crucible_neg_compile_test(neg_trace_num_ops_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<4194304>\\(\\), unsigned int>\\(4294967295\\)")
crucible_neg_compile_test(neg_trace_num_metas_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<16777216>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::MAX_METAS\\) \\+ 1\\)\\)")
crucible_neg_compile_test(neg_trace_num_metas_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<16777216>\\(\\), unsigned int>\\(4294967295\\)")
crucible_neg_compile_test(neg_trace_num_names_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<512>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::SCHEMA_TABLE_CAP\\) \\+ 1\\)\\)")
crucible_neg_compile_test(neg_trace_num_names_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<512>\\(\\), unsigned int>\\(4294967295\\)")

# TraceGraph build counters are
# WriteOnce<uint32_t>.  The fixtures pin both soundness directions:
# producers cannot raw-assign a counter, and consumers cannot read one
# through an implicit uint32_t conversion.
crucible_neg_compile_test(neg_trace_graph_num_ops_assign_raw
  "no match for .operator=. .operand types are .crucible::TraceGraph::BuiltCount. .aka .fixy::WriteOnce<unsigned int>.. and .unsigned int.."
  "no known conversion for argument 1 from .unsigned int. to .const fixy::WriteOnce<unsigned int>&.")
crucible_neg_compile_test(neg_trace_graph_num_edges_implicit_raw
  "cannot convert .crucible::TraceGraph::BuiltCount."
  ".aka .fixy::WriteOnce<unsigned int>.. to .uint32_t. .aka .unsigned int.. in initialization")

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
crucible_neg_compile_test(neg_metalog_append_count_above_max
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<1048576>\\(\\), unsigned int>\\(\\(\\(\\(unsigned int\\)crucible::MetaLog::CAPACITY\\) \\+ 1\\)\\)."
    "__builtin_trap\\(\\). is not a constant expression"
    "in expansion of macro .CRUCIBLE_PRE.")
crucible_neg_compile_test(neg_metalog_append_count_uint32_max
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<1048576>\\(\\), unsigned int>\\(4294967295\\)."
    "__builtin_trap\\(\\). is not a constant expression"
    "in expansion of macro .CRUCIBLE_PRE.")

# ─── ValidDrainCount neg-compile fixtures ─────────────────────────
#
# TraceRing::drain's `max_count` parameter (also drain_pinned,
# drain_pure, try_pop_batch) has a typed Refined gate capped at
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
# Defense-in-depth: the `count = std::min(available, max_count)`
# runtime clamp plus the precondition max_count <= CAPACITY
# plus the typed witness all fire at the boundary, exposing the
# silent clamp at enforce semantics.
crucible_neg_compile_test(neg_trace_ring_drain_count_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<65536>\\(\\), unsigned int>\\(\\(\\(\\(uint32_t\\)crucible::TraceRing::CAPACITY\\) \\+ 1\\)\\)")
crucible_neg_compile_test(neg_trace_ring_drain_count_uint32_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<65536>\\(\\), unsigned int>\\(4294967295\\)")

# ─── ndim Refined<bounded_above<8>> typed gate ────────────────────
#
# TensorMeta::ndim is structurally bounded by the inline sizes[8] /
# strides[8] arrays — values past 8 cannot be expressed without
# overrunning the inline storage.  A corrupt or adversarial Cipher
# payload can deliver a byte in [9, 255] to the deserialize boundary in
# Serialize.h::read_meta().  Without a gate there, the byte reaches
# compute_storage_nbytes, where the precondition meta.ndim <= 8
# terminates the program under enforce semantics, or where
# `[[assume]]` causes UB under ignore on hot-path TUs.
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
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<8>\\(\\), unsigned char>\\(.*crucible::kMaxTensorNDim.*\\+ 1")
crucible_neg_compile_test(neg_tensor_meta_ndim_uint8_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<8>\\(\\), unsigned char>\\(255\\)")

# read_meta dtype gate (sibling of the ndim and kernel_id gates) — ScalarType
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
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidScalarType\\(\\), signed char>\\(14\\)")
crucible_neg_compile_test(neg_tensor_meta_dtype_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidScalarType\\(\\), signed char>\\(99\\)")

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
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidDeviceType\\(\\), signed char>\\(3\\)")
crucible_neg_compile_test(neg_tensor_meta_device_type_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidDeviceType\\(\\), signed char>\\(99\\)")

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
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidLayout\\(\\), signed char>\\(-1\\)")
crucible_neg_compile_test(neg_tensor_meta_layout_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<crucible::IsValidLayout\\(\\), signed char>\\(99\\)")

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
crucible_neg_compile_test(neg_serialize_is_zero_nonzero_construct
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsZero\\(\\), long unsigned int>\\(1\\)")
crucible_neg_compile_test(neg_serialize_is_zero_uint64_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsZero\\(\\), long unsigned int>\\(18446744073709551615\\)")

# TensorMeta sizes/strides lanes
# write through TensorDim = Refined<bounded_above<INT64_MAX / 16>,
# int64_t> while keeping the raw 64-byte lane blocks for SIMD loads.
crucible_neg_compile_test(neg_tensor_meta_dim_raw_assignment
  "no match for .operator=. .operand types are .crucible::TensorDimArray::Slot. and .int.."
  "no known conversion for argument 1 from .int. to .crucible::TensorDim. .aka .fixy::Refinement<fixy::BoundedAbove<576460752303423487>\\(\\), long int, false>..")
crucible_neg_compile_test(neg_tensor_meta_dim_above_max
  "contract predicate is false in constant expression"
  "crucible/TensorMeta.h:[0-9]+:[0-9]+: error: contract predicate")

# TensorMeta::data_ptr stores
# ExternalDataPtr = Tagged<void*, source::External>.  Two HS14
# fixtures cover the provenance gate's two field-write directions:
#   * neg_tensor_meta_data_ptr_raw_pointer — raw void* cannot assign
#     directly to the explicit Tagged field.
#   * neg_tensor_meta_data_ptr_cross_tag — Tagged<void*, Sanitized>
#     cannot pass as source::External.
crucible_neg_compile_test(neg_tensor_meta_data_ptr_raw_pointer
  "no match for .operator=. .operand types are .crucible::ExternalDataPtr."
  ".aka .fixy::Tagged<void\\*, fixy::tags::source::External>.. and .void\\*..")
crucible_neg_compile_test(neg_tensor_meta_data_ptr_cross_tag
  "no match for .operator=. .operand types are .crucible::ExternalDataPtr."
  ".aka .fixy::Tagged<void\\*, fixy::tags::source::External>.. and .fixy::Tagged<void\\*, fixy::tags::source::Sanitized>..")

# TensorMeta::grad_fn_hash stores
# GradFnHash = Tagged<uint64_t, hash_family::FamilyB>.  Two HS14
# fixtures cover the Family-B gate's two field-write directions:
#   * neg_tensor_meta_grad_fn_hash_raw_uint64 — raw uint64_t cannot
#     assign directly to the explicit Tagged field.
#   * neg_tensor_meta_grad_fn_hash_cross_family — Family-A persistent
#     hashes cannot pass as process-local Family-B hashes.
crucible_neg_compile_test(neg_tensor_meta_grad_fn_hash_raw_uint64
  "no match for .operator=. .operand types are .crucible::GradFnHash."
  ".aka .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>.. and .long long unsigned int..")
crucible_neg_compile_test(neg_tensor_meta_grad_fn_hash_cross_family
  "no match for .operator=. .operand types are .crucible::GradFnHash."
  "no known conversion for argument 1 from .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyA>. to .const fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>&.")

# dim_hash_*_det returns the fixy DetSafe<Pure, Tagged<uint64_t,
# hash_family::FamilyB>>.  Two fixtures cover both fences:
#   * neg_dim_hash_raw_uint64 — raw uint64_t cannot enter the typed
#     deterministic Family-B carrier.
#   * neg_dim_hash_cross_family — Family-A values cannot substitute
#     for process-local Family-B dim hashes.
crucible_neg_compile_test(neg_dim_hash_raw_uint64
  "could not convert .raw. from .const uint64_t."
  "to .crucible::DimHashDet.")
crucible_neg_compile_test(neg_dim_hash_cross_family
  "could not convert .persistent. from .Graded<\\[...\\],\\[...\\],Tagged<\\[...\\],crucible::hash_family::FamilyA>>."
  "to .Graded<\\[...\\],\\[...\\],Tagged<\\[...\\],crucible::hash_family::FamilyB>>.")

# observe/HdrHistogram.h keeps the recording boundary typed and the merge
# geometry exact:
#   * neg_hdr_histogram_record_raw_uint64 — raw external samples cannot
#     bypass fixy::Refined<in_range<0, MaxValue>, uint64_t>.
#   * neg_hdr_histogram_merge_cross_config — histograms with different
#     significant-digit layouts cannot be merged.
crucible_neg_compile_test(neg_hdr_histogram_record_raw_uint64
  "cannot convert .uint64_t. .aka .long unsigned int.. to .crucible::observe::HdrHistogram<2, 1000000>::value_type."
  ".aka .fixy::Refinement<fixy::InRange<0, 1000000>\\(\\), long unsigned int, false>.")
crucible_neg_compile_test(neg_hdr_histogram_merge_cross_config
  "cannot convert .crucible::observe::HdrHistogram<3, 1000000>. to .const crucible::observe::HdrHistogram<2, 1000000>&."
  "initializing argument 1 of .void crucible::observe::HdrHistogram<Significant, MaxValue>::merge_from")

# observe/SyntheticProbe.h keeps synthetic-probe publication on the
# background row and transport registration typed:
#   * neg_synthetic_probe_record_fg_ctx — HotFgCtx cannot record probe
#     outcomes because it does not admit Row<Bg>.
#   * neg_synthetic_probe_raw_kind_mask — raw integer masks cannot
#     substitute for fixy::Bits<TransportProbeKind>.
crucible_neg_compile_test(neg_synthetic_probe_record_fg_ctx
  "no matching function for call to .record_gate<fixy::HotFgCtx>\\(\\)."
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Bg> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_synthetic_probe_raw_kind_mask
  "cannot convert .unsigned int. to .fixy::Bits<crucible::observe::TransportProbeKind>."
  "initializing argument 2 of .bool crucible::observe::SyntheticProbeRunner<MaxPeers>::register_peer")

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
crucible_neg_compile_test(neg_sdc_detect_mint_bg_ctx
  "constraint|constraints.*not satisfied"
    "CtxFitsSdcMint")
crucible_neg_compile_test(neg_sdc_detect_run_hot_ctx
  "constraint|constraints.*not satisfied"
    "CtxFitsSdcRun")
crucible_neg_compile_test(neg_sdc_detect_raw_result_as_verified
  "could not convert .42. from .uint64_t. .aka .long unsigned int.. to .crucible::observe::SdcVerified<long unsigned int>."
  ".aka .fixy::Tagged<long unsigned int, fixy::tags::source::SdcVerified>.")
crucible_neg_compile_test(neg_sdc_detect_cross_tag_result
  "could not convert .raw. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::SdcVerified>.")
crucible_neg_compile_test(neg_sdc_detect_sampling_rate_above_max
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 1000000>\\(\\), unsigned int>\\(1000001\\)")
crucible_neg_compile_test(neg_sdc_detect_direct_construct
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
crucible_neg_compile_test(neg_hlc_raw_timestamp_to_tagged
  "could not convert|cannot convert|no matching function"
  "HlcClockTimestamp")
crucible_neg_compile_test(neg_hlc_cross_source_timestamp
  "could not convert|cannot convert|no matching function"
  "source::External")
crucible_neg_compile_test(neg_hlc_direct_ctor
  "Hlc::Hlc\\(\\). is private within this context"
  "declared private here")
crucible_neg_compile_test(neg_hlc_mint_test_context
  "could not convert|no matching function|cannot convert"
    "effects::Test|effects::Init")
crucible_neg_compile_test(neg_hlc_mint_background_context
  "could not convert|no matching function|cannot convert"
    "effects::Bg|effects::Init")
crucible_neg_compile_test(neg_hlc_mint_without_capability
  "no matching function|too few arguments"
    "mint_hlc")

# The three mints of the runtime metrics channel, one fixture each:
# the writer mint refuses the permission of the reader role, and the
# two reader mints refuse a call without a metrics channel.
crucible_neg_compile_test(neg_observe_mint_metrics_writer_reader_permission
  "no matching function|could not convert|cannot convert|invalid initialization|cannot bind"
  "Permission|WriterTag|ReaderTag")
crucible_neg_compile_test(neg_observe_mint_keeper_metrics_reader_without_channel
  "no matching function|too few arguments"
  "mint_keeper_metrics_reader")
crucible_neg_compile_test(neg_observe_mint_canopy_metrics_reader_int_channel
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
crucible_neg_compile_test(neg_vector_clock_zero_nodes
  "required for the satisfaction of .VectorClockNodeBound<MaxNodes>. \\[with MaxNodes = 0\\]"
  "the expression .MaxNodes > 0 \\[with MaxNodes = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_vector_clock_cross_n_snapshot
  "could not convert .three. from"
  ".VectorClockSnapshot<3>. to .VectorClockSnapshot<4>.")
crucible_neg_compile_test(neg_vector_clock_direct_ctor
  "VectorClock<MaxNodes, Tag>::VectorClock\\(node_index_type\\) \\[with long unsigned int MaxNodes = 4. Tag = void. node_index_type = fixy::Refinement<fixy::BoundedAbove<3>\\(\\), short unsigned int, false>\\]"
  "is private within this context")
crucible_neg_compile_test(neg_vector_clock_mint_test_context
  "no matching function for call to .mint_vector_clock<4>\\(foundation::effects::Test, int\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_vector_clock_mint_background_context
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
crucible_neg_compile_test(neg_crdt_gset_raw_add
  "cannot convert .int. to"
  "fixy::Tagged<int, fixy::tags::source::Local>")
crucible_neg_compile_test(neg_crdt_merge_raw_state
  "no matching function for call to .crucible::canopy::GSet<int, 8>::merge"
  "merge\\(crucible::canopy::detail::BoundedHashSetState<int, 8>&\\)")
crucible_neg_compile_test(neg_crdt_merge_local_state
  "no matching function for call to .crucible::canopy::GSet<int, 8>::merge"
  "BoundedHashSetState<int, 8>, fixy::tags::source::Local>&\\)")
crucible_neg_compile_test(neg_crdt_lww_partial_clock
  "template constraint failure for .template<class V, class Clock>"
  "three_way_comparable<Clock, std::strong_ordering>. \\[with Clock = crucible::canopy::VectorClockSnapshot<4, void>\\]")
crucible_neg_compile_test(neg_crdt_mv_register_unordered_value
  "template constraint failure for .template<class V, long unsigned int MaxVersions"
  "totally_ordered<V>. \\[with V = EqualityOnlyPayload\\]")
crucible_neg_compile_test(neg_crdt_gset_floating_value
  "template constraint failure for .template<class T, long unsigned int Capacity>"
  "HashableValue<T>. \\[with T = double\\]")
crucible_neg_compile_test(neg_crdt_gset_pointer_value
  "template constraint failure for .template<class T, long unsigned int Capacity>"
  "HashableValue<T>. \\[with T = int\\*\\]")
crucible_neg_compile_test(neg_crdt_counter_update_raw_fields
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
crucible_neg_compile_test(neg_swim_zero_max_peers
  "required for the satisfaction of .SwimShape<MaxPeers, MaxPiggyback>. \\[with MaxPeers = 0. MaxPiggyback = 32\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_swim_raw_peer_add
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::SwimMembership<4, 32>::peer_type."
  "aka .fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::SwimMember>.")
crucible_neg_compile_test(neg_swim_external_peer_add
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>."
  "to .Tagged<\\[...\\],fixy::tags::source::SwimMember>.")
crucible_neg_compile_test(neg_swim_direct_ctor
  "SwimMembership<MaxPeers, MaxPiggyback>::SwimMembership\\(std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::SwimMember> >, crucible::canopy::SwimConfig\\)"
  "is private within this context")
crucible_neg_compile_test(neg_swim_mint_test_context
  "no matching function for call to .mint_swim_membership\\(foundation::effects::Test\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_swim_mint_background_context
  "no matching function for call to .mint_swim_membership<4>\\(foundation::effects::Bg\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_swim_witness_count_assign
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
crucible_neg_compile_test(neg_hyparview_zero_active
  "required for the satisfaction of .HyParViewShape<MaxActive, MaxPassive>. \\[with MaxActive = 0. MaxPassive = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_hyparview_active_exceeds_passive
  "required for the satisfaction of .HyParViewShape<MaxActive, MaxPassive>. \\[with MaxActive = 8. MaxPassive = 4\\]"
  "the expression .MaxActive <= MaxPassive \\[with MaxActive = 8. MaxPassive = 4\\]. evaluated to .false.")
crucible_neg_compile_test(neg_hyparview_raw_peer_join
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::HyParViewPeer."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::join\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_test(neg_hyparview_cross_tag_peer
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::HyParView>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::join\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_test(neg_hyparview_raw_shuffle
  "cannot convert .crucible::canopy::HyParViewShuffle<8>. to .crucible::canopy::GossipedHyParViewShuffle<8>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::apply_shuffle\\(crucible::canopy::GossipedHyParViewShuffle<MaxPassive>\\)")
crucible_neg_compile_test(neg_hyparview_external_shuffle
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::HyParViewError> crucible::canopy::HyParViewMembership<MaxActive, MaxPassive>::apply_shuffle\\(crucible::canopy::GossipedHyParViewShuffle<MaxPassive>\\)")
crucible_neg_compile_test(neg_hyparview_direct_ctor
  "HyParViewMembership<MaxActive, MaxPassive>::HyParViewMembership\\(std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::HyParView> >, std::span<const fixy::Tagged<crucible::cog::CogIdentity, fixy::tags::source::HyParView> >, crucible::canopy::HyParViewConfig\\)"
  "is private within this context")
crucible_neg_compile_test(neg_hyparview_mint_test_context
  "no matching function for call to .mint_hyparview<4>\\(foundation::effects::Test\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_hyparview_mint_background_context
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
crucible_neg_compile_test(neg_plumtree_zero_peers
  "required for the satisfaction of .PlumtreeShape<MaxPeers, MaxHistory>. \\[with MaxPeers = 0. MaxHistory = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_plumtree_overlay_too_large
  "required for the satisfaction of .PlumtreeFitsOverlay<MaxPeers, MaxHistory, HyMaxActive, HyMaxPassive>. \\[with MaxPeers = 4. MaxHistory = 8. HyMaxActive = 8. HyMaxPassive = 64\\]"
  "the expression .HyMaxActive <= MaxPeers \\[with HyMaxActive = 8. MaxPeers = 4\\]. evaluated to .false.")
crucible_neg_compile_test(neg_plumtree_raw_peer
  "cannot convert .crucible::cog::CogIdentity. to .crucible::canopy::HyParViewPeer."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::add_eager_peer\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_test(neg_plumtree_external_peer
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::HyParView>."
  "initializing argument 1 of .constexpr std::expected<void, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::add_lazy_peer\\(crucible::canopy::HyParViewPeer\\)")
crucible_neg_compile_test(neg_plumtree_raw_message
  "cannot convert .crucible::canopy::PlumtreeMessage. to .crucible::canopy::GossipedPlumtreeMessage."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeReceivePlan<MaxPeers, MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_message\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeMessage\\)")
crucible_neg_compile_test(neg_plumtree_external_message
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeReceivePlan<MaxPeers, MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_message\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeMessage\\)")
crucible_neg_compile_test(neg_plumtree_raw_ihave
  "cannot convert .crucible::canopy::PlumtreeIHave<8>. to .crucible::canopy::GossipedPlumtreeIHave<8>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeRepairPlan<MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_ihave\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeIHave<MaxHistory>\\)")
crucible_neg_compile_test(neg_plumtree_external_ihave
  "cannot convert .Tagged<\\[...\\],fixy::tags::source::External>. to .Tagged<\\[...\\],fixy::tags::source::Gossiped>."
  "initializing argument 2 of .constexpr std::expected<crucible::canopy::PlumtreeRepairPlan<MaxHistory>, crucible::canopy::PlumtreeError> crucible::canopy::PlumtreeBroadcast<MaxPeers, MaxHistory>::receive_ihave\\(crucible::canopy::HyParViewPeer, crucible::canopy::GossipedPlumtreeIHave<MaxHistory>\\)")
crucible_neg_compile_test(neg_plumtree_direct_ctor
  "PlumtreeBroadcast<MaxPeers, MaxHistory>::PlumtreeBroadcast\\(const crucible::canopy::HyParViewMembership<[A-Za-z]+, [A-Za-z]+>&, crucible::canopy::PlumtreeConfig\\)"
  "is private within this context")
crucible_neg_compile_test(neg_plumtree_mint_test_context
  "no matching function for call to .mint_plumtree<4, 8>\\(foundation::effects::Test, crucible::canopy::HyParViewMembership<4, 8>&\\)."
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_plumtree_mint_background_context
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
crucible_neg_compile_test(neg_lifeguard_zero_peers
  "required for the satisfaction of .LifeguardShape<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>. \\[with MaxPeers = 0. MaxPiggyback = 8. RttWindow = 4. MaxEvents = 8\\]"
  "the expression .Capacity > 0 \\[with Capacity = 0\\]. evaluated to .false.")
crucible_neg_compile_test(neg_lifeguard_raw_local_peer
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Init, crucible::cog::CogIdentity&\\)."
  "cannot convert .local. \\(type .crucible::cog::CogIdentity.\\) to type .crucible::canopy::SwimPeer.")
crucible_neg_compile_test(neg_lifeguard_direct_ctor
  "LifeguardSwim<MaxPeers, MaxPiggyback, RttWindow, MaxEvents>::LifeguardSwim\\(foundation::effects::Init, crucible::canopy::SwimPeer,"
  "is private within this context")
crucible_neg_compile_test(neg_lifeguard_mint_test_context
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Test,"
  "cannot convert .foundation::effects::testing::test\\(\\). \\(type .foundation::effects::Test.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_lifeguard_mint_background_context
  "no matching function for call to .mint_lifeguard_swim<4>\\(foundation::effects::Bg, crucible::canopy::SwimPeer\\)."
  "cannot convert .foundation::effects::testing::bg\\(\\). \\(type .foundation::effects::Bg.\\) to type .foundation::effects::Init.")
crucible_neg_compile_test(neg_lifeguard_event_count_advance
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
crucible_neg_compile_test(neg_scuttlebutt_zero_max_peers
  "template constraint failure for .template<long unsigned int MaxPeers, long unsigned int MaxKeys>"
  "ScuttlebuttShape<MaxPeers, MaxKeys>. \\[with MaxPeers = 0. MaxKeys = 4\\]")
crucible_neg_compile_test(neg_scuttlebutt_raw_key_register
  "no matching function for call to .crucible::canopy::ScuttlebuttSync<4, 4>::register_state\\(crucible::canopy::ScuttlebuttKey&"
  "cannot convert .key. \\(type .crucible::canopy::ScuttlebuttKey.\\) to type .crucible::canopy::LocalScuttlebuttKey.")
crucible_neg_compile_test(neg_scuttlebutt_raw_delta_apply
  "cannot convert .crucible::canopy::ScuttlebuttDelta<crucible::canopy::detail::BoundedHashSetState<long unsigned int, 4> >. to"
  "fixy::Tagged<crucible::canopy::ScuttlebuttDelta<crucible::canopy::detail::BoundedHashSetState<long unsigned int, 4> >, fixy::tags::source::Gossiped>")
crucible_neg_compile_test(neg_scuttlebutt_raw_digest_compare
  "cannot convert .crucible::canopy::ScuttlebuttDigest<4, 4>. to"
  "fixy::Tagged<crucible::canopy::ScuttlebuttDigest<4, 4>, fixy::tags::source::Gossiped>")
crucible_neg_compile_test(neg_scuttlebutt_non_crdt_publish
  "no matching function for call to .crucible::canopy::ScuttlebuttSync<4, 4>::publish_local_change"
  "required for the satisfaction of .ScuttlebuttCrdt<C>. \\[with C = NotCrdt\\]")
crucible_neg_compile_test(neg_scuttlebutt_direct_ctor
  "ScuttlebuttSync<MaxPeers, MaxKeys>::ScuttlebuttSync\\(peer_type, std::span<"
  "is private within this context")
crucible_neg_compile_test(neg_scuttlebutt_mint_test_context
  "could not convert .foundation::effects::testing::test\\(\\)."
  "from .foundation::effects::Test. to .foundation::effects::Init.")
crucible_neg_compile_test(neg_scuttlebutt_mint_background_context
  "could not convert .foundation::effects::testing::bg\\(\\)."
  "from .foundation::effects::Bg. to .foundation::effects::Init.")
crucible_neg_compile_test(neg_scuttlebutt_digest_count_assign
  "no match for .operator=. \\(operand types are .crucible::canopy::SlotCount<4>."
  "and .uint16_t.")
crucible_neg_compile_test(neg_scuttlebutt_request_count_advance
  "no match for .operator\\+\\+."
  "\\(operand type is .crucible::canopy::SlotCount<4>.\\)")

# cntp/CongestionControl.h per-socket TCP CC selection.
#   * neg_congestion_control_raw_algorithm_apply: raw enum values cannot
#     drive TCP_CONGESTION; callers must use a tagged DeclaredCcChoice.
#   * neg_congestion_control_dctcp_cross_dc: DCTCP is lossless-fabric only.
#   * neg_congestion_control_custom_missing_name: custom CC modules must
#     provide a constexpr kernel congestion-control name.
#   * neg_congestion_control_custom_invalid_name: that name must also be
#     a valid kernel name, which has no uppercase letter.
crucible_neg_compile_test(neg_congestion_control_raw_algorithm_apply
  "no matching function for call to .set_cc_for_socket\\(fixy::InitLoadCtx&, fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>&, crucible::cntp::CcAlgorithm\\)."
  "cannot convert .crucible::cntp::CcAlgorithm::Cubic. \\(type .crucible::cntp::CcAlgorithm.\\) to type .crucible::cntp::DeclaredCcChoice.")
crucible_neg_compile_test(neg_congestion_control_dctcp_cross_dc
  "no matching function for call to .mint_cc_choice<crucible::cntp::CcAlgorithm::Dctcp, crucible::cntp::LinkClass::CrossDatacenter>\\(\\)."
  "required for the satisfaction of .CcCompatible<Algorithm, Link>. .with Algorithm = crucible::cntp::CcAlgorithm::Dctcp. Link = crucible::cntp::LinkClass::CrossDatacenter.")
crucible_neg_compile_test(neg_congestion_control_custom_missing_name
  "no matching function for call to .mint_custom_cc_choice<MissingName, crucible::cntp::LinkClass::PublicInternet>\\(\\)."
  "the required expression .Module::congestion_control_name\\(\\). is invalid")
crucible_neg_compile_test(neg_congestion_control_custom_invalid_name
  "no matching function for call to .mint_custom_cc_choice<UppercaseName, crucible::cntp::LinkClass::PublicInternet>\\(\\)."
  "nested requirement .crucible::cntp::KernelCcName::from\\(Module::congestion_control_name\\(\\)\\).has_value\\(\\). is not satisfied")

# The socket option gate of cntp/CongestionControl.h.  A socket option
# call takes the socket lock, so the gate asks for IO and Block.
#   * neg_socket_option_cold_init_ctx: a context with IO and no Block.
#   * neg_socket_option_bg_drain_ctx: a context with neither.
crucible_neg_compile_test(neg_socket_option_cold_init_ctx
  "no matching function for call to .set_cc_for_socket\\(fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsSocketOption<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_test(neg_socket_option_bg_drain_ctx
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
crucible_neg_compile_test(neg_calibrate_psu_rail
  "constraints.*not satisfied"
    "CalibratableCogKind")
crucible_neg_compile_test(neg_calibrate_zero_latency_quantile
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::IsMedianPositive\\(\\)>\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{0, 1, 2\\}\\)")
crucible_neg_compile_test(neg_calibrate_misordered_latency_quantile
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<crucible::cog::AreQuantilesOrdered\\(\\), crucible::cog::IsMedianPositive\\(\\)>\\(\\), crucible::cog::LatencyQuantiles>\\(crucible::cog::LatencyQuantiles\\{90, 50, 70\\}\\)")
crucible_neg_compile_test(neg_calibrate_nonpositive_throughput
    "__builtin_trap\\(\\).* is not a constant expression"
    "mint_refined<(fixy::)?refined_algebra::AllOf<(fixy::)?IsPositive\\(\\), (fixy::)?BoundedAbove<1.0e\\+30>\\(\\)>\\(\\), double>\\(0.0\\)")
crucible_neg_compile_test(neg_calibrate_hot_fg_ctx
  "constraints.*not satisfied"
    "CtxFitsCalibration")

# cog/NicConfig.h, the admission of NIC configuration intent.
#   * neg_nic_config_raw_apply: a raw plan does not convert to the
#     declared configuration that a privileged apply takes.
#   * neg_nic_config_ring_not_power_of_two: the checked mint of a ring
#     size stops the constant evaluation at 1000.
#   * neg_nic_config_mint_bg_ctx, neg_nic_config_mint_hot_fg_ctx: the
#     mint refuses a context whose row holds no Init.
crucible_neg_compile_test(neg_nic_config_raw_apply
    "could not convert .raw. from .crucible::cog::nic::NicConfigPlan."
    "to .crucible::cog::nic::DeclaredNicConfig. \\{aka .fixy::Tagged<crucible::cog::nic::NicConfigPlan, fixy::tags::source::NicConfig>.\\}")
crucible_neg_compile_test(neg_nic_config_ring_not_power_of_two
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), InRange<256, 8192>\\(\\)>\\(\\), short unsigned int>\\(1000\\)")
crucible_neg_compile_test(neg_nic_config_mint_bg_ctx
    "no matching function for call to .mint_nic_config\\(fixy::BgDrainCtx"
    "required for the satisfaction of .CtxFitsNicConfigMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_nic_config_mint_hot_fg_ctx
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
crucible_neg_compile_test(neg_sriov_raw_plan_enable
    "could not convert .plan. from .crucible::cog::sriov::SrIovPlan."
    "to .crucible::cog::sriov::DeclaredSrIovPlan. \\{aka .fixy::Tagged<crucible::cog::sriov::SrIovPlan, fixy::tags::source::SrIov>.\\}")
crucible_neg_compile_test(neg_sriov_zero_vfs
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<InRange<1, 4096>\\(\\), short unsigned int>\\(0\\)")
crucible_neg_compile_test(neg_sriov_multicast_mac
    "__builtin_trap\\(\\).* is not a constant expression"
    "fixy::mint_refined<crucible::cog::sriov::IsAssignableVfMac\\(\\), crucible::cog::sriov::MacAddress>\\(crucible::cog::sriov::MacAddress\\{std::array<unsigned char, 6>.*\\{1, 0, 0, 0, 0, 1\\}")
crucible_neg_compile_test(neg_sriov_mint_bg_ctx
    "no matching function for call to .mint_sriov_plan\\(fixy::BgDrainCtx"
    "required for the satisfaction of .CtxFitsSrIovMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_sriov_mint_hot_fg_ctx
    "no matching function for call to .mint_sriov_plan\\(const fixy::HotFgCtx&"
    "required for the satisfaction of .CtxFitsSrIovMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_sriov_vf_handle_forged
    ".constexpr crucible::cog::sriov::VfHandle::VfHandle\\(crucible::cog::CogIdentity, crucible::cog::sriov::VfIndex\\). is private within this context"
    "cog/SrIov.h:[0-9]+:[0-9]+: note: declared private here")

# cntp/Pacing.h fq/qdisc verification + SO_MAX_PACING_RATE.
#   * neg_pacing_bbr_pfifo_config: BBR pacing config admits only fq/fq_codel.
#   * neg_pacing_raw_rate: socket pacing requires admitted positive rate.
#   * neg_pacing_raw_config_ensure: fq verification requires tagged qdisc config.
crucible_neg_compile_test(neg_pacing_bbr_pfifo_config
  "no matching function for call to .mint_bbr_qdisc_config<crucible::cntp::Qdisc::Pfifo>\\(crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .BbrCompatibleQdisc<Required>. .with Required = crucible::cntp::Qdisc::Pfifo.")
crucible_neg_compile_test(neg_pacing_raw_rate
  "no matching function for call to .set_socket_pacing_rate\\(fixy::TestRunnerCtx&, fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>&, long long unsigned int\\)."
  "cannot convert .1000000. \\(type .long long unsigned int.\\) to type .crucible::cntp::PositivePacingRate.")
crucible_neg_compile_test(neg_pacing_raw_config_ensure
  "no matching function for call to .ensure_fq_active\\(fixy::TestRunnerCtx&, crucible::cntp::QdiscConfig&\\)."
  "cannot convert .raw. \\(type .crucible::cntp::QdiscConfig.\\) to type .crucible::cntp::DeclaredQdiscConfig.")

# cntp/PathSwap.h: the application-level move of a flow to a new path.
#   * neg_path_swap_raw_plan_begin: a swap starts only from a tagged plan.
#   * neg_path_swap_plan_raw_path_id: the plan mint takes admitted path ids.
#   * neg_path_swap_plan_direct: the plan mint is the only door to a plan.
#   * neg_path_swap_mint_bg_ctx: the swapper mint needs a context that owns Init.
#   * neg_path_swapper_direct: the swapper mint is the only door to a swapper.
#   * neg_path_swap_commit_fg_ctx, neg_path_swap_begin_init_ctx: a transition
#     needs a context that owns Bg.
#   * neg_path_swap_begin_drain_ctx: a transition waits on the gate of the
#     swapper, so it needs a context that owns Block.
#   * neg_path_swap_plan_read_hot_ctx: a read of the plan waits on the same
#     gate, so it needs a context that owns Block.
#   * neg_path_swap_commit_non_session_resource,
#     neg_path_swap_commit_pointer_resource: the new resource is a
#     SessionResource.
crucible_neg_compile_test(neg_path_swap_raw_plan_begin
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::begin_swap.fixy::BgLoadCtx&, crucible::cntp::PathSwapPlan&, int.."
  "cannot convert .raw. .type .crucible::cntp::PathSwapPlan.. to type .const crucible::cntp::DeclaredPathSwapPlan&.")
crucible_neg_compile_test(neg_path_swap_plan_raw_path_id
  "could not convert .0. from .uint64_t."
  "to .crucible::cntp::PositivePathId. .aka .fixy::Refinement<fixy::IsPositive.., long unsigned int, false>..")
crucible_neg_compile_test(neg_path_swap_plan_direct
  ".constexpr crucible::cntp::PathSwapPlan::PathSwapPlan.crucible::cntp::PositivePathId, crucible::cntp::PositivePathId, crucible::cntp::PositivePathId, crucible::cntp::PositiveNanoseconds.. is private within this context"
  "PathSwap.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_path_swap_mint_bg_ctx
  "no matching function for call to .mint_path_swapper.fixy::BgDrainCtx&.."
  "required for the satisfaction of .CtxFitsPathSwapMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_path_swapper_direct
  ".constexpr crucible::cntp::PathSwapper<MaxEvents>::PathSwapper.. .with long unsigned int MaxEvents = 8.. is private within this context"
  "PathSwap.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_path_swap_commit_fg_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::commit_sender.fixy::HotFgCtx&"
  "required for the satisfaction of .CtxFitsPathSwapTransition<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_path_swap_begin_init_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::begin_swap.fixy::ColdInitCtx&"
  "required for the satisfaction of .CtxFitsPathSwapTransition<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >.")
crucible_neg_compile_test(neg_path_swap_begin_drain_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::begin_swap.fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxMayBlock<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_path_swap_plan_read_hot_ctx
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::plan.fixy::HotFgCtx&."
  "required for the satisfaction of .CtxFitsPathSwapRead<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_path_swap_commit_non_session_resource
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::commit_sender.fixy::BgLoadCtx&"
  "required for the satisfaction of .PathSwapSessionResource<NewResource>. .with NewResource = Wire&.")
crucible_neg_compile_test(neg_path_swap_commit_pointer_resource
  "no matching function for call to .crucible::cntp::PathSwapper<[^>]*>::commit_sender.fixy::BgLoadCtx&"
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
crucible_neg_compile_test(neg_congestion_telemetry_raw_fd_harvest
  "no matching function for call to .harvest_socket\\(fixy::TestRunnerCtx&, int\\)."
  "cannot convert .3. \\(type .int.\\) to type .crucible::cntp::SocketFd. \\{aka .fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>.\\}")
crucible_neg_compile_test(neg_congestion_telemetry_raw_state_aggregate
  "could not convert .std::span<crucible::topology::CongestionSample, 1>\\(samples\\)."
  "to .span<const fixy::Tagged<crucible::topology::CongestionSample, fixy::tags::source::TcpInfo>,")
crucible_neg_compile_test(neg_congestion_telemetry_worker_fg_record
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::record_link\\(const foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >&"
  "required for the satisfaction of .CtxFitsCongestionTelemetryHarvest<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_congestion_telemetry_worker_poll_bg_drain_ctx
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::poll_link\\(fixy::BgDrainCtx&, "
  "required for the satisfaction of .CtxFitsSocketOption<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]"
  "required for the satisfaction of .CtxFitsCongestionTelemetryPoll<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_test(neg_congestion_telemetry_worker_poll_init_load_ctx
  "no matching function for call to .crucible::topology::CongestionTelemetryWorker<1, 1>::poll_link\\(fixy::InitLoadCtx&, "
  "required for the satisfaction of .CtxFitsCongestionTelemetryHarvest<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, "
  "required for the satisfaction of .CtxFitsCongestionTelemetryPoll<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, ")
crucible_neg_compile_test(neg_congestion_telemetry_worker_mint_bg_ctx
  "no matching function for call to .mint_congestion_telemetry_worker<1, 1>\\(fixy::BgDrainCtx&\\)"
  "required for the satisfaction of .CtxFitsCongestionTelemetryStart<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_congestion_telemetry_worker_mint_non_ctx
  "no matching function for call to .mint_congestion_telemetry_worker<1, 1>\\(int\\)"
  "required for the satisfaction of .IsExecCtx<Ctx>. \\[with Ctx = int\\]")
crucible_neg_compile_test(neg_congestion_telemetry_worker_direct_construct
  "is private within this context"
  "CongestionTelemetryWorker<MaxLinks, MaxFlows>::CongestionTelemetryWorker\\(\\) \\[with long unsigned int MaxLinks = 1; long unsigned int MaxFlows = 1\\]")

# cntp/IncastControl.h incast mitigation substrate.
#   * neg_incast_raw_fd_configure: socket tuning requires admitted SocketFd.
#   * neg_incast_raw_config_configure: socket tuning requires tagged config.
#   * neg_incast_credit_fg_ctx: credit mutation is Bg-row work.
crucible_neg_compile_test(neg_incast_raw_fd_configure
  "no matching function for call to .crucible::cntp::IncastController<1>::configure_socket\\(fixy::InitLoadCtx&, int,"
  "cannot convert .3. \\(type .int.\\) to type .crucible::cntp::SocketFd. .aka .fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>.")
crucible_neg_compile_test(neg_incast_raw_config_configure
  "no matching function for call to .crucible::cntp::IncastController<1>::configure_socket\\(fixy::InitLoadCtx&, fixy::Refinement<fixy::IsNonNegative\\(\\), int, false>&, crucible::cntp::IncastConfig\\)."
  "\\(type .crucible::cntp::IncastConfig.\\) to type .crucible::cntp::DeclaredIncastConfig.")
crucible_neg_compile_test(neg_incast_credit_fg_ctx
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
crucible_neg_compile_test(neg_roce_zero_pfc_mask
  "no matching function for call to .mint_roce_config<0, 26>\\(crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .ValidPfcPriorityMask<PfcPriorities>. \\[with PfcPriorities = 0\\]")
crucible_neg_compile_test(neg_roce_dscp_above_max
  "no matching function for call to .mint_roce_config<8, 64>\\(crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .ValidRoceDscp<Dscp>. \\[with Dscp = 64\\]")
crucible_neg_compile_test(neg_roce_raw_config_apply
  "invalid initialization of reference of type .const crucible::cntp::DeclaredRoceConfig&. .* from expression of type .crucible::cntp::RoceConfig."
  "in passing argument 1 of .constexpr std::expected<void, crucible::cntp::RoceError> crucible::cntp::apply_roce_config\\(const DeclaredRoceConfig&\\).")
crucible_neg_compile_test(neg_roce_config_wrong_source
  "invalid initialization of reference of type .const crucible::cntp::DeclaredRoceConfig&. .* from expression of type .fixy::Tagged<crucible::cntp::RoceConfig, fixy::tags::source::External>."
  "in passing argument 1 of .constexpr std::expected<void, crucible::cntp::RoceError> crucible::cntp::validate_roce_config\\(const DeclaredRoceConfig&\\).")
crucible_neg_compile_test(neg_roce_pause_counters_without_block
  "no matching function for call to .query_pfc_pause_counters\\(fixy::ColdInitCtx&, crucible::cntp::NicInterfaceName&\\)."
  "required for the satisfaction of .CtxFitsFileMint<Ctx, fixy::atom::fs::mode<fixy::fs::open_mode::ReadOnly> >. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_test(neg_roce_pause_counters_hot_ctx
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
#   * neg_gpu_direct_register_is_a_stub: no driver registers a region, and
#     each call of the registration sees its CRUCIBLE_STUB deprecation.
crucible_neg_compile_test(neg_gpu_direct_null_address
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_test(neg_gpu_direct_zero_bytes
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), long unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_test(neg_gpu_direct_raw_plan_register
  "could not convert .plan. from .const crucible::cntp::_wip::gpu_direct::GpuDirectMrPlan. to .crucible::cntp::_wip::gpu_direct::DeclaredGpuDirectMrPlan."
  "aka .fixy::Tagged<crucible::cntp::_wip::gpu_direct::GpuDirectMrPlan, crucible::cntp::_wip::gpu_direct::wip_source::GpuDirect>.")
crucible_neg_compile_test(neg_gpu_direct_mint_bg_ctx
  "no matching function for call to .mint_gpu_direct_mr_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_gpu_direct_mint_hot_ctx
  "no matching function for call to .mint_gpu_direct_mr_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_gpu_direct_storage_bg_ctx
  "no matching function for call to .mint_gpu_direct_storage_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_gpu_direct_storage_hot_ctx
  "no matching function for call to .mint_gpu_direct_storage_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps&, "
  "required for the satisfaction of .CtxFitsGpuDirectMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_gpu_direct_register_is_a_stub
  "crucible::cntp::_wip::gpu_direct::register_gpu_memory\\(DeclaredGpuDirectMrPlan\\). is deprecated: CRUCIBLE_STUB: no driver registers the region"
  "\\[-Werror=deprecated-declarations\\]")

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
#   * neg_sharp_dispatch_is_a_stub: no switch reduces, and each call of the
#     free dispatch sees its CRUCIBLE_STUB deprecation.
crucible_neg_compile_test(neg_sharp_zero_participants
  "in .constexpr. expansion of .fixy::mint_refined<IsPositive\\(\\), short unsigned int>\\(0\\)."
  "__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_test(neg_sharp_raw_plan_dispatch
  "could not convert .plan. from .const crucible::cntp::_wip::sharp::SharpFabricPlan. to .crucible::cntp::_wip::sharp::DeclaredSharpFabricPlan."
  "aka .fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>.")
crucible_neg_compile_test(neg_sharp_strict_recipe_concept
  "no matching function for call to .sharp_recipe_laws<StrictRecipe>\\(\\)."
  "the expression .\\(Recipe::determinism\\) != crucible::ReductionDeterminism::BITEXACT_STRICT \\[with Recipe = StrictRecipe\\]. evaluated to .false.")
crucible_neg_compile_test(neg_sharp_non_commutative_recipe
  "no matching function for call to .sharp_recipe_laws<NonCommutativeRecipe>\\(\\)."
  "the expression .Recipe::commutative \\[with Recipe = NonCommutativeRecipe\\]. evaluated to .false.")
crucible_neg_compile_test(neg_sharp_mint_bg_ctx
  "no matching function for call to .mint_sharp_fabric_plan\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, "
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_sharp_mint_hot_ctx
  "no matching function for call to .mint_sharp_fabric_plan\\(const fixy::HotFgCtx&, crucible::cog::CogIdentity&, crucible::cog::NvSwitchTargetCaps&, "
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_sharp_context_bg_ctx
  "no matching function for call to .mint_sharp_context\\(fixy::BgDrainCtx&, fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>&\\)."
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_sharp_context_hot_ctx
  "no matching function for call to .mint_sharp_context\\(const fixy::HotFgCtx&, fixy::Tagged<crucible::cntp::_wip::sharp::SharpFabricPlan, crucible::cntp::_wip::sharp::wip_source::Sharp>&\\)."
  "required for the satisfaction of .CtxFitsSharpMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_sharp_dispatch_init_ctx
  "no matching function for call to .crucible::cntp::_wip::sharp::SharpReducer::allreduce_via_sharp\\(fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsSharpDispatch<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >\\]")
crucible_neg_compile_test(neg_sharp_dispatch_hot_ctx
  "no matching function for call to .crucible::cntp::_wip::sharp::SharpReducer::allreduce_via_sharp\\(const fixy::HotFgCtx&, "
  "required for the satisfaction of .CtxFitsSharpDispatch<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_sharp_dispatch_is_a_stub
  "crucible::cntp::_wip::sharp::dispatch_sharp_allreduce\\(std::span<const float>, std::span<float>, const crucible::NumericalRecipe&, SharpRecipeLaws, DeclaredSharpFabricPlan\\). is deprecated: CRUCIBLE_STUB: no switch reduces"
  "\\[-Werror=deprecated-declarations\\]")

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
crucible_neg_compile_test(neg_tcam_zero_entry_count
  "in .constexpr. expansion of .fixy::mint_refined<InRange<1, 65536>\\(\\), unsigned int>\\(0\\)."
  ".__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_test(neg_tcam_static_table_zero
  "template constraint failure for .template<unsigned int MaxRules>  requires  TcamTableShape<MaxRules> class crucible::cntp::tcam::TcamRules."
  "required for the satisfaction of .TcamTableShape<MaxRules>. .with MaxRules = 0.")
crucible_neg_compile_test(neg_tcam_table_plan_default
  "no matching function for call to .fixy::Tagged<crucible::cntp::tcam::TcamTablePlan, fixy::tags::source::TcamTable>::Tagged\\(<brace-enclosed initializer list>\\)."
  "required for the satisfaction of .default_initializable<T>. .with T = crucible::cntp::tcam::TcamTablePlan."
  "no matching function for call to .crucible::cntp::tcam::TcamTablePlan::TcamTablePlan\\(\\).")
crucible_neg_compile_test(neg_tcam_raw_rule_add
  "cannot convert .crucible::cntp::tcam::TcamFlowRule. to .crucible::cntp::tcam::DeclaredTcamFlowRule."
  "initializing argument 1 of .constexpr std::expected<fixy::Qtt<foundation::algebra::lattices::QttGrade::One, crucible::cntp::tcam::TcamRuleHandle>, crucible::cntp::tcam::TcamError> crucible::cntp::tcam::TcamRules<MaxRules>::add_rule\\(")
crucible_neg_compile_test(neg_tcam_rule_handle_copy
  "use of deleted function .fixy::Qtt<Grade, T>::Qtt\\(const fixy::Qtt<Grade, T>&\\) .with auto Grade = foundation::algebra::lattices::QttGrade::One. T = crucible::cntp::tcam::TcamRuleHandle.."
  "Linear<T> and Affine<T> are move-only. use std::move or drop\\(\\)")
crucible_neg_compile_test(neg_tcam_rule_handle_forge
  "error: .constexpr crucible::cntp::tcam::TcamRuleHandle::TcamRuleHandle\\(crucible::cog::Uuid, crucible::cntp::tcam::TcamRuleId, uint32_t, uint32_t\\). is private within this context"
  "include/crucible/cntp/Tcam.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_tcam_rule_handle_mint_linear
  "no matching function for call to .mint_linear<crucible::cntp::tcam::TcamRuleHandle>\\(crucible::cog::Uuid, fixy::Refinement<fixy::IsNonZero\\(\\), long unsigned int, false>, uint32_t, uint32_t\\)."
  ".crucible::cntp::tcam::TcamRuleHandle. is not constructible from .crucible::cog::Uuid, fixy::Refinement<fixy::IsNonZero\\(\\), long unsigned int, false>, unsigned int, unsigned int., because"
  "TcamRuleHandle::TcamRuleHandle\\(crucible::cog::Uuid, crucible::cntp::tcam::TcamRuleId, uint32_t, uint32_t\\). is private within this context")
crucible_neg_compile_test(neg_tcam_mint_bg_ctx
  "no matching function for call to .mint_tcam_table\\(fixy::BgDrainCtx&, crucible::cog::CogIdentity&, crucible::cog::NicPortTargetCaps&, "
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, "
  "required for the satisfaction of .CtxFitsTcamMint<Ctx, Caps>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, ")
crucible_neg_compile_test(neg_tcam_mint_gpu_caps
  "no matching function for call to .mint_tcam_table\\(fixy::ColdInitCtx&, crucible::cog::CogIdentity&, crucible::cog::GpuTargetCaps, "
  "required for the satisfaction of .TcamTargetCaps<Caps>. .with Caps = crucible::cog::GpuTargetCaps."
  "required for the satisfaction of .CtxFitsTcamMint<Ctx, Caps>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, ")

# cntp/MtlsTransport.h mutual-TLS federation policy substrate.
#   * neg_mtls_tls12_config: TLS 1.2 cannot mint a declared mTLS config.
#   * neg_mtls_legacy_cipher_config: RSA/3DES cannot enter the cipher set.
#   * neg_mtls_raw_config_connect: runtime connect requires tagged policy.
crucible_neg_compile_test(neg_mtls_tls12_config
  "no matching function for call to .mint_mtls_config<crucible::cntp::TlsVersion::V12>"
  "required for the satisfaction of .SupportedMtlsVersion<MinVersion>. .with MinVersion = crucible::cntp::TlsVersion::V12.")
crucible_neg_compile_test(neg_mtls_legacy_cipher_config
  "no matching function for call to .mint_mtls_config<crucible::cntp::TlsVersion::V13, crucible::cntp::MtlsCipherSuite::LegacyRsa3desSha,"
  "required for the satisfaction of .ApprovedMtlsCipherSuite<Primary>. .with Primary = crucible::cntp::MtlsCipherSuite::LegacyRsa3desSha.")
crucible_neg_compile_test(neg_mtls_raw_config_connect
  "invalid initialization of reference of type .const crucible::cntp::DeclaredMtlsConfig&."
  ".aka .const fixy::Tagged<crucible::cntp::MtlsConfig, fixy::tags::source::Mtls>&.. from expression of type .crucible::cntp::MtlsConfig.")

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
crucible_neg_compile_test(neg_backpressure_raw_admission_decision
  "conversion from .const crucible::cntp::AdmissionDecision. to non-scalar type .crucible::cntp::DeclaredAdmissionDecision."
  "fixy::Tagged<crucible::cntp::AdmissionDecision, fixy::tags::source::AdmissionDecision>")
crucible_neg_compile_test(neg_backpressure_invalid_resource_kind
  "no matching function for call to .mint_resource_pressure<bogus>\\(int\\)."
  "required for the satisfaction of .IsResourceKind<Kind>. \\[with Kind = \\(foundation::effects::ResourceKind\\)255\\]")
crucible_neg_compile_test(neg_backpressure_raw_pressure_kind
  "crucible::cntp::ResourcePressure::ResourcePressure\\(foundation::effects::ResourceKind, crucible::cntp::ResourcePressurePpm\\). is private within this context"
  "declared private here")
crucible_neg_compile_test(neg_backpressure_hot_ctx_credit
  "no matching function for call to .crucible::cntp::CreditFlowControl<1>::start_flow\\(foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "required for the satisfaction of .CtxFitsBackpressureRuntime<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >\\]")
crucible_neg_compile_test(neg_backpressure_start_without_block
  "no matching function for call to .crucible::cntp::CreditFlowControl<1>::start_flow\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxMayBlock<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >\\]")
crucible_neg_compile_test(neg_backpressure_controller_bg_mint
  "no matching function for call to .mint_admission_controller<2, 2>\\(fixy::BgLoadCtx&\\)."
  "required for the satisfaction of .CtxFitsBackpressureMint<Ctx>. \\[with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >\\]")
crucible_neg_compile_test(neg_backpressure_controller_hot_accept
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
crucible_neg_compile_test(neg_af_xdp_raw_config
  "no matching function for call to .mint_af_xdp_socket<131072, 2048"
  "cannot convert .raw. .type .crucible::cntp::AfXdpConfig.. to type .crucible::cntp::DeclaredAfXdpConfig.")
crucible_neg_compile_test(neg_af_xdp_non_power_of_two_frame
  "no matching function for call to .mint_af_xdp_socket<131072, 1500"
  "template constraint failure for .template<.*requires  AfXdpStaticShape<UmemBytes, FrameSize")
crucible_neg_compile_test(neg_af_xdp_cross_source_packet
  "cannot convert .Borrowed<.*,OtherOwner,.*>. to .Borrowed<.*,crucible::cntp::AfXdpSocket<131072, 2048"
  "initializing argument 1 of .*enqueue_tx.packet_view.")
crucible_neg_compile_test(neg_af_xdp_unsanitized_rx
  "could not convert .untrusted. from .Tagged<.*,fixy::tags::source::External>."
  "to .Tagged<.*,fixy::tags::source::Sanitized>.")
crucible_neg_compile_test(neg_af_xdp_config_raw_frame_size
  "could not convert .2048. from .uint32_t."
  "to .crucible::cntp::AfXdpFrameSize.")
crucible_neg_compile_test(neg_af_xdp_config_weaker_refinement
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
crucible_neg_compile_test(neg_connection_pool_raw_event
  "could not convert .raw. from .crucible::cntp::PoolEvent. to .crucible::cntp::DeclaredPoolEvent. .aka .fixy::Tagged<crucible::cntp::PoolEvent, fixy::tags::source::ConnectionPool>.."
  "Tagged.h:[0-9]+:[0-9]+: note: explicit conversion function was not considered")
crucible_neg_compile_test(neg_connection_pool_invalid_transport
  "no matching function for call to .mint_connection<.crucible::cntp::TransportClass.255>"
  "required for the satisfaction of .PoolTransportClass<T>. .with T = .crucible::cntp::TransportClass.255.")
crucible_neg_compile_test(neg_connection_direct
  "no matching function for call to .mint_linear<crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp> >"
  ".constexpr crucible::cntp::Connection<T>::Connection.crucible::cntp::SocketFd, crucible::cog::Uuid, crucible::cntp::PositiveConnectionId. .with crucible::cntp::TransportClass T = crucible::cntp::TransportClass::Tcp.* is private within this context")
crucible_neg_compile_test(neg_connection_pool_mint_bg_ctx
  "no matching function for call to .mint_connection_pool<crucible::cntp::TransportClass::Tcp, 1, 1>.fixy::BgLoadCtx&.."
  "required for the satisfaction of .CtxFitsConnectionPoolMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >.")
crucible_neg_compile_test(neg_connection_pool_direct
  ".constexpr crucible::cntp::ConnectionPool<T, MaxRemotes, MaxPerRemote, MaxEvents>::ConnectionPool.crucible::cntp::PoolConfig. .with crucible::cntp::TransportClass T = crucible::cntp::TransportClass::Tcp.* is private within this context"
  "ConnectionPoolRuntime.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_connection_pool_hot_ctx_lease
  "no matching function for call to .crucible::cntp::ConnectionPool<crucible::cntp::TransportClass::Tcp, 1, 1[^>]*>::lease.fixy::HotFgCtx&"
  "required for the satisfaction of .CtxFitsConnectionPoolRuntime<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >.")
crucible_neg_compile_test(neg_connection_pool_drain_ctx_lease
  "no matching function for call to .crucible::cntp::ConnectionPool<crucible::cntp::TransportClass::Tcp, 1, 1[^>]*>::lease.fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxMayBlock<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >.")
crucible_neg_compile_test(neg_connection_pool_lease_rewrite
  "passing .const crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>. as .this. argument discards qualifiers"
  "in call to .constexpr crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>& crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>::operator=.const crucible::cntp::Connection<crucible::cntp::TransportClass::Tcp>&..")

# cntp/Integrity.h end-to-end xxHash64 verification.
#   * neg_integrity_raw_payload_as_verified: raw bytes cannot satisfy the
#     IntegrityVerified provenance lane.
#   * neg_integrity_cross_tag_verified: External is not IntegrityVerified.
#   * neg_integrity_raw_hash_field: hash field requires refined non-zero
#     IntegrityHash admission.
#   * neg_integrity_non_contiguous_wrap: payloads must be byte-contiguous.
crucible_neg_compile_test(neg_integrity_raw_payload_as_verified
  "could not convert .payload. from .Payload. .aka .std::array<std::byte, 8>.."
  "to .crucible::cntp::IntegrityVerifiedPayload<std::array<std::byte, 8> >.")
crucible_neg_compile_test(neg_integrity_cross_tag_verified
  "could not convert .std::move<fixy::Tagged<.*fixy::tags::source::External>&>\\(external\\). from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::IntegrityVerified>.")
crucible_neg_compile_test(neg_integrity_raw_hash_field
  "could not convert .1. from .int. to .crucible::cntp::IntegrityHash."
  ".aka .fixy::Refinement<fixy::IsNonZero\\(\\), long unsigned int, false>.")
crucible_neg_compile_test(neg_integrity_non_contiguous_wrap
  "no matching function for call to .wrap\\(NotWireBytes\\)."
  "required for the satisfaction of .HasContiguousPayloadData<Payload>. .with Payload = NotWireBytes.")

# cntp/GossipMulticast.h XDP_TX multicast plan substrate.
#   * neg_gossip_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_gossip_multicast_raw_plan: XDP program extraction requires
#     source::GossipMulticast provenance.
#   * neg_gossip_multicast_raw_topic: raw topic keys cannot register
#     neighbors; callers must pass DeclaredGossipTopic.
#   * neg_gossip_multicast_zero_neighbors: zero-neighbor plans are rejected
#     at the shape concept.
crucible_neg_compile_test(neg_gossip_multicast_mint_bg_ctx
  "no matching function for call to .mint_gossip_multicast_plan<4, 4>\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxFitsGossipMulticastMint<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg,")
crucible_neg_compile_test(neg_gossip_multicast_raw_plan
  "could not convert .raw. from .crucible::cntp::GossipMulticastSpec. to .crucible::cntp::DeclaredGossipMulticastPlan."
  "fixy::Tagged<crucible::cntp::GossipMulticastSpec, fixy::tags::source::GossipMulticast>")
crucible_neg_compile_test(neg_gossip_multicast_raw_topic
  "cannot convert .crucible::cntp::GossipTopicKey. to .crucible::cntp::DeclaredGossipTopic."
  "fixy::Tagged<crucible::cntp::GossipTopicKey, fixy::tags::source::GossipMulticast>")
crucible_neg_compile_test(neg_gossip_multicast_zero_neighbors
  "no matching function for call to .mint_gossip_multicast_plan<4, 0>"
  "required for the satisfaction of .GossipNeighborShape<MaxNeighbors>. .with MaxNeighbors = 0.")

# cntp/OverlayMulticast.h application-layer route substrate.
#   * neg_overlay_multicast_mint_bg_ctx: only Init-row contexts mint plans.
#   * neg_overlay_multicast_raw_peer: raw peer refs cannot seed a plan.
#   * neg_overlay_multicast_zero_stripes: zero-stripe route tables rejected.
#   * neg_overlay_multicast_cross_tag_peer: externally tagged peer refs cannot
#     seed an overlay route plan.
#   * neg_overlay_multicast_private_ctor: the mint is the one door to a plan.
crucible_neg_compile_test(neg_overlay_multicast_mint_bg_ctx
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::BgDrainCtx&"
  "required for the satisfaction of .CtxOwnsCapability<Ctx, foundation::effects::Effect::Init>.")
crucible_neg_compile_test(neg_overlay_multicast_raw_peer
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::ColdInitCtx&, crucible::cntp::OverlayPeerRef&\\)"
  "cannot convert .raw. \\(type .crucible::cntp::OverlayPeerRef.\\) to type .crucible::cntp::DeclaredOverlayPeer.")
crucible_neg_compile_test(neg_overlay_multicast_zero_stripes
  "no matching function for call to .mint_overlay_multicast<4, 0, 2>\\(fixy::ColdInitCtx&"
  "required for the satisfaction of .OverlayMulticastShape<MaxPeers, MaxStripes, MaxFanout>. .with MaxPeers = 4. MaxStripes = 0. MaxFanout = 2.")
crucible_neg_compile_test(neg_overlay_multicast_cross_tag_peer
  "no matching function for call to .mint_overlay_multicast<4, 4, 2>\\(fixy::ColdInitCtx&, fixy::Tagged<crucible::cntp::OverlayPeerRef, fixy::tags::source::External>&\\)"
  "cannot convert .external. \\(type .fixy::Tagged<crucible::cntp::OverlayPeerRef, fixy::tags::source::External>.\\) to type .crucible::cntp::DeclaredOverlayPeer.")
crucible_neg_compile_test(neg_overlay_multicast_private_ctor
  "OverlayMulticastPlan<MaxPeers, MaxStripes, MaxFanout>::OverlayMulticastPlan\\(crucible::cntp::DeclaredOverlayPeer"
  "is private within this context")

# cntp/Fec.h GF(256) systematic Reed-Solomon primitive.
#   * neg_fec_zero_k / neg_fec_zero_m: shard counts must be positive.
#   * neg_fec_shape_over_256: GF(2^8) limits K+M to 256 coordinates.
#   * neg_fec_mint_test_context: the mint is Init-only.
#   * neg_fec_encode_non_contiguous_owned: owned FEC input must be a
#     Linear byte-contiguous buffer.
crucible_neg_compile_test(neg_fec_zero_k
  "template constraint failure for .template<unsigned char K, unsigned char M>  requires  ReedSolomonShape<K, M> class crucible::cntp::ReedSolomon."
  "the expression .K > 0 .with K = 0.. evaluated to .false.")
crucible_neg_compile_test(neg_fec_zero_m
  "template constraint failure for .template<unsigned char K, unsigned char M>  requires  ReedSolomonShape<K, M> class crucible::cntp::ReedSolomon."
  "the expression .M > 0 .with M = 0.. evaluated to .false.")
crucible_neg_compile_test(neg_fec_shape_over_256
  "template constraint failure for .template<unsigned char K, unsigned char M>  requires  ReedSolomonShape<K, M> class crucible::cntp::ReedSolomon."
  "the expression .static_cast<unsigned int>\\(K\\) \\+ static_cast<unsigned int>\\(M\\) <= 256 .with K = 255. M = 2.. evaluated to .false.")
crucible_neg_compile_test(neg_fec_mint_test_context
  "could not convert|no matching function|cannot convert"
    "effects::Test|effects::Init")
crucible_neg_compile_test(neg_fec_encode_non_contiguous_owned
  "no matching function for call to .crucible::cntp::ReedSolomon<4, 2>::encode_owned"
  "required for the satisfaction of .ByteContiguousBuffer<Input>. .with Input = NotWireBytes.")

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
crucible_neg_compile_test(neg_fountain_zero_symbols
  "template constraint failure for .template<long unsigned int SourceSymbols, long unsigned int SymbolBytes, class Algorithm>  requires \\(FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>\\) && \\(FountainAlgorithm<Algorithm>\\) class crucible::cntp::FountainEncoder."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>. .with SourceSymbols = 0. SymbolBytes = 16.")
crucible_neg_compile_test(neg_fountain_zero_symbol_bytes
  "template constraint failure for .template<long unsigned int SourceSymbols, long unsigned int SymbolBytes, class Algorithm>  requires \\(FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>\\) && \\(FountainAlgorithm<Algorithm>\\) class crucible::cntp::FountainEncoder."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, SourceSymbols>. .with SourceSymbols = 4. SymbolBytes = 0.")
crucible_neg_compile_test(neg_fountain_mint_test_context
  "no matching function for call to .mint_fountain_encoder<4, 16>\\(fixy::TestRunnerCtx&\\)."
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, "
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>. ")
crucible_neg_compile_test(neg_fountain_encoder_mint_zero_symbols
  "no matching function for call to .mint_fountain_encoder<0, 16>\\(fixy::ColdInitCtx&\\)."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, MaxEquations>. .with SourceSymbols = 0. SymbolBytes = 16. MaxEquations = 0."
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, SourceSymbols, Algorithm>. ")
crucible_neg_compile_test(neg_fountain_decoder_mint_bg_ctx
  "no matching function for call to .mint_fountain_decoder<8, 16>\\(fixy::BgDrainCtx&\\)."
  "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Init> >. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, "
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>. ")
crucible_neg_compile_test(neg_fountain_decoder_mint_short_equations
  "no matching function for call to .mint_fountain_decoder<8, 16, 7>\\(fixy::ColdInitCtx&\\)."
  "required for the satisfaction of .FountainShape<SourceSymbols, SymbolBytes, MaxEquations>. .with SourceSymbols = 8. SymbolBytes = 16. MaxEquations = 7."
  "required for the satisfaction of .CtxFitsFountainMint<Ctx, SourceSymbols, SymbolBytes, MaxEquations, Algorithm>. ")
crucible_neg_compile_test(neg_fountain_raw_seed
  "cannot convert .long long unsigned int. to .crucible::cntp::FountainSeed."
  "initializing argument 2 of .std::expected<void, crucible::cntp::FountainError> crucible::cntp::FountainEncoder<SourceSymbols, SymbolBytes, Algorithm>::start_encoding\\(std::span<const std::byte>, crucible::cntp::FountainSeed\\)")
crucible_neg_compile_test(neg_fountain_encode_non_contiguous_owned
  "no matching function for call to .crucible::cntp::FountainEncoder<4, 16, crucible::cntp::LtFountain>::encode_owned\\("
  "required for the satisfaction of .ByteContiguousBuffer<Input>. .with Input = NotWireBytes.")

# compute_storage_nbytes* accepts
# ExternalTensorMeta = Tagged<const TensorMeta&, source::External>.
# Two HS14 fixtures cover the provenance gate's two directions:
#   * neg_storage_nbytes_raw_tensor_meta — raw TensorMeta cannot bind
#     implicitly to the explicit Tagged boundary.
#   * neg_storage_nbytes_cross_tag — Tagged<const TensorMeta&,
#     source::Sanitized> cannot pass as source::External.
crucible_neg_compile_test(neg_storage_nbytes_raw_tensor_meta
  "could not convert .meta. from .crucible::TensorMeta. to .crucible::ExternalTensorMeta."
  ".aka .fixy::Tagged<const crucible::TensorMeta&, fixy::tags::source::External>.")
crucible_neg_compile_test(neg_storage_nbytes_cross_tag
  "could not convert .sanitized. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Sanitized>."
  "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>.")

# The deterministic storage-byte
# projections return DetSafe<Pure, Saturated<uint64_t>>.  Raw
# Saturated slots and differently pinned DetSafe slots cannot consume
# the result without an explicit unwrap / retag decision.
crucible_neg_compile_test(neg_storage_nbytes_det_raw_saturated
  "conversion from .fixy::DetSafe<foundation::algebra::lattices::DetSafeTier::Pure, fixy::Saturated<long unsigned int> >."
  "to non-scalar type .fixy::Saturated<long unsigned int>. requested")
crucible_neg_compile_test(neg_storage_nbytes_det_cross_tier
  "conversion from .Graded<\\[\\.\\.\\.\\],At<foundation::algebra::lattices::DetSafeTier::Pure>,\\[\\.\\.\\.\\]>."
  "to non-scalar type .Graded<\\[\\.\\.\\.\\],At<foundation::algebra::lattices::DetSafeTier::MonotonicClockRead>,\\[\\.\\.\\.\\]>. requested")

# ─── Non-zero head invariant ──────────────────────────────────────
#
# ValidCipherHead is fixy::Refined<fixy::non_zero, ContentHash>.
# ContentHash{0} is the structural sentinel for "no commit yet".  A head
# at zero corrupts the log invariant (a step with hash 0 looks the same
# as "before the first commit"), breaks the binary search of
# hash_at_step(), and loses the federation round-trip key for that step.
#
# Two fixtures (HS14) build a ValidCipherHead from a zero hash, and
# mint_refined refuses each one at compile time:
#   * neg_cipher_head_zero_literal: the explicit literal
#     ContentHash{uint64_t{0}}
#   * neg_cipher_head_default_construct: a default-constructed
#     ContentHash{}, which a caller gets when it does not set a local.
#
# The two failure modes have the same bytes (raw == 0) and different
# causes in the source, so each one has its fixture.
#
# The precondition of Cipher::record_event checks the same invariant at
# run time.  A caller that passes a bare ContentHash, and not a
# ValidCipherHead, meets that check.
crucible_neg_compile_test(neg_cipher_head_zero_literal
  "__builtin_trap... is not a constant expression"
  "in .constexpr. expansion of .fixy::mint_refined<IsNonZero.., crucible::ContentHash>.crucible::ContentHash.0..")
crucible_neg_compile_test(neg_cipher_head_default_construct
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
# Defense-in-depth: the runtime precondition is_non_zero(merkle_root)
# inside Tx::commit is retained — ValidMerkleRoot catches the value
# at the call site at compile-time via the typed alias path; the
# precondition catches it at runtime if a caller bypasses the typed
# path.
crucible_neg_compile_test(neg_merkle_root_zero_literal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::MerkleHash>\\(crucible::MerkleHash\\(0\\)\\)")
crucible_neg_compile_test(neg_merkle_root_default_construct
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
# Defense-in-depth: each accessor's CRUCIBLE_PRE(decide::is_non_zero(...))
# runtime gate is retained.  ValidContentHash catches the value at
# the construction call; the precondition catches it at the accessor
# body's entry.  Both layers must reject for the type-level
# invariant and the runtime path to disagree.
crucible_neg_compile_test(neg_content_hash_zero_literal
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::ContentHash>\\(crucible::ContentHash\\(0\\)\\)")
crucible_neg_compile_test(neg_content_hash_default_construct
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsNonZero\\(\\), crucible::ContentHash>\\(crucible::ContentHash\\(\\)\\)")

# The live-slot pointer of TransactionLog is
# fixy::Tagged<Transaction*, source::Ring>: a pointer into the ring's inline
# slots, valid for the log's lifetime.
#   * neg_active_tx_ptr_raw_assignment: a raw pointer does not convert,
#     because the constructor from the value is private.
#   * neg_active_tx_ptr_cross_source_assignment: a pointer under the Arena
#     tag does not convert, because no retag edge joins the two tags.
crucible_neg_compile_test(neg_active_tx_ptr_raw_assignment
  "neg_active_tx_ptr_raw_assignment.cpp:27:25: error: conversion from .crucible::FakeTransaction\\*. to non-scalar type"
  "\\{aka .fixy::Tagged<crucible::FakeTransaction\\*, fixy::tags::source::Ring>.\\} requested")
crucible_neg_compile_test(neg_active_tx_ptr_cross_source_assignment
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
crucible_neg_compile_test(neg_active_region_ptr_raw_assignment
  "conversion from .const crucible::FakeRegionNode\\*. to non-scalar type .ActiveRegionPtr."
  "fixy::Tagged<const crucible::FakeRegionNode\\*, fixy::tags::source::Vigil>")
crucible_neg_compile_test(neg_active_region_ptr_cross_source_assignment
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
crucible_neg_compile_test(neg_device_capability_raw_assignment
  "neg_device_capability_raw_assignment.cpp:23:36: error: no match for .operator=. \\(operand types are .crucible::BackgroundThread::DeviceCapability. \\{aka .fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>.\\} and .uint64_t."
  "no known conversion for argument 1 from .uint64_t. \\{aka .long unsigned int.\\} to .const fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>&.")
crucible_neg_compile_test(neg_device_capability_cross_source_assignment
  "neg_device_capability_cross_source_assignment.cpp:25:36: error: no match for .operator=. \\(operand types are .crucible::BackgroundThread::DeviceCapability. \\{aka .fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>.\\} and .fixy::Tagged<long unsigned int, fixy::tags::source::Calibrated>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<long unsigned int, fixy::tags::source::Calibrated>. to .const fixy::Tagged<long unsigned int, fixy::tags::source::Meridian>&.")

# RegionCache::ops_ holds fixy::Tagged<const TraceEntry*, source::RegionOps>.
#   * neg_region_ops_ptr_raw_assignment: a raw pointer does not convert,
#     because the constructor from the value is private.
#   * neg_region_ops_ptr_cross_source_assignment: a pointer under the Arena
#     tag does not convert, because no retag edge joins the two tags.
crucible_neg_compile_test(neg_region_ops_ptr_raw_assignment
  "neg_region_ops_ptr_raw_assignment.cpp:29:20: error: conversion from .const crucible::FakeTraceEntry\\*. to non-scalar type"
  "\\{aka .fixy::Tagged<const crucible::FakeTraceEntry\\*, fixy::tags::source::RegionOps>.\\} requested")
crucible_neg_compile_test(neg_region_ops_ptr_cross_source_assignment
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
crucible_neg_compile_test(neg_costmodel_regs_per_thread_overflow
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<255>\\(\\), short unsigned int>\\(256\\)")
crucible_neg_compile_test(neg_costmodel_regs_per_thread_max_uint16
  "__builtin_trap.*is not a constant expression"
  "mint_refined<BoundedAbove<255>\\(\\), short unsigned int>\\(std::numeric_limits<short unsigned int>::max\\(\\)\\)")
crucible_neg_compile_test(neg_costmodel_warp_size_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(33\\)")
crucible_neg_compile_test(neg_costmodel_warp_size_too_large
  "__builtin_trap.*is not a constant expression"
  "mint_refined<refined_algebra::AllOf<IsPowerOfTwo\\(\\), BoundedAbove<128>\\(\\)>\\(\\), short unsigned int>\\(256\\)")
crucible_neg_compile_test(neg_costmodel_utilization_above_one
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<0.0f, 1.0e\\+0f>\\(\\), float>\\(1.5e\\+0f\\)")
crucible_neg_compile_test(neg_costmodel_utilization_negative
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<0.0f, 1.0e\\+0f>\\(\\), float>\\(-5.0e-1f\\)")

# The evaluators are pure arithmetic over values the caller owns, and
# CtxFitsCostModel admits only a context whose row is empty.  Each fixture
# calls one evaluator with a context that claims one effect.  The first
# regex names the refused call and the context.  The second says that the
# gate failed, and the third names the refused row.
crucible_neg_compile_test(neg_costmodel_wave_efficiency_io_row
  "no matching function for call to .wave_efficiency\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::IO>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_test(neg_costmodel_sm_occupancy_alloc_row
  "no matching function for call to .sm_occupancy\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Alloc>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_test(neg_costmodel_evaluate_cost_bg_row
  "no matching function for call to .evaluate_cost\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Bg>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")
crucible_neg_compile_test(neg_costmodel_fusion_benefit_block_row
  "no matching function for call to .compute_fusion_benefit\\(const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Block> >&"
  "required for the satisfaction of .CtxFitsCostModel<Ctx>."
  "is_subrow\\(.*\\(\\^\\^ R1, \\^\\^ R2\\) \\[with R1 = foundation::effects::Row<foundation::effects::Effect::Block>. R2 = foundation::effects::Row<>\\]. evaluated to .false.")

# BackgroundThread::RegionReadyCallback::Fn is a noexcept function
# pointer, because the callback runs on the publish stage in the middle
# of a publication.  The first fixture shows that a function that is not
# noexcept does not convert to Fn.  The second shows that Fn is not the
# same type as the plain function pointer.
set(BG_CALLBACK_PARAMS "void\\*, const foundation::effects::Bg&, crucible::BackgroundThread::PublishStage, crucible::RegionNode\\*")
set(BG_CALLBACK_CLASS_PARAMS "void\\*, const class foundation::effects::Bg&, class crucible::BackgroundThread::PublishStage, struct crucible::RegionNode\\*")
crucible_neg_compile_test(neg_background_thread_callback_throwing_fnptr_rejected
  "invalid conversion from .void \\(\\*\\)\\(${BG_CALLBACK_PARAMS}\\). to .crucible::BackgroundThread::RegionReadyCallback::Fn."
  "aka .void \\(\\*\\)\\(${BG_CALLBACK_PARAMS}\\) noexcept.")
crucible_neg_compile_test(neg_background_thread_callback_type_identity_drift
  "static assertion failed: RegionReadyCallback::Fn carries noexcept, so it is not the plain function pointer type"
  ".void \\(\\*\\)\\(${BG_CALLBACK_CLASS_PARAMS}\\) noexcept. is not the same as .void \\(\\*\\)\\(${BG_CALLBACK_CLASS_PARAMS}\\).")

# Transaction::ts_ns holds an optional reading of the monotonic clock.
#   - neg_transaction_ts_ns_bare_u64_assign: a bare integer is no reading,
#     because the constructor of ClockSource from its value is private.
#   - neg_transaction_ts_ns_cross_source: a boot clock reading is a distinct
#     type from a monotonic one.
crucible_neg_compile_test(neg_transaction_ts_ns_bare_u64_assign
  "neg_transaction_ts_ns_bare_u64_assign.cpp:18:32: error: no match for .operator=. \\(operand types are .std::optional<fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int> >. and .uint64_t."
  "error: .constexpr fixy::ClockSource<Source, T>::ClockSource\\(T\\) \\[with foundation::algebra::lattices::ClockSource Source = foundation::algebra::lattices::ClockSource::Monotonic; T = long unsigned int\\]. is private within this context")
crucible_neg_compile_test(neg_transaction_ts_ns_cross_source
  "neg_transaction_ts_ns_cross_source.cpp:25:34: error: no match for .operator=. \\(operand types are .std::optional<fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int> >. and .fixy::ClockSource<foundation::algebra::lattices::ClockSource::Boot, long unsigned int>."
  "error: no matching function for call to .fixy::ClockSource<foundation::algebra::lattices::ClockSource::Monotonic, long unsigned int>::ClockSource\\(fixy::ClockSource<foundation::algebra::lattices::ClockSource::Boot, long unsigned int>\\).")

# Cipher::obj_path returns fixy::Tagged<std::string, tags::source::CipherPath>.
# The two fixtures read that return type by reflection.
#   - neg_cipher_obj_path_bare_string_assign: a bare std::string does not
#     convert, because the constructor from the value is explicit and private.
#   - neg_cipher_obj_path_cross_source: a path under FromUserPath does not
#     convert, because no retag edge reaches CipherPath.
crucible_neg_compile_test(neg_cipher_obj_path_bare_string_assign
  "neg_cipher_obj_path_bare_string_assign.cpp:41:45: error: no match for .operator=. \\(operand types are .CipherObjPath. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>.\\} and .std::string."
  "no known conversion for argument 1 from .std::string. \\{aka .std::__cxx11::basic_string<char>.\\} to .const fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>&.")
crucible_neg_compile_test(neg_cipher_obj_path_cross_source
  "neg_cipher_obj_path_cross_source.cpp:45:19: error: no match for .operator=. \\(operand types are .CipherObjPath. \\{aka .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>.\\} and .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::FromUserPath>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::FromUserPath>. to .const fixy::Tagged<std::__cxx11::basic_string<char>, fixy::tags::source::CipherPath>&.")

# ─── hash_family_of HS14 negative-compile fixtures ────────────────
#   - neg_types_hash_family_unspecialized  (incomplete primary template)
#   - neg_types_hash_family_cross_lane     (Family-A hash ≠ IsFamilyB)
crucible_neg_compile_test(neg_types_hash_family_unspecialized
  "invalid use of incomplete type .struct crucible::hash_family_of<int>."
  "using crucible::hash_family_of_t = typename crucible::hash_family_of::family .with HashT = int.")
crucible_neg_compile_test(neg_types_hash_family_cross_lane
  "static assertion failed"
  "the expression .is_same_v<typename crucible::hash_family_of<HashT>::family, crucible::hash_family::FamilyB> .with HashT = crucible::ContentHash.. evaluated to .false.")

# Expr::hash is fixy::Tagged<uint64_t, hash_family::FamilyB>.  The two
# fixtures take that type from the field.
#   - neg_expr_hash_bare_u64_assign: a bare uint64_t does not convert,
#     because the constructor from the value is explicit and private.
#   - neg_expr_hash_cross_family: a Family-A hash does not convert,
#     because the two families are different types.
crucible_neg_compile_test(neg_expr_hash_bare_u64_assign
  "neg_expr_hash_bare_u64_assign.cpp:29:39: error: no match for .operator=. \\(operand types are .ExprHash. \\{aka .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>.\\} and .uint64_t."
  "no known conversion for argument 1 from .uint64_t. \\{aka .long unsigned int.\\} to .const fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>&.")
crucible_neg_compile_test(neg_expr_hash_cross_family
  "neg_expr_hash_cross_family.cpp:31:21: error: no match for .operator=. \\(operand types are .ExprHash. \\{aka .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>.\\} and .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyA>.\\)"
  "no known conversion for argument 1 from .fixy::Tagged<long unsigned int, crucible::hash_family::FamilyA>. to .const fixy::Tagged<long unsigned int, crucible::hash_family::FamilyB>&.")

# ─── IsReflectFieldSupported HS14 negative-compile fixtures ───────
#   - neg_reflect_supported_void           (void excluded from allow-list)
#   - neg_reflect_supported_function_type  (function types excluded)
crucible_neg_compile_test(neg_reflect_supported_void
  "static assertion failed"
  "required by the constraints of .template<class T> concept crucible::detail_reflect::IsReflectFieldSupported.")
crucible_neg_compile_test(neg_reflect_supported_function_type
  "static assertion failed"
  "required by the constraints of .template<class T> concept crucible::detail_reflect::IsReflectFieldSupported.")

# pack_field packs only the field categories that IsReflectFieldSupported
# names.  A pointer-to-member field reaches its trailing static_assert.
crucible_neg_compile_test(neg_reflect_pack_field_unsupported_category
  "static assertion failed: T is outside IsReflectFieldSupported: pack_field handles enum, integral"
  "In instantiation of .constexpr uint64_t crucible::detail_reflect::pack_field\\(const T&\\) .with T = int Target::\\*.")

# ─── Philox op_key DetSafe gate neg-compiles (2 fixtures) ───────────
#
# The raw per-op key derivation API is not public.  Production callers
# must route through op_key_det(), carrying DetSafe<Pure> provenance
# until the boundary that explicitly unwraps the key bytes.  One fixture
# names the raw function, the other takes the band as a bare uint64_t.
crucible_neg_compile_test(neg_philox_raw_op_key
  "is not a member of"
  ".op_key. is not a member of .crucible::Philox.")
crucible_neg_compile_test(neg_philox_op_key_det_raw_uint64
  "cannot convert"
  "cannot convert .crucible::Philox::DetSafePureKey.*to .uint64_t.")

# ─── OpaqueLifetime production-call-site neg-compile ──────────────
#
# Load-bearing rejection at the persistence boundary: an
# OpaqueLifetime<PER_REQUEST, T> value rejected by Cipher::
# commit_per_fleet, whose requires-clause demands satisfies<PER_FLEET>.
# Captures the inferlet cross-request leak bug class (PdaState /
# user state declared PER_REQUEST silently flowed to fleet-durable
# cold storage).
crucible_neg_compile_test(neg_opaque_lifetime_per_request_at_per_fleet_fence
  "no matching function for call to .crucible::Cipher::commit_per_fleet."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_REQUEST>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_FLEET.. evaluated to .false.")

# The rest of the lifetime to tier refusals.  A request-scoped region
# does not reach the warm tier, which survives a restart, and a
# program-scoped region does not reach the fleet-wide cold tier.  A bare
# pointer and a band of another lattice state no scope, so they reach no
# commit entry point.
crucible_neg_compile_test(neg_opaque_lifetime_per_request_at_per_program_fence
  "no matching function for call to .crucible::Cipher::commit_per_program."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_REQUEST>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_PROGRAM.. evaluated to .false.")
crucible_neg_compile_test(neg_opaque_lifetime_per_program_at_per_fleet_fence
  "no matching function for call to .crucible::Cipher::commit_per_fleet."
  "the expression .satisfies_v<W, Scope> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::LifetimeLattice::At<foundation::algebra::lattices::Lifetime::PER_PROGRAM>, const crucible::RegionNode.>. Scope = foundation::algebra::lattices::Lifetime::PER_FLEET.. evaluated to .false.")
crucible_neg_compile_test(neg_cipher_commit_per_request_bare_pointer
  "no matching function for call to .crucible::Cipher::commit_per_request."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = const crucible::RegionNode... evaluated to .false.")
crucible_neg_compile_test(neg_cipher_commit_per_request_wrong_band
  "no matching function for call to .crucible::Cipher::commit_per_request."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::CipherTierLattice::At<foundation::algebra::lattices::CipherTierTag::Hot>, const crucible::RegionNode.>.. evaluated to .false.")
crucible_neg_compile_test(neg_cipher_commit_per_program_bare_pointer
  "no matching function for call to .crucible::Cipher::commit_per_program."
  "the expression .is_band_of_v<foundation::algebra::lattices::LifetimeLattice, W> .with W = const crucible::RegionNode... evaluated to .false.")

# A NumericalTier below BITEXACT must not reach a consumer that requires
# BITEXACT.  The three fixtures refuse a gap of six tiers (RELAXED), one
# tier (ULP_FP64) and three tiers (ULP_FP16), so the refusal is
# structural along the whole lattice.  The first regex names the tier of
# the value, and the second names the gate of the consumer.
crucible_neg_compile_test(neg_recipe_registry_relaxed_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::RELAXED>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_test(neg_recipe_registry_fp64_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::ULP_FP64>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_test(neg_recipe_registry_fp16_at_bitexact_fence
  "no matching function for call to .bitexact_fence_consumer\\(.*ToleranceLattice::At<foundation::algebra::lattices::Tolerance::ULP_FP16>, int>"
  "requires +satisfies_v<W, foundation::algebra::lattices::Tolerance::BITEXACT>")

# by_name_pinned takes a Tolerance.  A RecipeFamily enumerator in its
# place must fail at the template parameter.  Admission on the two axes
# uses by_name_spec and then fixy::admits.
crucible_neg_compile_test(neg_recipe_registry_pinned_with_family_template_arg
  "no matching function for call to .crucible::RecipeRegistry::by_name_pinned<foundation::algebra::lattices::RecipeFamily::Kahan>"
  "could not convert .foundation::algebra::lattices::RecipeFamily::Kahan. from .foundation::algebra::lattices::RecipeFamily. to .foundation::algebra::lattices::Tolerance.")

# The RecipeSpec of by_name_spec and the NumericalTier of by_name_pinned
# are different types.  The expected of one does not convert to the
# expected of the other, in each direction.
crucible_neg_compile_test(neg_recipe_registry_spec_assigned_to_pinned_expected
  "conversion from .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::RecipeFamilyLattice> >"
  "to non-scalar type .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ToleranceLattice::At<foundation::algebra::lattices::Tolerance::BITEXACT>")
crucible_neg_compile_test(neg_recipe_registry_pinned_assigned_to_spec_expected
  "conversion from .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ToleranceLattice::At<foundation::algebra::lattices::Tolerance::BITEXACT>"
  "to non-scalar type .expected<Graded<\\[\\.\\.\\.\\],foundation::algebra::lattices::ProductLattice<foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::ToleranceLattice>, foundation::algebra::lattices::DualLattice<foundation::algebra::lattices::RecipeFamilyLattice> >")

# The lookups of the registry are pure projections.  by_name, by_hash,
# entries, the pinned lookups and the spec lookups each refuse a caller
# row that is not empty.  The first regex names the lookup and the row,
# and the second names the Subrow gate that refused the row.
crucible_neg_compile_test(neg_recipe_registry_by_name_io_row
  "no matching function for call to .crucible::RecipeRegistry::by_name<foundation::effects::Row<foundation::effects::Effect::IO> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO>\\]")
crucible_neg_compile_test(neg_recipe_registry_by_hash_init_row
  "no matching function for call to .crucible::RecipeRegistry::by_hash<foundation::effects::Row<foundation::effects::Effect::Init> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Init>\\]")
crucible_neg_compile_test(neg_recipe_registry_entries_bg_row
  "no matching function for call to .crucible::RecipeRegistry::entries<foundation::effects::Row<foundation::effects::Effect::Bg> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>\\]")
crucible_neg_compile_test(neg_recipe_registry_pinned_alloc_row
  "no matching function for call to .crucible::RecipeRegistry::by_name_pinned<foundation::algebra::lattices::Tolerance::BITEXACT, foundation::effects::Row<foundation::effects::Effect::Alloc> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>\\]")
crucible_neg_compile_test(neg_recipe_registry_spec_block_row
  "no matching function for call to .crucible::RecipeRegistry::by_name_spec<foundation::effects::Row<foundation::effects::Effect::Block> >"
  "Subrow<CallerRow, foundation::effects::Row<> >. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Block>\\]")

# entries() gives a span tagged source::JsonRegistry.  A raw span and a
# span with a different tag must not reach a consumer of the entries.
crucible_neg_compile_test(neg_recipe_registry_entries_raw_span
  "could not convert .raw. from .std::span<const crucible::RecipeRegistry::Entry>. to .crucible::RecipeRegistry::Entries."
  "fixy::Tagged<std::span<const crucible::RecipeRegistry::Entry>, fixy::tags::source::JsonRegistry>")
crucible_neg_compile_test(neg_recipe_registry_entries_cross_tag
  "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
  "to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::JsonRegistry>. requested")

# Entry::scalar_values of TraceRing is fixy::FixedArray<int64_t, 5>.  Two
# fixtures hold its two gates:
#   * neg_tracering_scalar_values_at_oob: at<I>() refuses an index that is
#     not less than N.
#   * neg_tracering_scalar_values_in_place_wrong_arity: the in-place
#     constructor refuses std::in_place with six values for five slots.
crucible_neg_compile_test(neg_tracering_scalar_values_at_oob
  "no matching function for call to .fixy::FixedArray<long int, 5>::at<5>\\(\\)."
  "the expression .I < N .with I = 5. N = 5.. evaluated to .false.")
crucible_neg_compile_test(neg_tracering_scalar_values_in_place_wrong_arity
  "no matching function for call to .fixy::FixedArray<long int, 5>::FixedArray\\(<brace-enclosed initializer list>\\)."
  "the expression .sizeof ... \\(Args ...\\) == N .with Args = .long int, long int, long int, long int, long int, long int.. N = 5.. evaluated to .false.")

# BitMask stores a refined mask.  The numeric ceiling
# ((1 << kGroupWidth) - 1) bounds the mask, not kGroupWidth itself: a
# valid mask of width 16 can be 0xF0F0.  The two fixtures pin the edge
# one above the ceiling and a wide miss.  The first regex says that the
# precondition of the checked mint failed.  The second names the mint and
# its ceiling predicate.  The third names the BitMask constructor and the
# refused value.  On AVX512BW the width is 64 and no value is out of
# range, so each fixture fails there on its own static_assert.
crucible_neg_compile_test(neg_swiss_bitmask_one_past_group_mask
  "__builtin_trap.*is not a constant expression|requires a group width below 64"
  "fixy::mint_refined<BoundedAbove<[0-9]+>\\(\\), long unsigned int>\\(raw_mask\\)|requires a group width below 64"
  "BitMask::BitMask\\(.*kGroupMaskCeiling\\) \\+ 1\\)\\)|requires a group width below 64")
crucible_neg_compile_test(neg_swiss_bitmask_wide_miss
  "__builtin_trap.*is not a constant expression|requires a group width below 64"
  "fixy::mint_refined<BoundedAbove<[0-9]+>\\(\\), long unsigned int>\\(raw_mask\\)|requires a group width below 64"
  "BitMask::BitMask\\(std::numeric_limits<long unsigned int>::max\\(\\)\\)|requires a group width below 64")

# kGroupWidth is a fixy::PowerOfTwo<size_t>, and the checked mint is its
# only door.  The two fixtures give the mint zero and a value that is not
# a power of two.  The second regex names the mint, its predicate and the
# refused value.
crucible_neg_compile_test(neg_swiss_group_width_zero
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(0\\)")
crucible_neg_compile_test(neg_swiss_group_width_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(48\\)")

# ExprPool reads int_cache_ through IntCacheLiteral, which is
# Refined<in_range<-128, 127>, int64_t>.  Two fixtures pin the two ends
# of the closed range: -129 is one below the low end, and 128 is one
# above the high end.  The first regex says that the precondition of
# the door failed.  The second names the door, its predicate and the
# value, so the rejection is the one under test.
crucible_neg_compile_test(neg_expr_pool_int_cache_literal_below_low
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<-128, 127>\\(\\), long int>\\(-129\\)")
crucible_neg_compile_test(neg_expr_pool_int_cache_literal_above_high
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<-128, 127>\\(\\), long int>\\(128\\)")

# ExprPool::capacity_ is a PowerOfTwo<size_t>, and intern_count_ is a
# Monotonic<size_t>.  The fixtures pin two absences: no capacity that is
# not a power of two, and no assignment of a raw integer to the intern
# count.
crucible_neg_compile_test(neg_expr_pool_capacity_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), long unsigned int>\\(12\\)")
crucible_neg_compile_test(neg_expr_pool_intern_count_assign_raw
  "no match for .operator=."
  "crucible::ExprPool::InternCount.*fixy::Monotonic<long unsigned int")

# ExprPool::make returns DetSafe<Pure, Tagged<const Expr*,
# source::Interned>>.  A raw Expr* and a value at the same tier with a
# different source tag must both fail at the consumer.
crucible_neg_compile_test(neg_expr_pool_make_raw_expr
  "could not convert"
  "from .const crucible::Expr\\*. to .crucible::ExprPool::PureInternedExpr.")
crucible_neg_compile_test(neg_expr_pool_make_cross_tag
  "could not convert"
  "source::External>>. to .Graded<.*source::Interned>>")

# RecipePool::capacity_ is a PowerOfTwo<uint32_t>, and size_ is a
# Monotonic<uint32_t>.  The fixtures pin two absences: no capacity that
# is not a power of two, and no assignment of a raw integer to the size.
# The first regex says that the gate refused.  The second names the
# door, the predicate or the type, so the refusal is the one under test.
crucible_neg_compile_test(neg_recipe_pool_capacity_not_power_of_two
  "__builtin_trap.*is not a constant expression"
  "mint_refined<IsPowerOfTwo\\(\\), unsigned int>\\(12\\)")
crucible_neg_compile_test(neg_recipe_pool_size_assign_raw
  "no match for .operator=."
  "crucible::RecipePool::Size.*fixy::Monotonic<unsigned int")

# The RecipePool constructor takes a minted BorrowedRef<Arena, Brand>.
# A raw Arena* and a borrow of a different type must fail at the
# constructor.
crucible_neg_compile_test(neg_recipe_pool_arena_raw_pointer
  "no matching function for call to .crucible::RecipePool::RecipePool"
  "mismatched types .fixy::BorrowedRef<crucible::Arena, Brand>. and .crucible::Arena\\*.")
crucible_neg_compile_test(neg_recipe_pool_arena_wrong_borrow
  "no matching function for call to .crucible::RecipePool::RecipePool"
  "mismatched types .crucible::Arena. and .OtherArena.")

# A RecipePool is built in the initialization phase.  The constructor
# defaults to Row<Init>, and a pure row, a row with only Alloc and a row
# with only Bg each fail the Subrow gate.
crucible_neg_compile_test(neg_recipe_pool_ctor_pure_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<>\\]")
crucible_neg_compile_test(neg_recipe_pool_ctor_alloc_only_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>\\]")
crucible_neg_compile_test(neg_recipe_pool_ctor_bg_only_row
  "constraints.*not satisfied"
  "Subrow<foundation::effects::Row<foundation::effects::Effect::Init>, CallerRow>. \\[with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>\\]")

# The RecipeRegistry constructor takes a minted BorrowedRef<RecipePool,
# Brand>.  A raw RecipePool* and a borrow of a different type must fail
# at the constructor.
crucible_neg_compile_test(neg_recipe_registry_pool_raw_pointer
  "no matching function for call to .crucible::RecipeRegistry::RecipeRegistry"
  "mismatched types .fixy::BorrowedRef<crucible::RecipePool, Brand>. and .crucible::RecipePool\\*.")
crucible_neg_compile_test(neg_recipe_registry_pool_wrong_borrow
  "no matching function for call to .crucible::RecipeRegistry::RecipeRegistry"
  "mismatched types .crucible::RecipePool. and .crucible::Arena.")

# forge/recipes/Network.h network-kernel recipe constraints.
#   * neg_network_recipe_strict_sharp: BITEXACT_STRICT cannot enter SHARP.
#   * neg_network_recipe_non_commutative_sharp: SHARP requires commutativity.
#   * neg_network_recipe_bitexact_lossy: BITEXACT tiers reject lossy Top-K.
#   * neg_network_recipe_recursive_non_power2: recursive halving-doubling
#                                             requires power-of-two peers.
#   * neg_network_recipe_raw_constraints: consumers require registry provenance.
crucible_neg_compile_test(neg_network_recipe_strict_sharp
  "no matching function for call to .accepts_network_recipe<StrictRecipe, crucible::forge::recipes::SharpAlgorithm>\\(\\)."
  "required for the satisfaction of .NetworkRecipeEligible<R, A>. .with R = StrictRecipe. A = crucible::forge::recipes::SharpAlgorithm.")
crucible_neg_compile_test(neg_network_recipe_non_commutative_sharp
  "no matching function for call to .accepts_network_recipe<NonCommutativeRecipe, crucible::forge::recipes::SharpAlgorithm>\\(\\)."
  "required for the satisfaction of .NetworkRecipeEligible<R, A>. .with R = NonCommutativeRecipe. A = crucible::forge::recipes::SharpAlgorithm.")
crucible_neg_compile_test(neg_network_recipe_bitexact_lossy
  "no matching function for call to .accepts_network_recipe<TensorCoreRecipe, crucible::forge::recipes::TopKCompressedAlgorithm>\\(\\)."
  "required for the satisfaction of .NetworkRecipeEligible<R, A>. .with R = TensorCoreRecipe. A = crucible::forge::recipes::TopKCompressedAlgorithm.")
crucible_neg_compile_test(neg_network_recipe_recursive_non_power2
  "no matching function for call to .accepts_network_recipe<OddParticipantRecipe, crucible::forge::recipes::RecursiveHalvingDoublingAlgorithm>\\(\\)."
  "required for the satisfaction of .NetworkRecipeEligible<R, A>. .with R = OddParticipantRecipe. A = crucible::forge::recipes::RecursiveHalvingDoublingAlgorithm.")
crucible_neg_compile_test(neg_network_recipe_raw_constraints
  "could not convert .raw. from .crucible::forge::recipes::NetworkRecipeConstraints."
  "to .crucible::forge::recipes::DeclaredNetworkRecipeConstraints. .aka .fixy::Tagged<crucible::forge::recipes::NetworkRecipeConstraints")

# forge/Ir001/Comm.h comm-through-IR substrate.
#   * neg_ir001_comm_raw_node: Forge visitors require source::Ir001 provenance.
#   * neg_ir001_comm_send_as_collective: point-to-point ops are not collectives.
#   * neg_ir001_comm_zero_participants: participant counts are positive.
crucible_neg_compile_test(neg_ir001_comm_raw_node
  "could not convert .raw. from .crucible::forge::ir001::AllReduceOp."
  "to .crucible::forge::ir001::DeclaredIr001Node<")
crucible_neg_compile_test(neg_ir001_comm_send_as_collective
  "no matching function for call to .accepts_collective_kind<crucible::forge::ir001::Ir001OpKind::SendAsync>\\(\\)."
  "required for the satisfaction of .Ir001CollectiveKind<Kind>. .with Kind = crucible::forge::ir001::Ir001OpKind::SendAsync.")
crucible_neg_compile_test(neg_ir001_comm_zero_participants
  "__builtin_trap.*is not a constant expression"
  "mint_refined<InRange<1, 4096>\\(\\), short unsigned int>\\(0\\)")

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
  "no matching function for call to .admit_comm_fusion<crucible::forge::_wip::phases::comm::CommFusionPattern::SendFromEpilogue, crucible::cog::CogKind::Gpu>\\(ComputeNode&,"
  ".crucible::forge::ir001::Ir001Node<crucible::forge::ir001::Ir001OpKind::Gemm, .*. is not derived from .fixy::Tagged<ComputeNode, fixy::tags::source::Ir001>.")
crucible_neg_compile_test(neg_forge_comm_phase_send_as_collective
  "static assertion failed"
  "the expression .\\(pattern_accepts_kind_pair<Pattern, ComputeNode::kind, CommNode::kind>\\)\\(\\) .with Pattern = crucible::forge::_wip::phases::comm::CommFusionPattern::ReduceOnRecv. ComputeNode = crucible::forge::ir001::Ir001Node<crucible::forge::ir001::Ir001OpKind::RecvAsync")
crucible_neg_compile_test(neg_forge_comm_phase_oversubscribed
  "static assertion failed"
  "the expression .row_fits_cog_v<Row, K> .with Row = foundation::effects::ConcurrentRow<foundation::effects::resource::SmBudget<999>, foundation::effects::resource::NvlinkBandwidth<1> >. K = crucible::cog::CogKind::Gpu.. evaluated to .false.")
crucible_neg_compile_test(neg_forge_comm_phase_strict_compress
  "no matching function for call to .accepts_compress_before_send<StrictRecipe>\\(\\)."
  "required for the satisfaction of .CommFusionRecipeAllowed<Recipe, crucible::forge::_wip::phases::comm::CommFusionPattern::CompressBeforeSend>. .with Recipe = StrictRecipe.")
crucible_neg_compile_test(neg_forge_comm_phase_unknown_pattern
  "call to non-.constexpr. function .void std::unreachable\\(\\)."
  "in .constexpr. expansion of .crucible::forge::_wip::phases::comm::comm_fusion_shape\\(\\(\\(crucible::forge::_wip::phases::comm::CommFusionPattern\\)kUnlistedPattern\\)\\).")
crucible_neg_compile_test(neg_forge_comm_phase_wrong_lane
  "could not convert .fused. from .Tagged<\\[\\.\\.\\.\\],ForgePhase<'D'>>."
  "to .Tagged<\\[\\.\\.\\.\\],ForgePhase<'F'>>.")

# mimic/_wip/network/Backend.h per-vendor backend sketch.
#   * neg_mimic_network_backend_raw_node: backend planning requires admitted
#     source::Ir001 nodes, not raw comm descriptors.
#   * neg_mimic_network_backend_compute_node: backend planning accepts only
#     network-visible point-to-point or collective IR001 nodes.
#   * neg_mimic_network_backend_cross_vendor: backend artifacts are source-tagged
#     per vendor and cannot cross vendor launch boundaries.
#   * neg_mimic_network_backend_wrong_cog: vendor/cog compatibility is a
#     compile-time gate.
crucible_neg_compile_test(neg_mimic_network_backend_raw_node
  "no matching function for call to .plan_network_kernel<crucible::mimic::_wip::network::NetworkBackendVendor::Cpu>\\("
  ".crucible::forge::ir001::Ir001Node<crucible::forge::ir001::Ir001OpKind::AllReduce, crucible::forge::ir001::CollectiveAttrs, foundation::effects::Row<> >. is not derived from .fixy::Tagged<Node, fixy::tags::source::Ir001>.")
crucible_neg_compile_test(neg_mimic_network_backend_compute_node
  "static assertion failed"
  "the expression .ir001_network_kernel_kind_v<Node::kind> .with Node = crucible::forge::ir001::Ir001Node<crucible::forge::ir001::Ir001OpKind::Gemm,")
crucible_neg_compile_test(neg_mimic_network_backend_cross_vendor
  "could not convert .am. from .Tagged<\\[\\.\\.\\.\\],crucible::mimic::_wip::network::wip_source::AmNetwork>."
  "to .Tagged<\\[\\.\\.\\.\\],crucible::mimic::_wip::network::wip_source::NvNetwork>.")
crucible_neg_compile_test(neg_mimic_network_backend_wrong_cog
  "static assertion failed"
  "the expression .\\(backend_accepts_cog_kind<Vendor, Kind>\\)\\(\\) .with Vendor = crucible::mimic::_wip::network::NetworkBackendVendor::Mellanox. Kind = crucible::cog::CogKind::Gpu.. evaluated to .false.")

# mimic/Semaphore.h: mint_device_semaphore is the one door to a descriptor.
#   * neg_mimic_semaphore_backend_none, _backend_portable: SemaphoreBackend
#     refuses a backend that names no one device, in the mint and in the
#     class.
#   * neg_mimic_semaphore_temporary_counter: the mint refuses a temporary
#     counter, because the descriptor borrows it.
#   * neg_mimic_semaphore_forged, _default_construct: no constructor of the
#     descriptor goes around the mint.
crucible_neg_compile_test(neg_mimic_semaphore_backend_none
  "no matching function for call to .mint_device_semaphore<foundation::algebra::lattices::VendorBackend::None>\\(std::atomic<long unsigned int>&, int\\)."
  "the expression .Backend != foundation::algebra::lattices::VendorLattice::bottom\\(\\)\\(\\) \\[with Backend = foundation::algebra::lattices::VendorBackend::None\\]. evaluated to .false.")
crucible_neg_compile_test(neg_mimic_semaphore_backend_portable
  "template constraint failure for .template<foundation::algebra::lattices::VendorBackend Backend>  requires  SemaphoreBackend<Backend> class crucible::mimic::DeviceSemaphore."
  "the expression .Backend != foundation::algebra::lattices::VendorLattice::top\\(\\)\\(\\) \\[with Backend = foundation::algebra::lattices::VendorBackend::Portable\\]. evaluated to .false.")
crucible_neg_compile_test(neg_mimic_semaphore_temporary_counter
  "use of deleted function .constexpr crucible::mimic::DeviceSemaphore<Backend> crucible::mimic::mint_device_semaphore\\(std::atomic<long unsigned int>&&, uint64_t\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU. uint64_t = long unsigned int\\]."
  ".: The DeviceSemaphore borrows its counter, so a temporary counter dangles at the end of the full expression. Bind the counter to a name first.")
crucible_neg_compile_test(neg_mimic_semaphore_forged
  ".constexpr crucible::mimic::DeviceSemaphore<Backend>::DeviceSemaphore\\(std::atomic<long unsigned int>&, uint64_t\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU. uint64_t = long unsigned int\\]. is private within this context"
  "mimic/Semaphore.h:[0-9]+:[0-9]+: note: declared private here")
crucible_neg_compile_test(neg_mimic_semaphore_default_construct
  "use of deleted function .crucible::mimic::DeviceSemaphore<Backend>::DeviceSemaphore\\(\\) \\[with foundation::algebra::lattices::VendorBackend Backend = foundation::algebra::lattices::VendorBackend::CPU\\]."
  ".: A DeviceSemaphore borrows a counter. Take one from mint_device_semaphore.")

# ReplayEngine::init takes the pool as fixy::BorrowedRef<const
# PoolAllocator>.  A raw pointer and a borrow of another owner type are
# both refused at the init boundary.
crucible_neg_compile_test(neg_replay_engine_pool_raw_pointer
  "cannot convert .const crucible::PoolAllocator\\*. to .crucible::ReplayEngine::PoolBorrow."
  "neg_replay_engine_pool_raw_pointer.cpp:15")
crucible_neg_compile_test(neg_replay_engine_pool_wrong_borrow
  "cannot convert .BorrowedRef<crucible::RegionNode>. to .BorrowedRef<const crucible::PoolAllocator>."
  "neg_replay_engine_pool_wrong_borrow.cpp:15")

# Production-call-site fence: a hot-path consumer that requires the
# Pool allocation class refuses a Heap band.  This is the rejection that
# carries the no-malloc-on-hot-path rule at the slot_ptr_pinned to
# consumer boundary.  Without it, jemalloc allocations (50-200 ns
# setup) could flow into hot-path sites budgeted at a few ns per call.
crucible_neg_compile_test(neg_alloc_class_heap_at_pool_fence
  "no matching function for call to .hot_path_slot_consumer"
  "the expression .satisfies_v<Slot, foundation::algebra::lattices::AllocClassTag::Pool> \\[with Slot = foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::AllocClassLattice::At<foundation::algebra::lattices::AllocClassTag::Heap>, int\\*>\\]. evaluated to .false.")

# ─── IsCacheableFunction concept rejection ────────────────────────
# `auto FnPtr` accepts ANY structural NTTP — without the concept
# fence the dispatcher cache would silently produce nonsense slots
# for integral constants, data-pointers, or member-function
# pointers.  Three orthogonal rejection classes mirror the three
# wrong-shape NTTP categories the production API guards against.
crucible_neg_compile_test(neg_computation_cache_integral_nttp
  "no matching function for call to .lookup_computation_cache<42, int>\\(\\)."
  "the expression .is_pointer_v<decltype\\(FnPtr\\)> .with FnPtr = 42.. evaluated to .false.")
crucible_neg_compile_test(neg_computation_cache_data_ptr_nttp
  "no matching function for call to .insert_computation_cache<\\(& data_global\\)>\\(crucible::cipher::CompiledBody\\*\\)."
  "the expression .is_function_v<typename std::remove_pointer<decltype\\(Value\\)>::type> .with FnPtr = &data_global.. evaluated to .false.")
crucible_neg_compile_test(neg_computation_cache_member_fn_nttp
  "no matching function for call to .lookup_computation_cache<&Receiver::member_fn, int>\\(\\)."
  ".void \\(Receiver::\\*\\)\\(int\\) noexcept. is not a pointer")
# Sister rejection — member-DATA pointers (vs member-fn pointers
# above).  With it, the fixtures cover all four NTTP categories that
# the cache cannot key on.
crucible_neg_compile_test(neg_computation_cache_member_data_ptr_nttp
  "no matching function for call to .lookup_computation_cache<&Receiver::data_field>\\(\\)."
  ".int Receiver::\\*. is not a pointer")
# IsEffectRow concept fence — the row-aware cache API rejects a Row
# parameter that is not an effects::Row<...>.
crucible_neg_compile_test(neg_computation_cache_in_row_non_row_param
  "no matching function for call to .lookup_computation_cache_in_row<cacheable_fn, int, double>\\(\\)."
  "required for the satisfaction of .IsEffectRow<Row>. .with Row = int.")
# Cross-constraint coverage — the IsCacheableFunction conjunct on the
# row-aware path.  The `_in_row` template carries TWO
# constraints (`IsCacheableFunction<FnPtr> && IsEffectRow<Row>`); the
# row-blind neg fixtures cover the IsCacheableFunction half on the
# row-blind path and `neg_computation_cache_in_row_non_row_param` covers
# the IsEffectRow half on the row-aware path — this fixture closes
# the matrix on the row-aware × IsCacheableFunction cell.
crucible_neg_compile_test(neg_computation_cache_in_row_non_function_nttp
  "no matching function for call to .lookup_computation_cache_in_row<42, foundation::effects::Row<>, int>\\(\\)."
  "required for the satisfaction of .IsCacheableFunction<FnPtr>. .with FnPtr = 42.")
# The content key folds the printed name of the function and of each
# argument type, so it refuses a closure's static invoker and a closure
# argument type: either prints a name that depends on the translation
# unit.
crucible_neg_compile_test(neg_computation_cache_closure_invoker
  "computation_cache_key: the function, its signature or an argument type has no stable identity"
  "HasStableKeyIdentity")
crucible_neg_compile_test(neg_computation_cache_closure_argument
  "computation_cache_key: the function, its signature or an argument type has no stable identity"
  "HasStableKeyIdentity")

# Every Cipher operation that reaches the store takes a context whose row
# admits IO and Block: open, mint_open_view, record_event and
# persist_session_events.  Each fixture below refuses one context, and
# the second regex names the context the gate refused.  The four
# record_event rows hold each atom of the gate on its own: IO alone,
# Block alone, Alloc alone, and the background drain row, whose Bg atom
# implies neither IO nor Block.
crucible_neg_compile_test(neg_cipher_record_event_pure_row
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_test(neg_cipher_record_event_io_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::IO> >")
crucible_neg_compile_test(neg_cipher_record_event_block_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Block> >")
crucible_neg_compile_test(neg_cipher_record_event_alloc_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Alloc> >")
crucible_neg_compile_test(neg_cipher_record_event_bg_only
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc> >")
# A commit also stamps the log with a reading of the monotonic clock, so
# record_event and Vigil::persist refuse a row that fits the store and
# owns none of Bg, Init and Test.
crucible_neg_compile_test(neg_cipher_record_event_store_only_row
  "no matching function for call to .crucible::Cipher::record_event."
  "required for the satisfaction of .CtxFitsMonotonicClock<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block> >")
crucible_neg_compile_test(neg_vigil_persist_store_only_row
  "no matching function for call to .crucible::Vigil::persist."
  "required for the satisfaction of .CtxFitsMonotonicClock<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block> >")
crucible_neg_compile_test(neg_cipher_open_view_hot_fg_ctx
  "no matching function for call to .crucible::Cipher::mint_open_view.foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. const."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_test(neg_cipher_open_view_init_ctx
  "no matching function for call to .crucible::Cipher::mint_open_view.fixy::ColdInitCtx. const."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")
crucible_neg_compile_test(neg_cipher_open_ctx_without_block
  "no matching function for call to .crucible::Cipher::open.const fixy::ColdInitCtx&, "
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")
crucible_neg_compile_test(neg_cipher_open_non_context
  "no matching function for call to .crucible::Cipher::open.foundation::effects::Test, "
  "the expression .is_exec_ctx\\(.* .with T = foundation::effects::Test.. evaluated to .false.")
crucible_neg_compile_test(neg_cipher_persist_session_events_fg_ctx
  "no matching function for call to .crucible::Cipher::persist_session_events."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >")
crucible_neg_compile_test(neg_cipher_persist_session_events_compile_ctx
  "no matching function for call to .crucible::Cipher::persist_session_events."
  "required for the satisfaction of .CtxFitsCipherPersistence<Ctx>. .with Ctx = foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >")

# ─── Federation-bridge concept-fence parity ───────────────────────
# The federation primitives carry the SAME `IsCacheableFunction<FnPtr>
# && IsEffectRow<Row>` constraint pair as the `*_in_row` family.  The
# `*_in_row` path has its own fence-rejection witnesses
# (neg_computation_cache_in_row_non_row_param +
# neg_computation_cache_in_row_non_function_nttp).  These two fixtures
# extend the matrix to the federation surface, so a regression in the
# bridge does not silently pass non-Row / non-Fn args through to a
# row-blind federation key.
crucible_neg_compile_test(neg_computation_cache_federation_non_row
  "no matching function for call to .federation_key<cacheable_fn, int, double>\\(\\)."
  "required for the satisfaction of .IsEffectRow<Row>. .with Row = int.")
crucible_neg_compile_test(neg_computation_cache_federation_non_fn
  "no matching function for call to .federation_key<42, foundation::effects::Row<>, int>\\(\\)."
  "required for the satisfaction of .IsCacheableFunction<FnPtr>. .with FnPtr = 42.")
crucible_neg_compile_test(neg_computation_cache_federation_without_local_permission
  "no matching function for call to .serialize_computation_cache_federation_entry<neg_payload, foundation::effects::Row<>, int>\\("
  "serialize_computation_cache_federation_entry\\(LocalCipherPermission<Brand>&, std::span<unsigned char>")

# ─── The row gate of MetaLog::try_append_pure ───────────────────────
# MetaLog::try_append_pure<CallerRow> requires IsPure<CallerRow>, that
# is, CallerRow is a subrow of Row<>.  There is one fixture for each of
# the six atoms (Alloc, IO, Block, Bg, Init and Test), one for a row of
# two atoms, and one for the full row.  If the gate widens to admit one
# atom or one combination of atoms, the fixture for it compiles.  Each
# regex names the row that the gate refused.
crucible_neg_compile_test(neg_metalog_try_append_pure_io
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::IO> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_bg
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Bg> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Bg>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_alloc
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Alloc> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_block
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Block> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Block>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_init
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Init> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Init>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_test
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::Test> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Test>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_io_block
    "no matching function for call to .crucible::MetaLog::try_append_pure<foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block> >"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::IO, foundation::effects::Effect::Block>.")
crucible_neg_compile_test(neg_metalog_try_append_pure_full_row
    "no matching function for call to .crucible::MetaLog::try_append_pure<fixy::AllRow>"
    "satisfaction of .IsPure<CallerRow>. .with CallerRow = foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, ")

# ─── TraceRing row-fence — full per-axis matrix on both legs ──────
# TraceRing::try_append_pure<CallerRow> AND TraceRing::drain_pure<CallerRow>
# both require IsPure<CallerRow>.  Sibling of the MetaLog::try_append_pure
# matrix above.  16 fixtures = 8 per direction (per-axis × 6 atoms +
# multi-atom + saturation).  Pins both directions of the SPSC ring.
# Producer side:
crucible_neg_compile_test(neg_tracering_try_append_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_io
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_init
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_test
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_try_append_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
# Consumer side:
crucible_neg_compile_test(neg_tracering_drain_pure_alloc
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_io
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_bg
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_init
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_test
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_io_block
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")
crucible_neg_compile_test(neg_tracering_drain_pure_full_row
  "constraints not satisfied|template constraint failure|associated constraints" "satisfaction of .IsPure<CallerRow>")

# Vigil::dispatch_op_pure takes crucible::VigilFgCtx, the context of the
# Vigil's producer claim.  Each fixture passes another context, or none.
# The first regex pins the refused context or call, and the second pins the
# parameter that refuses it.
set(VIGIL_PURE_TO "to .const crucible::VigilFgCtx&.")
set(VIGIL_PURE_PARAM
  "initializing argument 1 of .crucible::DispatchResult crucible::Vigil::dispatch_op_pure\\(const crucible::VigilFgCtx&, ")
set(VIGIL_PURE_ROW_TAIL "foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_bg_ctx
  "cannot convert .const fixy::BgLoadCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_init_ctx
  "cannot convert .const fixy::InitLoadCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_test_ctx
  "cannot convert .const fixy::TestRunnerCtx. \\{aka .const foundation::effects::ExecCtx<foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test, ${VIGIL_PURE_ROW_TAIL}.\\} ${VIGIL_PURE_TO}"
  "${VIGIL_PURE_PARAM}")
crucible_neg_compile_test(neg_vigil_dispatch_op_pure_no_ctx
  "no matching function for call to .crucible::Vigil::dispatch_op_pure\\(fixy::Tagged<const crucible::TraceRing::Entry\\*, fixy::tags::vessel_trust::Validated>, crucible::TensorMeta\\*, int\\)."
  "crucible::Vigil::dispatch_op_pure\\(const crucible::VigilFgCtx&, crucible::TraceRing::ValidatedEntryPtr, ")

# BackgroundThread::run_in_row(ctx) requires CtxAdmits<Ctx, Row<Bg,
# Alloc, IO, Block>>.  Each fixture hands in a context whose row lacks
# one required atom or more.  The first regex pins the refused context,
# the second pins the gate and the row it demands.
set(BG_RUN_GATE "required for the satisfaction of .CtxAdmits<Ctx, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >.")
set(BG_RUN_CALL "no matching function for call to .crucible::BackgroundThread::run_in_row\\(const foundation::effects::ExecCtx<")
crucible_neg_compile_test(neg_background_thread_run_in_row_empty
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_bg_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_alloc_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_io_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::IO> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_block_only
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_bg
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Alloc, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_alloc
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::IO, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_io
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::Block> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_missing_block
  "${BG_RUN_CALL}foundation::effects::Bg, foundation::effects::Row<foundation::effects::Effect::Bg, foundation::effects::Effect::Alloc, foundation::effects::Effect::IO> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_init_only
  "${BG_RUN_CALL}foundation::effects::Init, foundation::effects::Row<foundation::effects::Effect::Init> >&\\)" "${BG_RUN_GATE}")
crucible_neg_compile_test(neg_background_thread_run_in_row_test_only
  "${BG_RUN_CALL}foundation::effects::Test, foundation::effects::Row<foundation::effects::Effect::Test> >&\\)" "${BG_RUN_GATE}")

# The tier mints keep their direction: mint_promote goes up the chain
# only and mint_demote goes down only.  Each mint and mint_restore also
# refuses a payload that cannot move, and mint_restore refuses a cold
# band over another value type and a payload with no content hash
# projection, because without the projection the hash check would run
# for ContentHash alone.
crucible_neg_compile_test(neg_cipher_tier_promote_hot_to_cold
  "no matching function for call to .mint_promote<foundation::algebra::lattices::CipherTierTag::Hot, foundation::algebra::lattices::CipherTierTag::Cold>"
  "the expression .can_promote_tier_v<From, To> .with From = foundation::algebra::lattices::CipherTierTag::Hot. To = foundation::algebra::lattices::CipherTierTag::Cold.. evaluated to .false.")
crucible_neg_compile_test(neg_cipher_tier_promote_unmovable_payload
  "required for the satisfaction of .PromotableTier<From, To, T>. .with From = foundation::algebra::lattices::CipherTierTag::Cold. To = foundation::algebra::lattices::CipherTierTag::Warm. T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_test(neg_cipher_tier_demote_cold_to_hot
  "no matching function for call to .mint_demote<foundation::algebra::lattices::CipherTierTag::Cold, foundation::algebra::lattices::CipherTierTag::Hot>"
  "the expression .can_demote_tier_v<From, To> .with From = foundation::algebra::lattices::CipherTierTag::Cold. To = foundation::algebra::lattices::CipherTierTag::Hot.. evaluated to .false.")
crucible_neg_compile_test(neg_cipher_tier_demote_unmovable_payload
  "required for the satisfaction of .DemotableTier<From, To, T>. .with From = foundation::algebra::lattices::CipherTierTag::Hot. To = foundation::algebra::lattices::CipherTierTag::Warm. T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_test(neg_cipher_tier_restore_unmovable_payload
  "required for the satisfaction of .RestorableTier<T>. .with T = ImmovablePayload."
  "required for the satisfaction of .move_constructible<T>. .with T = ImmovablePayload.")
crucible_neg_compile_test(neg_cipher_tier_restore_wrong_value_type
  "could not convert .std::declval<foundation::algebra::Graded<foundation::algebra::ModalityKind::Absolute, foundation::algebra::lattices::CipherTierLattice::At<foundation::algebra::lattices::CipherTierTag::Cold>, int> >"
  "from .Graded<.....,.....,int>. to .Graded<.....,.....,crucible::ContentHash>.")
crucible_neg_compile_test(neg_cipher_tier_restore_no_hash_projection
  "no matching function for call to .mint_restore<ProductionPayload>"
  "required for the satisfaction of .RestorableHashed<T>. .with T = ProductionPayload.")

# State-discipline: SealedView cannot satisfy the MutableView parameter
# of register_name / register_op — the only typed registration path
# requires proof that the table is in the Mutable phase.
#
# [GCC-WRAPPER-TEXT] Production-side typed-view rejection.  The direct
# signature requires MutableView; SealedView reaches GCC's conversion
# diagnostic before any runtime path can exist.
crucible_neg_compile_test(neg_schema_register_with_sealed_view
  "cannot convert .sv. \\(type .fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Sealed>.\\) to type .const crucible::SchemaTable::MutableView&."
  "neg_schema_register_with_sealed_view.cpp:14")
crucible_neg_compile_test(neg_ckernel_register_with_sealed_view
  "cannot convert .fixy::ScopedView<crucible::CKernelTable, crucible::ckernel_state::Sealed>. to .const crucible::CKernelTable::MutableView&."
  "initializing argument 1 of .bool crucible::CKernelTable::register_op\\(const MutableView&, crucible::SchemaHash, crucible::CKernelId\\).")
# A field keeps the proof of a sealed view after the table is gone, so the
# field audit refuses it.
crucible_neg_compile_test(neg_ckernel_mint_sealed_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<SealedContainer>\\(\\). evaluates to false")

# HS14 floor for CKernelTable::mint_mutable_view.
# Paired fixtures pin the per-state lock at BOTH gates of the member
# mint.  The register_op fixture above pins only the inverse direction:
#   * _to_sealed_overload — value-level typed-overload rejection when
#     a freshly-minted MutableView is offered where SealedView is
#     required.  Dual of neg_ckernel_register_with_sealed_view: same
#     per-state lock, opposite direction.  [GCC-WRAPPER-TEXT]
#   * _in_field — structural Tier-2 field-storage audit reject when a
#     class declares CKernelTable::MutableView as a member.  Routes
#     through the same no_scoped_view_field_check audit that fires
#     for the Steady state of IterationDetector, and exercises the alias-stability path
#     (MutableView is a typedef of ScopedView<CKernelTable, Mutable>).
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_to_sealed_overload
  "invalid initialization of reference of type .const crucible::CKernelTable::SealedView&. .aka .const fixy::ScopedView<crucible::CKernelTable, crucible::ckernel_state::Sealed>&.. from expression of type .const fixy::ScopedView<crucible::CKernelTable, crucible::ckernel_state::Mutable>"
  "in passing argument 1 of .void requires_sealed_view\\(const crucible::CKernelTable::SealedView&\\).")
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingContainer>\\(\\). evaluates to false")

# Each carrier has its own ScopedView type, its own view_ok overload and its
# own no_scoped_view_field_check instance, so the kernel table fixtures do
# not see a defect in the schema table gate.  The HS14 scanner counts a
# fixture for the carrier whose class it names.  This pair is the schema
# table's own witness.
crucible_neg_compile_test(neg_schema_mint_mutable_view_to_sealed_overload
  "invalid initialization of reference of type .const crucible::SchemaTable::SealedView&."
  "from expression of type .const fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Mutable>.")
crucible_neg_compile_test(neg_schema_mint_mutable_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingSchemaContainer>\\(\\). evaluates to false")

# A sealed table mints no mutable view.  The view comes back optional, so
# a registration that forgets the seal does not compile.
crucible_neg_compile_test(neg_schema_mutable_view_unchecked
  "\\(type .std::optional<fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Mutable> >.\\) to type .const crucible::SchemaTable::MutableView&."
  "neg_schema_mutable_view_unchecked.cpp:12")
crucible_neg_compile_test(neg_ckernel_mutable_view_unchecked
  "cannot convert .std::optional<fixy::ScopedView<crucible::CKernelTable, crucible::ckernel_state::Mutable> >."
  "neg_ckernel_mutable_view_unchecked.cpp:12")

# Lock-free readers read a sealed table, so every write goes through the
# view and the seal phase.  The entry arrays are private, and clear(), which
# opens a sealed table again, takes the test context.
crucible_neg_compile_test(neg_schema_table_entries_private
  "is private within this context"
  "crucible::SchemaTable::entries_")
crucible_neg_compile_test(neg_ckernel_table_entries_private
  "is private within this context"
  "crucible::CKernelTable::entries_")
crucible_neg_compile_test(neg_schema_table_clear_needs_test_context
  "no matching function for call to"
  "crucible::SchemaTable::clear..")
crucible_neg_compile_test(neg_ckernel_table_clear_needs_test_context
  "no matching function for call to"
  "crucible::CKernelTable::clear..")

# The names and schemas in the two tables come from the ops that a Vigil
# records, so a mutable view, and a trace load that registers names, ask
# for the context of a Vigil's producer claim.
crucible_neg_compile_test(neg_schema_mint_mutable_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_schema_mint_mutable_view_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_schema_mint_mutable_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_schema_mint_mutable_view_other_brand.cpp:15")
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_ckernel_mint_mutable_view_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_ckernel_mint_mutable_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_ckernel_mint_mutable_view_other_brand.cpp:15")
crucible_neg_compile_test(neg_trace_loader_names_unbranded_ctx
  "no matching function for call to .load_trace.const fixy::TestRunnerCtx&, foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >"
  "to type .const crucible::VigilFgCtx&."
  "neg_trace_loader_names_unbranded_ctx.cpp:12")
crucible_neg_compile_test(neg_trace_loader_names_other_brand
  "no matching function for call to .load_trace.const fixy::TestRunnerCtx&, foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >"
  "to type .const crucible::VigilFgCtx&."
  "neg_trace_loader_names_other_brand.cpp:16")
# A trace load reads a file, so it asks for a context that owns IO and
# Block.  The foreground context of a Vigil owns neither.
crucible_neg_compile_test(neg_trace_loader_refuses_foreground_io
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
crucible_neg_compile_test(neg_crucible_context_mint_compiled_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::CrucibleContext, crucible::ctx_mode::Compiled>.")
crucible_neg_compile_test(neg_crucible_context_mint_compiled_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingCtxContainer>\\(\\). evaluates to false")

crucible_neg_compile_test(neg_pool_allocator_mint_initialized_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::PoolAllocator, crucible::pool_state::Initialized>.")
crucible_neg_compile_test(neg_pool_allocator_mint_initialized_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingPoolContainer>\\(\\). evaluates to false")

crucible_neg_compile_test(neg_replay_engine_mint_active_view_cross_carrier
  "invalid initialization of reference of type .const crucible::CKernelTable::MutableView&."
  "from expression of type .fixy::ScopedView<crucible::ReplayEngine, crucible::engine_state::Active>.")
crucible_neg_compile_test(neg_replay_engine_mint_active_view_in_field
  "static assertion failed: Type contains a fixy::ScopedView<> in some field"
  "! fixy::contains_scoped_view<OffendingEngineContainer>\\(\\). evaluates to false")

# The foreground views are read only by the thread that holds the claim
# of the Vigil, so each gate asks for the context of the Vigil's brand.
# A context with no brand and a context of another state are refused.
crucible_neg_compile_test(neg_crucible_context_compiled_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_crucible_context_compiled_view_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_crucible_context_compiled_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_crucible_context_compiled_view_other_brand.cpp:15")
crucible_neg_compile_test(neg_replay_engine_active_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_replay_engine_active_view_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_replay_engine_active_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_replay_engine_active_view_other_brand.cpp:15")
crucible_neg_compile_test(neg_pool_allocator_initialized_view_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_pool_allocator_initialized_view_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_pool_allocator_initialized_view_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_pool_allocator_initialized_view_other_brand.cpp:15")
crucible_neg_compile_test(neg_vigil_output_ptr_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_output_ptr_unbranded_ctx.cpp:11")
# ring() and meta_log() hand out the producer surface of the ring and of the
# metadata log, so each asks for the context of the Vigil's producer claim.
crucible_neg_compile_test(neg_vigil_ring_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_ring_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_vigil_ring_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_ring_other_brand.cpp:15")
crucible_neg_compile_test(neg_vigil_meta_log_unbranded_ctx
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::Fg, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_meta_log_unbranded_ctx.cpp:11")
crucible_neg_compile_test(neg_vigil_meta_log_other_brand
  "cannot convert .foundation::effects::ExecCtx<foundation::effects::ctx_cap::BrandedFg<.anonymous.::Stranger>, foundation::effects::Row<> >. to .const crucible::VigilFgCtx&."
  "neg_vigil_meta_log_other_brand.cpp:15")
crucible_neg_compile_test(neg_vigil_mint_producer_context_const
  "passing .const crucible::Vigil. as .this. argument discards qualifiers"
  "neg_vigil_mint_producer_context_const.cpp:10")
crucible_neg_compile_test(neg_vigil_producer_context_to_unbranded_gate
  "invalid initialization of reference of type .const fixy::HotFgCtx&."
  "from expression of type .crucible::VigilFgCtx."
  "neg_vigil_producer_context_to_unbranded_gate.cpp:17")

# [GCC-WRAPPER-TEXT] Same class as schema/ckernel above — production-
# side overload-set rejection in Cipher.  Same accepted-intrinsic
# rationale.
crucible_neg_compile_test(neg_cipher_wrong_tag
  "cannot convert .fixy::ScopedView<crucible::SchemaTable, crucible::schema_state::Sealed>. to .const crucible::Cipher::OpenView&."
  "aka .const fixy::ScopedView<crucible::Cipher, crucible::cipher_state::Open>&.")

# deserialize_federation_entry takes the peer token of the reading
# organization.  A call with no token, or with the token of another
# organization, does not compile.
crucible_neg_compile_test(neg_federation_deserialize_without_permission
    "no matching function for call to .deserialize_federation_entry\\(std::array<unsigned char, 32>&, uint16_t\\)."
    "deserialize_federation_entry\\(const foundation::permissions::Permission<foundation::permissions::tag::FederatedPeer<Org>, Brand>&,")
crucible_neg_compile_test(neg_federation_deserialize_wrong_org_permission
    "no matching function for call to .deserialize_federation_entry<OrgB>\\(foundation::permissions::Permission<foundation::permissions::tag::FederatedPeer<OrgA>,"
    "mismatched types .OrgB. and .OrgA.")

crucible_neg_compile_test(neg_vigil_copy_attempt
    "use of deleted function .crucible::Vigil::Vigil\\(const crucible::Vigil&\\)."
    "Vigil owns the runtime organism. not copyable")

# Vigil::AlignmentPos is fixy::Refined<bounded_above<5>, uint8_t>, and
# Vigil::alignment_pos_at is its one door.  A constant above the bound
# stops the constant evaluation of the mint.  The first fixture passes 6,
# one past the bound.  The second passes 255, the largest byte.
crucible_neg_compile_test(neg_vigil_alignment_pos_above_max
    "in .constexpr. expansion of .crucible::Vigil::alignment_pos_at\\(6\\)."
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<5>\\(\\), unsigned char>\\(pos\\)."
    "error: .__builtin_trap\\(\\). is not a constant expression")
crucible_neg_compile_test(neg_vigil_alignment_pos_uint8_max
    "in .constexpr. expansion of .crucible::Vigil::alignment_pos_at\\(255\\)."
    "in .constexpr. expansion of .fixy::mint_refined<BoundedAbove<5>\\(\\), unsigned char>\\(pos\\)."
    "error: .__builtin_trap\\(\\). is not a constant expression")

# ── PermissionedMetaLog role-discrimination tests ─────────────────
#
# MetaLog is append-only on the foreground side and drain-only on the
# background side.  The permissioned facade makes that distinction
# structural rather than convention-based.
crucible_neg_compile_test(neg_metalog_producer_drain
    "has no member named .try_drain_one."
    ".class crucible::PermissionedMetaLog<.anonymous.::Tag, foundation::permissions::<lambda\\(\\)> >::ProducerHandle. has no member")

crucible_neg_compile_test(neg_metalog_consumer_append
    "has no member named .try_append."
    ".class crucible::PermissionedMetaLog<.anonymous.::Tag, foundation::permissions::<lambda\\(\\)> >::ConsumerHandle. has no member")

# HS14 for the two MetaLog session mints.  Each gate is one concept, and
# each fixture fails one clause of it: the context, the handle passed as
# an lvalue, a type that is not a permissioned log, and the handle of the
# other role.
crucible_neg_compile_test(neg_metalog_session_producer_not_a_context
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int."
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_producer_lvalue_handle
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .same_as<Handle, typename Log::ProducerHandle>. .with Handle = crucible::PermissionedMetaLog<producer_lvalue_fixture::Tag, foundation::permissions::\\._anon_[0-9]+>::ProducerHandle&"
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_consumer_not_a_log
    "no matching function for call to .mint_metalog_consumer_session<int>"
    "satisfaction of .MetaLogSessionSurface<Log>. .with Log = int."
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_consumer_wrong_role
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .same_as<Handle, typename Log::ConsumerHandle>. .with Handle = crucible::PermissionedMetaLog<consumer_wrong_role_fixture::Tag, foundation::permissions::\\._anon_[0-9]+>::ProducerHandle"
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_producer_not_a_log
    "no matching function for call to .mint_metalog_producer_session<int>"
    "satisfaction of .MetaLogSessionSurface<Log>. .with Log = int."
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_consumer_not_a_context
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .IsExecCtx<Ctx>. .with Ctx = int."
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_consumer_lvalue_handle
    "no matching function for call to .mint_metalog_consumer_session<"
    "satisfaction of .same_as<Handle, typename Log::ConsumerHandle>. .with Handle = crucible::PermissionedMetaLog<consumer_lvalue_fixture::Tag, foundation::permissions::\\._anon_[0-9]+>::ConsumerHandle&"
    "satisfaction of .CtxFitsMetaLogConsumerSession<Ctx, Log, Handle>.")
crucible_neg_compile_test(neg_metalog_session_producer_wrong_role
    "no matching function for call to .mint_metalog_producer_session<"
    "satisfaction of .same_as<Handle, typename Log::ProducerHandle>. .with Handle = crucible::PermissionedMetaLog<producer_wrong_role_fixture::Tag, foundation::permissions::\\._anon_[0-9]+>::ConsumerHandle"
    "satisfaction of .CtxFitsMetaLogProducerSession<Ctx, Log, Handle>.")

# The accessor that makes each handle takes the Permission of its own
# role, and that parameter type is its whole admission.  Each fixture
# passes the Permission of the other role.
crucible_neg_compile_test(neg_metalog_producer_wrong_permission
    "cannot convert .*Permission<crucible::metalog_tag::Consumer<producer_wrong_permission_fixture::Tag>, foundation::permissions::<lambda\\(\\)> >.* to .foundation::permissions::Permission<crucible::metalog_tag::Producer<producer_wrong_permission_fixture::Tag>, foundation::permissions::<lambda\\(\\)> >&&"
    "initializing argument 1 of .*PermissionedMetaLog<UserTag, Brand>::producer.foundation::permissions::Permission<crucible::metalog_tag::Producer<UserTag>, Brand>&&. .with UserTag = producer_wrong_permission_fixture::Tag. Brand = foundation::permissions::<lambda\\(\\)>.")
crucible_neg_compile_test(neg_metalog_consumer_wrong_permission
    "cannot convert .*Permission<crucible::metalog_tag::Producer<consumer_wrong_permission_fixture::Tag>, foundation::permissions::<lambda\\(\\)> >.* to .foundation::permissions::Permission<crucible::metalog_tag::Consumer<consumer_wrong_permission_fixture::Tag>, foundation::permissions::<lambda\\(\\)> >&&"
    "initializing argument 1 of .*PermissionedMetaLog<UserTag, Brand>::consumer.foundation::permissions::Permission<crucible::metalog_tag::Consumer<UserTag>, Brand>&&. .with UserTag = consumer_wrong_permission_fixture::Tag. Brand = foundation::permissions::<lambda\\(\\)>.")

# SingletonInitGraph.h: the gate refuses a cycle, and it refuses an edge
# to a singleton that the registry does not hold.
crucible_neg_compile_test(neg_singleton_init_graph_cycle
    "template constraint failure for .template<class Registry, class Edges>  requires  AcyclicInitGraph<Registry, Edges> struct crucible::singleton_init_graph::init_graph."
    "the expression .\\(is_acyclic_<Registry>\\)\\(static_cast<Edges\\*>\\(nullptr\\)\\) \\[with Registry = std::tuple<fixy::atom::global::singleton<neg_singleton_init_graph_cycle_types::first_tag>.*\\]. evaluated to .false.")
crucible_neg_compile_test(neg_singleton_init_graph_unregistered_edge
    "template constraint failure for .template<class Registry, class Edges>  requires  AcyclicInitGraph<Registry, Edges> struct crucible::singleton_init_graph::init_graph."
    "required for the satisfaction of .EdgesNameRegisteredSingletons<Registry, Edges>. \\[with Registry = std::tuple<fixy::atom::global::singleton<neg_singleton_init_graph_unregistered_edge_types::registered_tag> >.*\\]"
    "the expression .\\(edges_name_registered_<Registry>\\)")

# ─── Vessel ABI Tagged direction fence ────────────────────────────
#
# from_typed() takes TypedHandle = Tagged<Vigil*, source::ABIBoundary>
# only.  This fixture witnesses that calling it with any other source
# tag (here: source::External) is rejected by the type system, so a
# refactor "promoting" the helper to accept any Tagged<Vigil*, *> would
# turn red here.
crucible_neg_compile_test(neg_vessel_typed_handle_wrong_tag
    "could not convert .wrong_tag. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
    "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::ABIBoundary>.")

# ─── Vessel ABI typed-meta direction fence ────────────────────────
#
# The matching neg-compile witness for as_meta_typed / metas_from_typed
# (the helper that carries the TensorMeta cast beside the Vigil
# handle).  Pins that wrong-tag values cannot
# launder as ABIBoundary-tagged metas — same architectural rationale
# as the handle fence above, applied to the meta-array entry point.
crucible_neg_compile_test(neg_vessel_typed_meta_wrong_tag
    "could not convert .wrong_tag. from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
    "to .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::ABIBoundary>.")

# ─── Vessel typed-accessor provenance fences ──────────────────────
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
    "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>."
    "to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Sanitized>. requested")
crucible_neg_compile_test(neg_schema_name_typed_wrong_tag
    "conversion from .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::Interned>. to non-scalar type .Tagged<\\[\\.\\.\\.\\],fixy::tags::source::External>. requested"
    "neg_schema_name_typed_wrong_tag.cpp:25")

# These fixtures pin crucible::live_intervals_disjoint_at<MaxLive>,
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
    "neg_memory_plan_live_intervals_overlap.cpp:[0-9]+:[0-9]+: error: static assertion failed"
    "static assertion failed: MemoryPlan invariant must hold at op 3")
crucible_neg_compile_test(neg_memory_plan_live_intervals_max_overflow
    "neg_memory_plan_live_intervals_max_overflow.cpp:[0-9]+:[0-9]+: error: static assertion failed"
    "static assertion failed: MaxLive=2 must accommodate live count")

# DispatchResult::compiled_status and compiled_op_index check
# `action == COMPILED` with CRUCIBLE_PRE.  The two accessors are constexpr,
# so the constant evaluation of each fixture enters the accessor and stops
# at that check.  The first regex names the accessor that the evaluation
# entered, and the second names the CRUCIBLE_PRE of CrucibleContext.h.
crucible_neg_compile_test(neg_dispatch_result_compiled_status_on_record
    "in .constexpr. expansion of .result.crucible::DispatchResult::compiled_status\\(\\)."
    "crucible/CrucibleContext.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")
crucible_neg_compile_test(neg_dispatch_result_compiled_op_index_on_record
    "in .constexpr. expansion of .result.crucible::DispatchResult::compiled_op_index\\(\\)."
    "crucible/CrucibleContext.h:[0-9]+:[0-9]+: note: in expansion of macro .CRUCIBLE_PRE.")

# Cipher::open takes the root as fixy::Path<fixy::tags::source::External>,
# so every operator-supplied path is tagged at the boundary, and open runs
# the sanitizer itself.  It refuses a bare string and a path that a caller
# tagged Sanitized.
crucible_neg_compile_test(neg_cipher_open_rejects_bare_string
    "no matching function for call to .crucible::Cipher::open.const fixy::TestRunnerCtx&, const char .35.."
    "to type .fixy::Path<fixy::tags::source::External>.")
crucible_neg_compile_test(neg_cipher_open_rejects_sanitized_input
    "no matching function for call to .crucible::Cipher::open.const fixy::TestRunnerCtx&, std::remove_reference<fixy::Tagged<std::filesystem::__cxx11::path, fixy::tags::source::Sanitized>&>::type."
    "aka .fixy::Tagged<std::filesystem::__cxx11::path, fixy::tags::source::Sanitized>... to type .fixy::Path<fixy::tags::source::External>.")

# Each fixture file under test/*_neg/ has a registration above.  A file with
# no registration never compiles, so the gate that it names has no witness.
file(GLOB neg_fixture_sources LIST_DIRECTORIES false "${CMAKE_CURRENT_SOURCE_DIR}/*_neg/*.cpp")
get_property(neg_registered_sources DIRECTORY PROPERTY CRUCIBLE_NEG_REGISTERED_SOURCES)
set(neg_unregistered_sources ${neg_fixture_sources})
if(neg_registered_sources)
  list(REMOVE_ITEM neg_unregistered_sources ${neg_registered_sources})
endif()
if(neg_unregistered_sources)
  list(JOIN neg_unregistered_sources "\n  " neg_unregistered_text)
  message(FATAL_ERROR
    "These negative fixtures have no crucible_neg_compile_test registration, so no test compiles "
    "them:\n  ${neg_unregistered_text}\nRegister each one with the regexes of its rejection, or "
    "delete the file.")
endif()

# ── The substrate and its two guards ──────────────────────────────
#
# include/foundation/ and include/fixy/ are the canonical substrate.  Two
# guards hold it to its rules:
#
#   layer_boundary — foundation < fixy < crucible; a file names only its
#                    own layer and the layers below it.  No allowlist.
#   derived_pins   — a cardinality static_assert must have a derived
#                    operand; a literal pinned to itself cannot fire.
#   header_checks  — a header holds no self-test namespace, no
#                    namespace-scope static_assert, no static_assert in a
#                    function that is not a template, and no reflection
#                    walk outside a template.  Each check lives in the
#                    check file of its header under test/layer/checks,
#                    each such walk is lazy, and
#                    utils/scripts/header-checks-ledger.txt holds the items
#                    that remain.  Each item of the ledger gives a warning,
#                    and the counts can only decrease.
#   walk_units     — test/layer/walk-checks.txt lists each check file whose
#                    checks can call members_of on the reflection of a
#                    namespace that a header opens, and no other file.  The
#                    walk units of test/layer compile each listed file after
#                    each header that can add to such a namespace.
add_test(NAME layer_boundary_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-layer-boundary.py --self-test)
add_test(NAME layer_boundary
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-layer-boundary.py)
set_tests_properties(layer_boundary layer_boundary_self_test PROPERTIES
    LABELS "ci_guard;substrate"
    SKIP_RETURN_CODE 3)
add_test(NAME derived_pins_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-derived-pins.py --self-test)
add_test(NAME derived_pins
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-derived-pins.py)
set_tests_properties(derived_pins derived_pins_self_test PROPERTIES
    LABELS "ci_guard;substrate"
    SKIP_RETURN_CODE 3)
add_test(NAME header_checks_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-header-checks.py --self-test)
add_test(NAME header_checks
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-header-checks.py
          --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(header_checks header_checks_self_test PROPERTIES
    LABELS "ci_guard;substrate"
    SKIP_RETURN_CODE 3)
add_test(NAME walk_units_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-walk-units.py --self-test)
add_test(NAME walk_units
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-walk-units.py
          --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(walk_units walk_units_self_test PROPERTIES
    LABELS "ci_guard;substrate"
    SKIP_RETURN_CODE 3)

# The compile-time checks report each finding as a warning or an error, in the
# format of utils/scripts/check_report.py.  An error fails the check, and a
# warning does not.  ctest shows no output of a test that passes, so a check
# that gets --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR} also writes its
# warnings there, and utils/scripts/report-check-warnings.py prints them.
# utils/scripts/budgets.txt gives the thresholds of the checks that measure a
# quantity.  The top of this file sets CRUCIBLE_CHECK_WARNINGS_DIR.
add_test(NAME check_report_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check_report.py --self-test)
# report-check-warnings.py also adds the time totals of the build: the CPU time
# of the objects of all, of the fixtures and of the executable tests, against
# the rows total-compile-cpu, total-fixture-cpu and total-test-cpu.  A total
# gives a warning only.  The CI step "Check warnings" runs it after the tests.
add_test(NAME report_check_warnings_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/report-check-warnings.py --self-test)
set_tests_properties(check_report_self_test report_check_warnings_self_test PROPERTIES LABELS "ci_guard")

# heavy_includes — a header under include/ includes no heavy standard header.
# A header is heavy when its compile time alone is more than the time of
# <meta> alone times the factor of the row heavy-include of
# utils/scripts/budgets.txt.  utils/scripts/header-costs.txt holds the
# measured times.  Each include that the tree holds today is a row of
# utils/scripts/heavy-includes-ledger.txt and gives a warning.  A new one is
# an error.
add_test(NAME heavy_includes_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-heavy-includes.py --self-test)
add_test(NAME heavy_includes
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-heavy-includes.py
          --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(heavy_includes heavy_includes_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)

# no_unity_pch — no target compiles as a unity build or with a precompiled
# header, because the reflection walks of this tree see each header that a
# translation unit includes.  The check reads the compile database of this
# build and the CMake files of the tree.
add_test(NAME no_unity_pch_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-unity-pch.py --self-test)
add_test(NAME no_unity_pch
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-no-unity-pch.py
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(no_unity_pch no_unity_pch_self_test PROPERTIES LABELS "ci_guard")

# contract_semantic — the last contract semantic flag of each compile agrees
# with the define CRUCIBLE_CONTRACT_SEMANTIC_IGNORE.  GCC uses the last flag,
# and CRUCIBLE_PRE reads the define.  The configure step sees each scope but
# not the order of the final compile line, so the check reads the compile
# database of this build.
add_test(NAME contract_semantic_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-contract-semantic.py --self-test)
add_test(NAME contract_semantic
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-contract-semantic.py
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(contract_semantic contract_semantic_self_test PROPERTIES LABELS "ci_guard")

# padded_lists — the element type of a large fixed list has no padding bit.
# With -ftrivial-auto-var-init=zero, GCC writes one store for each padding
# hole of each element of a fixed list in an automatic object, with no loop.
# The check selects files from the parse tree, compiles one probe for each
# through ccache, and reads the counts and the padding with reflection.  The
# row padded-list of utils/scripts/budgets.txt gives the count.  Each padded
# list that the tree holds today is a row of utils/scripts/padded-lists-ledger.txt
# and gives a warning.  A new one is an error.
add_test(NAME padded_lists_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-padded-lists.py
          --build-dir ${CMAKE_BINARY_DIR} --self-test)
add_test(NAME padded_lists
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-padded-lists.py
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(padded_lists padded_lists_self_test PROPERTIES
    LABELS "ci_guard"
    SKIP_RETURN_CODE 3)

# reconfigure_noop — a second configure and the two builds after it compile
# nothing.  A ctest test cannot configure and build the tree that runs it, so
# the check itself is a step of the CI build job, after the build:
#     python3 utils/scripts/check-reconfigure-noop.py BUILD_DIR
# Only the self-test is a test.  It plants each defect in a scratch project.
add_test(NAME reconfigure_noop_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-reconfigure-noop.py --self-test
          --cmake ${CMAKE_COMMAND} --ninja ${CMAKE_MAKE_PROGRAM})
set_tests_properties(reconfigure_noop_self_test PROPERTIES LABELS "ci_guard")

# The test launcher (cmake/TestLauncher.cmake) runs in front of each test whose
# command is an executable target, and it holds the rows test-time and
# test-memory of utils/scripts/budgets.txt while the test runs.  CMake gives
# no launcher to a script test, so check-test-time.py reads the wall time of
# those tests from the JUnit report of a run:
#     ctest --test-dir BUILD_DIR -j16 --output-junit ctest-junit.xml
#     python3 utils/scripts/check-test-time.py --build-dir BUILD_DIR
# The CI build job runs it after the tests of each leg.  A test cannot measure
# the run that it is part of, so only the two self-tests are tests.
add_test(NAME test_launcher_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 -S ${CMAKE_SOURCE_DIR}/utils/scripts/test-launcher.py --self-test)
add_test(NAME test_time_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-test-time.py --self-test)
set_tests_properties(test_launcher_self_test test_time_self_test PROPERTIES LABELS "ci_guard")

# cmake/BuildLauncher.cmake puts utils/scripts/build-launcher.py in front of
# each compile and each link.  The launcher writes the cost of the step beside
# its output, fails a step over the memory error threshold that no ledger row
# admits, and stops a step at three times the time error threshold.
# build_launcher_test runs the launcher with planted tools, and with the real
# compiler and the real ccache.
add_test(NAME build_launcher_test
  COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/build_launcher_test.py --cxx ${CRUCIBLE_REAL_CXX})
set_tests_properties(build_launcher_test PROPERTIES LABELS "ci_guard")

# The cost checks of utils/scripts/check-compile-cost.py read the records of
# the launcher and the outputs of this build:
#   compile_cpu, compile_memory  the CPU time and the peak memory of each compile job
#   compile_instructions         the user instructions of each compile job, which hold the
#                                error level of a compile when its record has a count
#   link_time, link_memory       the CPU time and the peak memory of each link
#   function_size                the largest function of each object
#   object_text                  the machine code of each object
#   header_alone                 the bytes that each header of include/ includes alone,
#                                from the ninja dependency log of its sentinel
# A ledger row applies only to the build of its kind, and each check reads
# the kind from build-kind.txt of this build.
set(_compile_cost_ninja "")
if(CMAKE_GENERATOR MATCHES "Ninja")
  set(_compile_cost_ninja ${CMAKE_MAKE_PROGRAM})
endif()
foreach(_compile_cost_check compile-cpu compile-instructions compile-memory link-time link-memory function-size
        object-text header-alone)
  string(REPLACE "-" "_" _compile_cost_test "${_compile_cost_check}")
  add_test(NAME ${_compile_cost_test}
    COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-compile-cost.py --check ${_compile_cost_check}
            --build-dir ${CMAKE_BINARY_DIR} --ninja "${_compile_cost_ninja}"
            --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
  set_tests_properties(${_compile_cost_test} PROPERTIES LABELS "ci_guard" SKIP_RETURN_CODE 3)
endforeach()
add_test(NAME compile_cost_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-compile-cost.py --self-test)
set_tests_properties(compile_cost_self_test PROPERTIES LABELS "ci_guard")

# parse_total — the sum, over each object of the target all and each negative
# fixture, of the bytes that its compile reads, against the baseline of the
# build kind in utils/scripts/parse-total-ledger.txt (the row parse-total of
# utils/scripts/budgets.txt gives the levels).  utils/scripts/build_census.py
# reads the files of an object from the ninja dependency log and the files of a
# fixture from the record that its driver writes after each run.  The check
# exits 3 for a build that made only some targets and for a build whose
# fixtures did not run.  Each fixture registration appends its name to the
# global property CRUCIBLE_NEG_FIXTURE_TESTS, and a call at the end of the top
# directory makes the check DEPEND on each of them, so that ctest runs the
# check after the last fixture of the run.
add_test(NAME parse_total
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-cost.py --check parse-total
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
add_test(NAME parse_cost_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-cost.py --self-test)
# header_fanout — for each header of include/, the bytes that its sentinel
# includes alone times the units that read it, against the 30 largest
# products of utils/scripts/header-fanout-ledger.txt.  It prints the ten
# largest products, and it gives warnings only: for a header that enters the
# ten largest, and for a product that changed by more than the row
# header-fanout of utils/scripts/budgets.txt.
add_test(NAME header_fanout
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-cost.py --check header-fanout
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
# instruction_total — the user instructions of the last real compile of each
# object of all and each negative fixture, against the baseline of
# utils/scripts/instruction-total-ledger.txt.  It gives warnings only, and it
# exits 3 when one compile has no exact instruction count.
add_test(NAME instruction_total
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-cost.py --check instruction-total
          --build-dir ${CMAKE_BINARY_DIR} --warnings-dir ${CRUCIBLE_CHECK_WARNINGS_DIR})
set_tests_properties(parse_total parse_cost_self_test header_fanout instruction_total PROPERTIES
    LABELS "ci_guard" SKIP_RETURN_CODE 3)
function(crucible_order_census_checks)
  get_property(_fixtures GLOBAL PROPERTY CRUCIBLE_NEG_FIXTURE_TESTS)
  set_tests_properties(parse_total header_fanout instruction_total DIRECTORY ${CMAKE_SOURCE_DIR}/test PROPERTIES
      DEPENDS "${_fixtures}")
endfunction()
cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL crucible_order_census_checks)

# affected_tests_self_test — utils/scripts/run-affected-tests.py builds a tree
# and runs only the tests whose inputs changed since their last pass.  It is
# the test command of the inner loop (CLAUDE.md §XV).  CI and the checks before
# a landing run the full suite.  The self-test plants each kind of input in a
# scratch tree with a CTestTestfile.cmake that it writes.
add_test(NAME affected_tests_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/run-affected-tests.py --self-test
          --ctest ${CMAKE_CTEST_COMMAND})
set_tests_properties(affected_tests_self_test PROPERTIES LABELS "ci_guard")

# vessel_acceptance is the acceptance gate of the PyTorch vessel under
# vessel/torch.  utils/scripts/check-vessel-acceptance.py builds the vessel targets
# and accepts the vessel of this build only when the vessel compiles against a
# PyTorch with DispatchKey::Crucible at -Werror, each vessel/torch/test_*.py
# exits 0, and replay gives the bytes of PyTorch alone.  The self-test needs no
# PyTorch, so it is a guard of every build.
#
# A build that cannot operate the gate does not get a test that passes or
# skips.  It gets vessel_acceptance_unavailable, which ctest shows as
# "Not Run (Disabled)" with the others that did not run.  A build that must
# operate the gate sets CRUCIBLE_REQUIRE_VESSEL, and the configure step then
# stops with the reason.  CRUCIBLE_VESSEL_PYTHON is the interpreter of the
# PyTorch that TORCH_DIR names.  The default is the interpreter of the
# virtual environment that holds TORCH_DIR, when one exists.
add_test(NAME vessel_acceptance_self_test
  COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-vessel-acceptance.py --self-test)
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
    COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-vessel-acceptance.py
            --build-dir ${CMAKE_BINARY_DIR} --torch-dir ${TORCH_DIR} --python ${CRUCIBLE_VESSEL_PYTHON})
  # The gate builds the vessel targets in this build directory, so no other
  # test operates beside it.  It invokes a script, so it carries ci_guard, and
  # `ctest -L ci_guard` operates it in a build that has the vessel.
  set_tests_properties(vessel_acceptance PROPERTIES LABELS "ci_guard;vessel" TIMEOUT 3600 RUN_SERIAL TRUE)
endif()

# allowlist_keys runs over every utils/scripts/*allowlist*.txt and holds the one
# property none of the owning guards can hold once it breaks: the path half
# of each key names a file that exists.  When a file moves, a key on the
# missing path exempts nothing AND the owning guard can no longer locate
# the site it was written for, so the guard goes quiet rather than loud.
add_test(NAME allowlist_keys_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-allowlist-keys.sh --self-test)
add_test(NAME allowlist_keys
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-allowlist-keys.sh)
set_tests_properties(allowlist_keys allowlist_keys_self_test PROPERTIES
    LABELS "ci_guard;allowlist")

# atom_roster_joined supplies the one thing fixy/Collision.h cannot supply
# for itself: a vantage point that has seen every header.  The relation is
# in the header — roster_declared_but_not_joined<Site>() and
# sample_set_wrongly_joined<Site>() — and members_of answers as of the
# point where a walk is instantiated, so a walk written in a header can
# never see a roster declared by a header included after it.  The script
# generates a sentinel over every header under include/fixy/, from the
# directory rather than from a list, and instantiates the same two
# templates from its own tag.
add_test(NAME atom_roster_joined_self_test
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-atom-roster-joined.sh --self-test)
add_test(NAME atom_roster_joined
  COMMAND bash ${CMAKE_SOURCE_DIR}/utils/scripts/check-atom-roster-joined.sh --quiet)
set_tests_properties(atom_roster_joined atom_roster_joined_self_test PROPERTIES
  ENVIRONMENT "CXX=${CMAKE_CXX_COMPILER}"
  LABELS "ci_guard;substrate")

# parse_clean owns the one question every AST gate depends on: does the file
# parse at all.  The AST gates read a tree-sitter parse from the pinned kit
# rather than scan text, so a file that does not parse is a silent hole in all
# of them at once.  The gate fails in three directions — an unadmitted parse
# error, a roster entry whose file parses clean now, and a roster entry naming
# a path that no longer exists — so the roster in utils/scripts/tsast.py cannot rot.
#
# Exit 3 (the pinned kit is absent) is a SKIP here.  A developer who has not
# run utils/scripts/install-tree-sitter.sh gets the instruction rather than a
# failure.  CI installs the kit in both the guards job and the build job, so CI
# always enforces.
add_test(NAME parse_clean_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-clean.py --self-test)
add_test(NAME parse_clean
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/check-parse-clean.py)
add_test(NAME tsast_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/tsast.py --self-test)
# mintmodel derives the §XXI mint surface from the parse tree, for the guard and
# for the inventory. Its self-test plants one fixture carrying every shape the
# model must tell apart, and four of its cases are negative controls: a deleted
# overload, a constructor of a class whose name starts with mint_, a
# trailing-underscore helper, and a friend re-declaration that must not count
# as a second site.
add_test(NAME mintmodel_self_test
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/scripts/mintmodel.py --self-test)
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
  COMMAND ${CMAKE_COMMAND} -E env python3 ${CMAKE_SOURCE_DIR}/utils/tools/mutation/run.py selftest
          --compiler ${CMAKE_CXX_COMPILER} ${_mutation_ninja})
set_tests_properties(mutation_self_test PROPERTIES LABELS "ci_guard;ast" SKIP_RETURN_CODE 3)

# Each guard test of this directory starts on a full tree cache.  Eight setup
# tests parse the C++ files of the tree, each one the files of its shard, with
# their macro bodies (THE SHARDED FILL in utils/scripts/tsast.py).  Without
# them, each guard that starts cold parses each file again.  ctest adds the
# setup tests to a run that selects one guard test.  This block reads the
# tests of the directory, so it stays after the last guard test.
set(CRUCIBLE_TREE_SHARDS 8)
math(EXPR crucible_last_tree_shard "${CRUCIBLE_TREE_SHARDS} - 1")
foreach(crucible_tree_shard RANGE ${crucible_last_tree_shard})
  add_test(NAME tsast_tree_fill_${crucible_tree_shard}
           COMMAND python3 ${CMAKE_SOURCE_DIR}/utils/scripts/tsast.py
                   --fill --shard ${crucible_tree_shard} --of ${CRUCIBLE_TREE_SHARDS})
  set_tests_properties(tsast_tree_fill_${crucible_tree_shard} PROPERTIES
      LABELS "ci_guard"
      FIXTURES_SETUP tsast_trees
      SKIP_RETURN_CODE 3)
endforeach()
get_property(crucible_directory_tests DIRECTORY PROPERTY TESTS)
foreach(crucible_guard_test IN LISTS crucible_directory_tests)
  get_test_property(${crucible_guard_test} LABELS crucible_guard_labels)
  get_test_property(${crucible_guard_test} FIXTURES_SETUP crucible_guard_setup)
  if("ci_guard" IN_LIST crucible_guard_labels AND NOT crucible_guard_setup)
    set_property(TEST ${crucible_guard_test} APPEND PROPERTY FIXTURES_REQUIRED tsast_trees)
  endif()
endforeach()

add_subdirectory(foundation)
add_subdirectory(fixy)
add_subdirectory(session_oracle)
add_subdirectory(ct)
add_subdirectory(layer)

# The fuzz harnesses (opt-in).  test/fuzz/README.md tells how to run them.
#   ctest -L fuzz_property  the property tests on Philox-derived input
#   ctest -L fuzz_boundary  the boundary harnesses, on their seeds and mutated copies
# AFL++ with its GCC plugin drives the same boundary harnesses for a
# coverage-guided campaign (test/fuzz/run-afl.sh).
option(CRUCIBLE_FUZZ "Build property + boundary fuzz harnesses" OFF)
if(CRUCIBLE_FUZZ AND CRUCIBLE_TSAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(FATAL_ERROR "The fuzz harnesses build under ASan, and CRUCIBLE_TSAN puts ThreadSanitizer on every target.  Set CRUCIBLE_FUZZ to OFF in a ThreadSanitizer build.")
endif()
if(CRUCIBLE_FUZZ)
  add_subdirectory(fuzz)
endif()
