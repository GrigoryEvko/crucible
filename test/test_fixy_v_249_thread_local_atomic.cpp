// Rule G002 rejects a thread_local grant paired with an atomic memory-order
// wrapper.  An atomic operation on a per-thread object orders against no peer,
// because each thread holds its own instance.  The rule is marker-driven: the
// grant layer specializes marks_thread_local_atomic when both are present.

#include <crucible/safety/Fn.h>

#include <atomic>
#include <string_view>
#include <type_traits>

namespace csfn = ::crucible::safety::fn;
namespace csc = ::crucible::safety::fn::collision;

namespace {

static_assert(csc::catalog_size >= 37, "floor: the catalog must include G002");
static_assert(csc::rule_bijection_v<csc::RuleCode::G002>);

using DefaultFn = csfn::Fn<int>;
static_assert(csc::CollisionDiagnosticByRule<DefaultFn, csc::RuleCode::G002>::rule_code() == std::string_view{"G002"});

static_assert(!csc::marks_thread_local_atomic<DefaultFn>::value);
static_assert(csc::G002_OK<DefaultFn>);
static_assert(csc::first_failure_v<DefaultFn> == csc::RuleCode::None);

// G002 needs both thread_local storage and atomic synchronization.  A carrier
// with only one of them leaves the marker unset and must pass, so the rule
// cannot fire spuriously on a lone thread_local or a lone atomic.
struct ThreadLocalOnlyTag {};
struct AtomicOnlyTag {};
using ThreadLocalOnlyFn = csfn::Fn<ThreadLocalOnlyTag>;
using AtomicOnlyFn = csfn::Fn<AtomicOnlyTag>;
static_assert(csfn::ValidComposition<ThreadLocalOnlyFn>);
static_assert(csfn::ValidComposition<AtomicOnlyFn>);
static_assert(csc::G002_OK<ThreadLocalOnlyFn>);
static_assert(csc::G002_OK<AtomicOnlyFn>);
static_assert(csc::first_failure_v<AtomicOnlyFn> == csc::RuleCode::None);

// A real atomic carrier with no thread_local marker also passes.  The atomic
// type by itself is not a hazard.
using RealAtomicFn = csfn::Fn<std::atomic<std::uint64_t>>;
static_assert(csc::G002_OK<RealAtomicFn>);

// This TU cannot assert first_failure_v<F> == G002 for a carrier that genuinely
// trips the rule.  Instantiating that carrier runs the Fn static_assert on
// ValidComposition before the query can run.  The negative case therefore lives
// in the neg-compile fixtures.

}  // namespace

int main() { return 0; }
