// The rows of jemalloc (THE UNITS of bench_alloc_scaling_rows.h).  Only a
// build with the pin of jemalloc compiles this unit.

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

void run_jemalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    run_allocator<JemallocAdapter>(spread, arguments, record);
}

}  // namespace bench_alloc
