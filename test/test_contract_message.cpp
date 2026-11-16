// The message forms of a contract in a function of the namespace crucible.
// The rule layer-boundary reads each name that a base file spells in the
// unit, also in the expansion of a macro of the base.  The expansion of
// CRUCIBLE_PRE_MSG and CRUCIBLE_POST_MSG names no declaration, so the unit
// gives the rule no finding.  A violation ends the process through the
// contract handler, and its report holds the condition and the message.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/contracts/Post.h>
#include <foundation/contracts/Pre.h>

#include <cstdint>

namespace crucible {
namespace {

// The value comes back through a call that no optimization looks into, so
// each check below runs on a run-time argument.
template <typename T>
[[nodiscard, gnu::noipa]] T opaque(T value) noexcept {
    return value;
}

[[nodiscard, gnu::noinline]] std::uint64_t take_in_range(std::uint64_t index, std::uint64_t count) noexcept {
    CRUCIBLE_PRE_MSG(index < count, "the index of a take is less than the count");
    return index * 3;
}

[[nodiscard, gnu::noinline]] std::uint64_t triple_below(std::uint64_t index, std::uint64_t bound) noexcept {
    std::uint64_t const tripled = index * 3;
    CRUCIBLE_POST_MSG(tripled, tripled < bound, "the tripled index is less than the bound");
    return tripled;
}

void take_past_the_count() noexcept { (void)opaque(take_in_range(opaque<std::uint64_t>(7), 7)); }

void triple_past_the_bound() noexcept { (void)opaque(triple_below(opaque<std::uint64_t>(4), 12)); }

}  // namespace
}  // namespace crucible

int main() {
    ::fixy::TestRunnerCtx const test{::foundation::effects::testing::test()};
    using ::fixy::check::expect_dies;
    expect_dies(test, crucible::take_past_the_count, ::fixy::TextView{"foundation: contract violation: index < count"});
    expect_dies(test, crucible::take_past_the_count, ::fixy::TextView{"the index of a take is less than the count"});
    expect_dies(test, crucible::triple_past_the_bound, ::fixy::TextView{"the tripled index is less than the bound"});

    // The controls: a take and a triple inside their bounds.
    CRUCIBLE_TEST_ASSERT(test, crucible::take_in_range(crucible::opaque<std::uint64_t>(6), 7) == 18);
    CRUCIBLE_TEST_ASSERT(test, crucible::triple_below(crucible::opaque<std::uint64_t>(3), 12) == 9);

    ::fixy::check::pass(test, "test_contract_message: PASSED\n");
    return 0;
}
