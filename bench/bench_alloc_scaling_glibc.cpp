// The rows of glibc (the units of bench_alloc_scaling_rows.h).

#include "bench_alloc_scaling_rows.h"

namespace bench_alloc {

void run_glibc_rows(bench::Spread const& spread, Arguments const& arguments, RunRecord& record) {
    run_allocator<GlibcAdapter>(spread, arguments, record);
}

}  // namespace bench_alloc
