# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_smoke

function(crucible_bench name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_compile_options(${name} PRIVATE
    -O3 -march=native -mtune=native -fno-omit-frame-pointer)
  target_compile_definitions(${name} PRIVATE CRUCIBLE_BENCH)
  # Opt out of -Werror: benches ignore nodiscard returns by design.
  set_target_properties(${name} PROPERTIES CRUCIBLE_NO_WERROR TRUE)
endfunction()

crucible_bench(bench_smoke)
crucible_bench(bench_arena)
crucible_bench(bench_meta_log)
crucible_bench(bench_pool_allocator)
crucible_bench(bench_trace_ring)
crucible_bench(bench_region_cache)
crucible_bench(bench_dispatch)
crucible_bench(bench_swiss_table)
crucible_bench(bench_expr_pool)
crucible_bench(bench_iteration_detector)
crucible_bench(bench_replay_engine)
crucible_bench(bench_merkle_dag)
crucible_bench(bench_phases)
crucible_bench(bench_saturate)
crucible_bench(bench_safety)
crucible_bench(bench_philox)
crucible_bench(bench_ckernel)
crucible_bench(bench_schema_table)
crucible_bench(bench_serialize)
crucible_bench(bench_graph)
crucible_bench(bench_reflect)
crucible_bench(bench_cipher)

# ── Visualization tool ────────────────────────────────────────────────
# Not a benchmark — a utility for rendering .crtrace traces as SVG.
# Usage: ./render_trace <trace.crtrace> [-o output.svg] [--blocks-only]
add_executable(render_trace render_trace.cpp)
target_link_libraries(render_trace PRIVATE crucible)
target_compile_options(render_trace PRIVATE -O2)
set_target_properties(render_trace PROPERTIES CRUCIBLE_NO_WERROR TRUE)
