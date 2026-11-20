// The rows of snmalloc (THE UNITS of bench_alloc_scaling_rows.h).  Only a
// build with the pin of snmalloc compiles this unit.

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

void run_snmalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    run_allocator<SnmallocAdapter>(spread, arguments, record);
}

}  // namespace bench_alloc
