# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_arena
#
# The `bench-caps` target (declared below) applies three Linux
# capabilities to every bench binary so they can load the eBPF sense
# hub as an unprivileged user:
#
#   cap_bpf               — bpf_prog_load (tracepoint)
#   cap_perfmon           — perf_event_open on tracepoint IDs
#   cap_dac_read_search   — read /sys/kernel/tracing/events/*/id
#
# Usage: `cmake --build --preset bench --target bench-caps`
# See the target's own comment block for rationale + caveats.

# eBPF sense hub: previously compiled+embedded here as
# `crucible_bench_bpf`.  As of GAPS-004a (2026-05-03) the substrate
# moved to include/crucible/perf/SenseHub.h + src/perf/SenseHub.cpp,
# and is bundled into the production `crucible_perf` static library
# wired by the root CMakeLists.txt via cmake/CrucibleBpf.cmake.
# Bench targets get the loader transitively when they link
# `crucible` — no per-bench BPF wiring needed below.
#
# The CRUCIBLE_HAVE_BPF compile definition is set PUBLIC on the
# `crucible` target (1 if the substrate compiled in, 0 otherwise),
# so bench_harness.h's #if defined(CRUCIBLE_HAVE_BPF)... gate fires
# correctly for every bench target without re-declaring the macro
# below.
#
# `setcap` discovery (for the bench-caps target further down) is
# also moved into the cmake module, which exposes SETCAP_EXECUTABLE
# at the root scope.

function(crucible_bench name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_compile_options(${name} PRIVATE
    -O3 -march=native -mtune=native -fno-omit-frame-pointer)
  target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH)
  # The CRUCIBLE_HAVE_BPF compile definition (1 when libbpf+clang+xxd
  # are available, 0 otherwise) is set PUBLIC on the `crucible`
  # target by the root CMakeLists.txt and propagates here via the
  # link above.  Bench harness gates on it directly — no per-bench
  # macro definition needed.  The bench-caps target only applies to
  # binaries that actually load BPF programs, so registration still
  # gates on CRUCIBLE_HAVE_BPF.
  if (CRUCIBLE_HAVE_BPF)
    # Register for the bench-caps target — applied after all benches
    # are declared so we can batch them into one sudo invocation.
    set_property(GLOBAL APPEND PROPERTY CRUCIBLE_BENCH_CAP_TARGETS ${name})
  endif()
  # Opt out of -Werror: benches ignore nodiscard returns by design.
  set_target_properties(${name} PROPERTIES CRUCIBLE_NO_WERROR TRUE)

  # Asm dump (gitignored, opt-in via -DCRUCIBLE_DUMP_ASM=ON).
  crucible_dump_asm(${name})
endfunction()

# ── Migrated to the new bench::Run / Report / Compare API ─────────────
crucible_bench(bench_arena)
crucible_bench(bench_adaptive_scheduler)
crucible_bench(bench_augur_metrics_swmr)
crucible_bench(bench_cipher)
crucible_bench(bench_ckernel)
crucible_bench(bench_content_hash)
crucible_bench(bench_dispatch)
crucible_bench(bench_expr_pool)
crucible_bench(bench_graph)
crucible_bench(bench_iteration_detector)
crucible_bench(bench_kernel_cache)
crucible_bench(bench_merkle_dag)
crucible_bench(bench_meta_log)
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
crucible_bench(bench_region_cache)
crucible_bench(bench_replay_engine)
crucible_bench(bench_safety)
crucible_bench(bench_fusion)
crucible_bench(bench_saturate)
crucible_bench(bench_schema_table)
crucible_bench(bench_serialize)
crucible_bench(bench_simd)
crucible_bench(bench_smoke)
crucible_bench(bench_swiss_table)
crucible_bench(bench_trace_ring)
crucible_bench(bench_concurrent_queues)
crucible_bench(bench_concurrent_saturation)
crucible_bench(bench_auto_router)
crucible_bench(bench_auto_split)
crucible_bench(bench_auto_split_compare)
crucible_bench(bench_mpmc_saturation)
crucible_bench(bench_mpsc_contention)
crucible_bench(bench_permissioned_calendar_grid)
crucible_bench(bench_permissioned_zero_cost)
crucible_bench(bench_permissioned_session_handle)
crucible_bench(bench_spsc_session)
crucible_bench(bench_metalog_session)
crucible_bench(bench_chainedge_session)
crucible_bench(bench_swmr_session)
crucible_bench(bench_chaselev_session)
target_compile_definitions(bench_chaselev_session PRIVATE NDEBUG)
crucible_bench(bench_scheduler_policies)

# ── Visualization tool ────────────────────────────────────────────────
# Not a benchmark — a utility for rendering .crtrace traces as SVG.
# Usage: ./render_trace <trace.crtrace> [-o output.svg] [--blocks-only]
add_executable(render_trace render_trace.cpp)
target_link_libraries(render_trace PRIVATE crucible)
target_compile_options(render_trace PRIVATE -O2)
set_target_properties(render_trace PROPERTIES CRUCIBLE_NO_WERROR TRUE)

# ── bench-caps target ────────────────────────────────────────────────
#
# The bench binary needs three capabilities to load + attach the BPF
# sense hub as an unprivileged user:
#
#   CAP_BPF             — bpf_prog_load (tracepoint type, since 5.8)
#   CAP_PERFMON         — perf_event_open on tracepoint IDs
#   CAP_DAC_READ_SEARCH — read /sys/kernel/tracing/events/*/id (640 root:root).
#                         libbpf reads the tracepoint id file on every
#                         bpf_program__attach() and silently fails to
#                         attach without it; CAP_PERFMON alone does
#                         not override DAC on tracefs.
#
# This target batches `sudo setcap` over every bench binary in one
# command so you enter the password exactly once per rebuild:
#
#   cmake --build --preset bench --target bench-caps
#
# Caveat: the ELF xattr is destroyed on every re-link, so re-run this
# target after any rebuild. It's intentionally NOT a POST_BUILD step
# because that would demand sudo on every build.
#
# Security note: CAP_DAC_READ_SEARCH lets the binary read any file on
# the system, including otherwise-protected ones like /etc/shadow.
# Acceptable for dev binaries running your own bench code; don't
# setcap binaries you don't trust to the same extent.
# Fallback when setcap is absent (hardened containers, some CI images,
# cross-distro build hosts): invoke benches under `sudo ./bench_foo` so
# the eBPF loader runs as root and bypasses the capability check
# entirely. The SenseHub setup path accepts either route — runtime
# behaviour is identical either way.
get_property(_crucible_bench_cap_targets GLOBAL PROPERTY CRUCIBLE_BENCH_CAP_TARGETS)
if (CRUCIBLE_HAVE_BPF AND SETCAP_EXECUTABLE AND _crucible_bench_cap_targets)
  set(_crucible_bench_caps_commands
    COMMAND ${CMAKE_COMMAND} -E echo
      "Applying CAP_BPF+CAP_PERFMON to ${_crucible_bench_cap_targets}")
  foreach (_t IN LISTS _crucible_bench_cap_targets)
    list(APPEND _crucible_bench_caps_commands
      COMMAND sudo ${SETCAP_EXECUTABLE} cap_bpf,cap_perfmon,cap_dac_read_search=eip $<TARGET_FILE:${_t}>)
  endforeach()
  add_custom_target(bench-caps
    ${_crucible_bench_caps_commands}
    DEPENDS ${_crucible_bench_cap_targets}
    COMMENT "Applying BPF caps (sudo password required once, re-run after any rebuild)"
    VERBATIM)
endif()
