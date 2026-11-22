// The rows of mimalloc (the units of bench_alloc_scaling_rows.h).  Only a
// build with the pin of mimalloc compiles this unit.

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

void run_mimalloc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    run_allocator<MimallocAdapter>(spread, arguments, record);
}

}  // namespace bench_alloc
