// ── neg_fixy_resources_hot_with_io (FIXY-G12 HS14) ───────────────────
//
// Pins R019: hot-path bindings cannot carry any IO operations.  A
// binding engaging `cg::bounded_io<1>` (one syscall allowed) fails
// the strictest hot-path-resources gate which demands bounded_io<0>.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/Rules.h>
#include <crucible/fixy/dim/Termination.h>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;
namespace cr = crucible::fixy::rule;

namespace {

using HotWithIo = cf::fn<int,
    cf::accept_default_strict_for<cd::Type>,
    cf::accept_default_strict_for<cd::Refinement>,
    cf::accept_default_strict_for<cd::Usage>,
    cf::accept_default_strict_for<cd::Effect>,
    cf::accept_default_strict_for<cd::Security>,
    cf::accept_default_strict_for<cd::Protocol>,
    cf::accept_default_strict_for<cd::Lifetime>,
    cf::accept_default_strict_for<cd::Provenance>,
    cf::accept_default_strict_for<cd::Trust>,
    cf::accept_default_strict_for<cd::Representation>,
    cf::accept_default_strict_for<cd::Observability>,
    cf::accept_default_strict_for<cd::Complexity>,
    cf::accept_default_strict_for<cd::Precision>,
    cf::accept_default_strict_for<cd::Space>,
    cf::accept_default_strict_for<cd::Overflow>,
    cf::accept_default_strict_for<cd::Mutation>,
    cf::accept_default_strict_for<cd::Reentrancy>,
    cf::accept_default_strict_for<cd::Size>,
    cf::accept_default_strict_for<cd::Version>,
    cf::accept_default_strict_for<cd::Staleness>,
    cg::terminating,
    cg::bounded_alloc<2048>,
    cg::bounded_io<1>>;             // ONE syscall — R019 rejects

// Sanity: terminating + bounded_alloc are satisfied.
static_assert(cf::is_terminating_v<HotWithIo>);
static_assert(cf::bounded_alloc_v<HotWithIo> == 2048);
static_assert(cf::bounded_io_v<HotWithIo> == 1);  // non-zero

// THE DISCIPLINE: R019 rejects bounded_io != 0 on hot path.  The
// static_assert INVERTS the predicate.
static_assert(cr::R019_hot_path_resources_v<HotWithIo>,
    "R019 fixture: a hot-path binding with bounded_io<1> declares "
    "ONE syscall is allowed.  R019 demands zero syscalls on the hot "
    "path.  Build red is the EXPECTED outcome.");

}  // namespace

int main() { return 0; }
