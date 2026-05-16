// ── neg_fixy_resources_unbounded_recursion (FIXY-G12 HS14) ───────────
//
// Pins the structural inconsistency check: a binding cannot claim
// `cg::terminating` while engaging `cg::loop_bound<UINT64_MAX>` —
// UINT64_MAX is the "no bound declared" sentinel.  The grant-level
// static_assert in cg::loop_bound rejects on COMPLETE-TYPE
// instantiation.  A type alias alone is insufficient (only declares
// the type, doesn't instantiate the static-assert pass); accessing
// a member, taking sizeof, or using the type as a template argument
// forces the instantiation and fires the diagnostic.
//
// Build red is the EXPECTED outcome.

#include <crucible/fixy/Fixy.h>
#include <crucible/fixy/dim/Termination.h>

#include <cstddef>

namespace cf = crucible::fixy;
namespace cd = crucible::fixy::dim;
namespace cg = crucible::fixy::grant;

namespace {

// Forcing instantiation: sizeof requires a complete type.  The
// static_assert(N != UINT64_MAX) inside cg::loop_bound fires here.
[[maybe_unused]] constexpr std::size_t kUnboundedLoopSize =
    sizeof(cg::loop_bound<UINT64_MAX>);

// Belt-and-suspenders: also access ::max_iterations to force the
// class-body static_assert pass on the off-chance that sizeof is
// optimized to be type-completeness-only.
[[maybe_unused]] constexpr std::uint64_t kUnboundedLoopBound =
    cg::loop_bound<UINT64_MAX>::max_iterations;

}  // namespace

int main() { return 0; }
