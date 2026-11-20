// The compile-time checks of fixy/check/Check.h.

#include <fixy/check/Check.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <foundation/Linear.h>

namespace check_family_checks {

using ::fixy::check::Tally;

// The questions of the checks.  Each one is a concept, so a refused
// expression is a false answer and not an error.
template <class T>
concept CopyBuildable = requires(T const& value) { T{value}; };
template <class T>
concept CopyAssignable = requires(T& target, T const& source) { target = source; };
template <class T>
concept MoveAssignable = requires(T& target, T&& source) { target = static_cast<T&&>(source); };
template <class T>
concept NoexceptMoveBuildable = requires(T&& source) {
    { T{static_cast<T&&>(source)} } noexcept;
};
template <class T, class Ctx>
concept BuildableFrom = requires(Ctx const& ctx, ::fixy::TextView name) { T{ctx, name}; };
template <class T, class Ctx>
concept BuildableWithNoName = requires(Ctx const& ctx) { T{ctx}; };

// A Tally is linear, and close is its drop door, so the rule must-consume
// reads each path of a Tally.
static_assert(::foundation::linear::is_linear_v<Tally>);

// No copy and no assignment, so no failure is counted two times or lost.
static_assert(!CopyBuildable<Tally>);
static_assert(!CopyAssignable<Tally>);
static_assert(!MoveAssignable<Tally>);

// A move gives the counts to the new Tally, and it ends nothing.
static_assert(NoexceptMoveBuildable<Tally>);

// Only a context that owns the Test capability builds a Tally, and each
// Tally has the name of its test.
static_assert(BuildableFrom<Tally, ::fixy::TestRunnerCtx>);
static_assert(!BuildableFrom<Tally, ::fixy::HotFgCtx>);
static_assert(!BuildableWithNoName<Tally, ::fixy::TestRunnerCtx>);

// The state is as wide as a count, so the class has no padding byte: the
// name, the count of the checks, the count of the failures and the state.
static_assert(sizeof(Tally) == sizeof(::fixy::TextView) + 3 * sizeof(unsigned long long));

}  // namespace check_family_checks
