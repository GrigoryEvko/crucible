// The compile-time checks of crucible/perf/detail/BpfLoader.h.

#include <crucible/perf/detail/BpfLoader.h>

#include <fixy/Ctx.h>

namespace crucible::perf::detail {

static_assert(sizeof(Fd) == sizeof(int), "Tagged<int, source::BpfMap> must be the same width as int.  The empty "
                                         "trust-lattice element must collapse under EBO.");

// The startup load context reads the tracepoint files and the identity of the
// process.  The cold init context stops at IO, and the file calls need Block.
static_assert(CtxFitsBpfLoad<::fixy::InitLoadCtx>);
static_assert(!CtxFitsBpfLoad<::fixy::ColdInitCtx>);

}  // namespace crucible::perf::detail
