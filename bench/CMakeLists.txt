# Benchmark targets — O3 + native, no sanitizers.
# Built separately from tests:
#   cmake --preset bench && cmake --build --preset bench
#   ./build-bench/bench/bench_smoke

function(crucible_bench name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE crucible)
  target_compile_options(${name} PRIVATE -O3 -march=native -mtune=native -fno-omit-frame-pointer)
endfunction()

crucible_bench(bench_smoke)
crucible_bench(bench_arena)
crucible_bench(bench_meta_log)
crucible_bench(bench_pool_allocator)
crucible_bench(bench_trace_ring)
crucible_bench(bench_region_cache)
crucible_bench(bench_dispatch)
crucible_bench(bench_swiss_ctrl)
crucible_bench(bench_expr_pool)
crucible_bench(bench_iteration_detector)
