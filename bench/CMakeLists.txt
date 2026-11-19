# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_trace_ring
#
# A bench loads the eBPF sense hub only with three Linux capabilities:
#
#   cap_bpf               — bpf_prog_load (tracepoint)
#   cap_perfmon           — perf_event_open on tracepoint IDs
#   cap_dac_read_search   — read /sys/kernel/tracing/events/*/id
#
# `python3 utils/bin/loop bench` runs each bench through the launcher
# /usr/local/bin/crucible-caps, which gives them (utils/assay/loop/caps.py).

# eBPF sense hub: the substrate lives in include/crucible/perf/SenseHub.h
# and src/perf/SenseHub.cpp, and is bundled into the production
# `crucible_perf` static library wired by the root CMakeLists.txt via
# cmake/CrucibleBpf.cmake.
# Bench targets get the loader transitively when they link
# `crucible` — no per-bench BPF wiring needed below.
#
# The CRUCIBLE_HAVE_BPF compile definition is set PUBLIC on the
# `crucible` target (1 if the substrate compiled in, 0 otherwise),
# so bench_harness.h's #if defined(CRUCIBLE_HAVE_BPF)... gate fires
# correctly for every bench target without re-declaring the macro
# below.

# crucible_bench(NAME [CORES COUNT]) makes the bench NAME from NAME.cpp.  A
# bench whose threads run at the same time states their count with CORES.
# The definition CRUCIBLE_BENCH_CORES=COUNT then tells `loop bench` to claim
# that many isolated cores for each run, one for each thread, and the harness
# pins each thread to one of them (THE CORES OF A RUN of
# utils/assay/loop/bench.py).  A bench with no CORES gets one core.
function(crucible_bench name)
  cmake_parse_arguments(PARSE_ARGV 1 _bench "" "CORES" "")
  if(_bench_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "crucible_bench(${name}): the arguments '${_bench_UNPARSED_ARGUMENTS}' are not CORES COUNT")
  endif()
  add_executable(${name} ${name}.cpp)
  if(DEFINED _bench_CORES)
    if(NOT _bench_CORES MATCHES "^[1-9][0-9]*$")
      message(FATAL_ERROR "crucible_bench(${name}): CORES is '${_bench_CORES}', and it must be a count of at least one")
    endif()
    target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH_CORES=${_bench_CORES})
  endif()
  target_link_libraries(${name} PRIVATE crucible)
  # Each loop starts a line of 64 bytes, so a change of other code does not
  # move a timed loop inside its line.  With loops at 16 bytes, the roof of
  # bench_core_strided took 130 or 150 ns at L1 for one code, by the place of
  # its timed loop.
  target_compile_options(${name} PRIVATE
    -O3 -march=native -mtune=native -fno-omit-frame-pointer -falign-loops=64)
  target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH)
  # The CRUCIBLE_HAVE_BPF compile definition (1 when libbpf+clang+xxd
  # are available, 0 otherwise) is set PUBLIC on the `crucible`
  # target by the root CMakeLists.txt and propagates here via the
  # link above.  Bench harness gates on it directly — no per-bench
  # macro definition needed.  `loop bench` reads the cache entry
  # CRUCIBLE_HAVE_BPF, and it runs each bench of a build with BPF
  # through the launcher.
  # The folder marks the target as a bench in the CMake file API.
  # `loop bench` and `loop pgo` read the bench list from there, through
  # bench_targets() of utils/assay/loop/bench.py.
  set_target_properties(${name} PROPERTIES FOLDER crucible_bench)

  # Asm dump (gitignored, opt-in via -DCRUCIBLE_DUMP_ASM=ON).
  crucible_dump_asm(${name})
endfunction()

# crucible_bench_reference(TARGET TOOL DEFINITION SOURCE...) gives the bench
# TARGET a pinned source of the state of the art: the pin TOOL of
# utils/pins.txt, which `python3 utils/bin/setup install TOOL` puts into the
# tools directory (THE SOURCES OF THE BENCH of utils/assay/setup/install.py).
# The configure asks the install module for the directory src of the source,
# with no network.  When the source is at its pin, TARGET gets that
# directory as a system include directory, the compile definition
# DEFINITION=1 and each SOURCE, which holds the doors to the reference.  When
# the tools directory holds no source at the pin, the configure prints the
# install command, and TARGET builds with no DEFINITION and with no SOURCE, so
# the bench omits the rows of that reference.  Only the bench presets build
# a bench, so the product links none of the code of a reference.
function(crucible_bench_reference target tool definition)
  execute_process(
    COMMAND "${CRUCIBLE_PYTHON3}" -S "${CMAKE_SOURCE_DIR}/utils/bin/setup" install --print-bin "${tool}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE directory
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(NOT status EQUAL 0 OR directory STREQUAL "")
    message(STATUS "${target}: the pinned ${tool} is not installed, so the bench omits its rows.  Install it with: "
                   "python3 ${CMAKE_SOURCE_DIR}/utils/bin/setup install ${tool}")
    return()
  endif()
  target_include_directories(${target} SYSTEM PRIVATE "${directory}")
  target_compile_definitions(${target} PRIVATE ${definition}=1)
  target_sources(${target} PRIVATE ${ARGN})
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${directory}")
endfunction()

# ── Migrated to the new bench::Run / Report / Compare API ─────────────
crucible_bench(bench_meta_zero)
crucible_bench(bench_observe_metrics_swmr)
crucible_bench(bench_cipher)
crucible_bench(bench_ckernel)
crucible_bench(bench_content_hash)
crucible_bench(bench_core_atomic)
crucible_bench(bench_thread_local_ref)
crucible_bench(bench_core_containers)
crucible_bench(bench_core_region)
crucible_bench(bench_core_walk)
crucible_bench(bench_core_reduce)
crucible_bench(bench_core_sort)
# The stable reference fluxsort, and on x86-64 the vector roof x86-simd-sort:
# its AVX2 arm in a unit at x86-64-v3, and the arm of the host.
crucible_bench_reference(bench_core_sort fluxsort CRUCIBLE_BENCH_FLUXSORT bench_core_sort_fluxsort.cpp)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  crucible_bench_reference(bench_core_sort x86-simd-sort CRUCIBLE_BENCH_X86_SIMD_SORT bench_core_sort_simd.cpp
                           bench_core_sort_simd_avx2.cpp)
  set_source_files_properties(bench_core_sort_simd_avx2.cpp PROPERTIES COMPILE_OPTIONS -march=x86-64-v3)
endif()
# fp::fma at three levels in one program: two units at x86-64 and x86-64-v2
# run the exact form, and the main unit at the host runs the instruction.
# The flags of a source come after the flags of the target, so the last
# -march word of each unit is its own.  On aarch64 the three units compile
# for the host.
crucible_bench(bench_core_fma)
target_sources(bench_core_fma PRIVATE fma_bench_v1.cpp fma_bench_v2.cpp)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  set_source_files_properties(fma_bench_v1.cpp PROPERTIES COMPILE_OPTIONS -march=x86-64)
  set_source_files_properties(fma_bench_v2.cpp PROPERTIES COMPILE_OPTIONS -march=x86-64-v2)
endif()
crucible_bench(bench_core_strided)
# Nine threads: eight shards, each on its own core, and the measuring thread.
crucible_bench(bench_parallel_pointwise CORES 9)
# Rows of 1 to 8 threads, one on each core of one L3 group, and one core of a second group for the consumer of the
# spsc rows in another L3 group.  --cores 16 adds the row of 16 threads.  The pinned queues of Erik Rigtorp are the
# references of the two rings.
crucible_bench(bench_thread_scaling CORES 9)
crucible_bench_reference(bench_thread_scaling rigtorp-spscqueue CRUCIBLE_BENCH_RIGTORP_SPSC)
crucible_bench_reference(bench_thread_scaling rigtorp-mpmcqueue CRUCIBLE_BENCH_RIGTORP_MPMC)
crucible_bench(bench_decide_compile_time)
crucible_bench(bench_dispatch)
crucible_bench(bench_fp_control)
crucible_bench(bench_diag_emission)
crucible_bench(bench_iteration_detector)
crucible_bench(bench_kernel_cache)
crucible_bench(bench_keyed_hash)
# A bench compiles for the processor of the host, and the foundation library
# for the level of the tree, so the two name different target namespaces of
# the run-time arm of the keyed hash.  The bench compiles the source of the arm
# itself, so it measures the arm of its own target and links it.
target_sources(bench_keyed_hash PRIVATE "${CMAKE_SOURCE_DIR}/src/foundation/core/KeyedHash.cpp")
# The reference of the keyed hash of the base: the official BLAKE3, its C and
# its assembly of x86-64 in the static library blake3_reference.  This
# directory enables the C and ASM languages for that library alone, and only
# a bench preset reads this directory (CRUCIBLE_BENCH), so no product preset
# compiles a line of C.  The library takes no flag of the tree, as the
# official build takes none, apart from the control-flow protection that the
# rule artifacts asks of each object of an executable.  With that flag the
# compiler sets __CET__, and the official assembly then includes <cet.h> for
# its marks of IBT and SHSTK.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
  execute_process(
    COMMAND "${CRUCIBLE_PYTHON3}" -S "${CMAKE_SOURCE_DIR}/utils/bin/setup" install --print-bin blake3
    RESULT_VARIABLE _blake3_status
    OUTPUT_VARIABLE _blake3_directory
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if(_blake3_status EQUAL 0 AND NOT _blake3_directory STREQUAL "")
    enable_language(C ASM)
    add_library(blake3_reference STATIC
      "${_blake3_directory}/blake3.c"
      "${_blake3_directory}/blake3_dispatch.c"
      "${_blake3_directory}/blake3_portable.c"
      "${_blake3_directory}/blake3_sse2_x86-64_unix.S"
      "${_blake3_directory}/blake3_sse41_x86-64_unix.S"
      "${_blake3_directory}/blake3_avx2_x86-64_unix.S"
      "${_blake3_directory}/blake3_avx512_x86-64_unix.S")
    target_compile_options(blake3_reference PRIVATE $<$<COMPILE_LANGUAGE:C>:-O3> -fcf-protection=full)
    target_link_libraries(bench_keyed_hash PRIVATE blake3_reference)
  endif()
  crucible_bench_reference(bench_keyed_hash blake3 CRUCIBLE_BENCH_BLAKE3 bench_keyed_hash_blake3.cpp)
endif()
crucible_bench(bench_merkle_dag)
crucible_bench(bench_meta_log)
crucible_bench(bench_net_checks)
crucible_bench(bench_phases)
# Default trace for bench_phases: SD1.5 U-Net (36499 ops) — real-world
# production-scale baseline. 10k+ DAG ops is the minimum meaningful
# signal for the build_trace pipeline; resnet18 (1386) shows <100 µs
# noise, vit_b (~15k) is borderline. SD1.5's 3 ms full pipeline gives
# the phase breakdown clean percentages without sub-phase quantization.
target_compile_definitions(bench_phases PRIVATE
  CRUCIBLE_BENCH_DEFAULT_TRACE="${CMAKE_SOURCE_DIR}/vessel/torch/examples/traces/sd15_unet.crtrace")
crucible_bench(bench_perf_loader)
crucible_bench(bench_philox)
crucible_bench(bench_pool_allocator)
crucible_bench(bench_reflect)
# The reference rapidhash of the hash of the Protocol family.
crucible_bench_reference(bench_reflect rapidhash CRUCIBLE_BENCH_RAPIDHASH bench_reflect_rapidhash.cpp)
crucible_bench(bench_region_cache)
crucible_bench(bench_replay_engine)
crucible_bench(bench_safety)
crucible_bench(bench_saturate)
crucible_bench(bench_schema_table)
crucible_bench(bench_serialize)
crucible_bench(bench_simd)
crucible_bench(bench_smoke)
# A waiter and a writer on two cores: the wake latency of each spin wait and
# the cost of a spin for the writer of its line.
crucible_bench(bench_spin_wake CORES 2)
# The cell of 128 bits beside libatomic of the compiler, the reference of
# each atomic operation of 16 bytes, which GCC does not put in line.
crucible_bench(bench_wide_atomic CORES 2)
target_link_libraries(bench_wide_atomic PRIVATE atomic)
crucible_bench(bench_swiss_table)
crucible_bench(bench_trace_ring)
crucible_bench(bench_concurrent_queues)
crucible_bench(bench_concurrent_saturation)
crucible_bench(bench_pipeline_dispatch)
crucible_bench(bench_no_regression)
crucible_bench(bench_mpsc_contention)
crucible_bench(bench_permissioned_zero_cost)
# Rows of 2 and 4 threads, one on each core of one L3 group.
crucible_bench(bench_permissioned_channel CORES 4)
crucible_bench(bench_permissioned_session_handle)
crucible_bench(bench_spsc_session)
crucible_bench(bench_metalog_session)
crucible_bench(bench_swmr_session)

# ── The one gated latency number ─────────────────────────────────────
#
# Deliberately absent from CRUCIBLE_CONTRACT_IGNORE_TUS in the root
# CMakeLists.  The six TUs on that list compile with contracts erased,
# so none of them can say what the production Release build costs —
# which is the only question this target exists to answer.  It takes
# the Release default, `observe`, and measures the armed shape.
#
# Keep it off that list.
# Six threads: the producer, and the pipeline thread and the four stages of
# each Vigil.
crucible_bench(bench_record_leaf CORES 6)
# The baseline of the hardware tier of this host (cmake/HostTier.cmake).  A
# host of a tier with no baseline file measures, prints its numbers and passes.
# A host with no tier gets an empty path, with the same result.
set(_record_leaf_baseline "")
if(CRUCIBLE_HOST_TIER)
  set(_record_leaf_baseline "${CMAKE_CURRENT_SOURCE_DIR}/baselines/${CRUCIBLE_HOST_TIER}/record_leaf.json")
endif()
target_compile_definitions(bench_record_leaf PRIVATE
  CRUCIBLE_RECORD_LEAF_TIER="${CRUCIBLE_HOST_TIER}"
  CRUCIBLE_RECORD_LEAF_BASELINE="${_record_leaf_baseline}")

# The only `add_test` in bench/.  The rest of the tree is a compile
# gate; this one runs, compares against a committed ceiling and fails.
#
# The ceiling is the latency of a Release build with no instrumentation.
# Each configuration below measures a different program, so the test is
# registered there as disabled, and ctest lists it as not run with no red.
# The list names the configurations that do not apply, and every other
# configuration runs the gate, so a new latency configuration cannot skip it.
#
# The binary also decides for itself: a sample whose coefficient of
# variation is past the code guide's 5% bar reports SKIP and passes,
# so on a loaded box or a shared runner this reports rather than
# flaps.  A real regression on a quiet box is what turns it red.
#
# Pinning is left to the harness, which routes through
# warden::select_hot_cpu and already prefers an isolated core.
set(_record_leaf_not_measured "")
if(NOT CMAKE_BUILD_TYPE STREQUAL "Release")
  list(APPEND _record_leaf_not_measured "the build type is '${CMAKE_BUILD_TYPE}', not Release")
endif()
if(CRUCIBLE_UBSAN_STRICT)
  list(APPEND _record_leaf_not_measured "CRUCIBLE_UBSAN_STRICT puts UBSan on every target")
endif()
if(CRUCIBLE_PGO STREQUAL "generate")
  list(APPEND _record_leaf_not_measured "CRUCIBLE_PGO=generate instruments every branch")
endif()
if(CRUCIBLE_VERIFY)
  list(APPEND _record_leaf_not_measured "CRUCIBLE_VERIFY arms the contract families that Release leaves out")
endif()
add_test(NAME bench_record_leaf COMMAND bench_record_leaf)
set_tests_properties(bench_record_leaf PROPERTIES
  TIMEOUT 300
  ENVIRONMENT "CRUCIBLE_BENCH_JSON=0"
  LABELS "bench;latency")
if(_record_leaf_not_measured)
  string(JOIN ", and " _record_leaf_reasons ${_record_leaf_not_measured})
  message(STATUS "bench_record_leaf is disabled in this configuration, because ${_record_leaf_reasons}.")
  set_tests_properties(bench_record_leaf PROPERTIES DISABLED TRUE)
endif()

# The regression budget of the strided walks: the median time of each case
# over the median time of its roof in the same run, against a budget for
# each case.  A ratio does not change with the clock of the host.  The test
# skips a case whose run drifts by more than 5%.  It measures the code of a
# Release build, so each configuration that disables bench_record_leaf
# disables it too.
crucible_bench(bench_strided_budget)
add_test(NAME bench_strided_budget COMMAND bench_strided_budget)
set_tests_properties(bench_strided_budget PROPERTIES
  TIMEOUT 300
  ENVIRONMENT "CRUCIBLE_BENCH_JSON=0"
  LABELS "bench;latency")
if(_record_leaf_not_measured)
  set_tests_properties(bench_strided_budget PROPERTIES DISABLED TRUE)
endif()
