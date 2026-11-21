// The holder of foundation/core/Seat.h.
//
//   - A loop of steps keeps each next payload in the Seat, and the take at the
//     end gives the payload of the last step.
//   - A step moves the payload one time, as the emplace of a std::optional
//     does: the result of the step goes into the storage of the Seat.
//   - Each payload of a loop ends one time: the end of a step ends the old
//     payload and the moved result, and the end of the Seat after its take
//     ends the moved payload.
//   - A Seat of a linear payload is linear, and a Seat of a plain payload is
//     not.

#include <foundation/core/Seat.h>

#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <foundation/Linear.h>
#include <foundation/effects/Effect.h>

#include <cstdint>

namespace {

namespace core = ::foundation::core;

using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// The count of the destructors of Counted that ran.
constinit std::uint32_t ended = 0;

// The count of the moves of a Counted.
constinit std::uint32_t moved = 0;

// A linear payload whose destructor counts its runs, as a session handle ends
// its moved state.  A send gives the next payload with the sum of the items.
struct[[= ::foundation::linear::linear{}]][[= ::foundation::lifetime::no_start_over_bytes{}]] Counted final {
    std::uint64_t sum = 0;

    explicit Counted(std::uint64_t value) noexcept : sum{value} {}
    Counted(Counted&& source) noexcept : sum{source.sum} {
        source.sum = 0;
        ++moved;
    }
    Counted& operator=(Counted&& source) noexcept {
        sum = source.sum;
        source.sum = 0;
        return *this;
    }
    Counted(Counted const&) = delete("a payload of the test is linear");
    Counted& operator=(Counted const&) = delete("a payload of the test is linear");
    ~Counted() { ++ended; }

    [[nodiscard]] Counted send(std::uint64_t item) && noexcept { return Counted{sum + item}; }

    // The last use of a payload: it gives the sum, and the payload ends at the
    // end of its scope.
    [[nodiscard]] std::uint64_t finish() && noexcept { return sum; }
};

static_assert(::foundation::linear::is_linear_v<Counted>);
static_assert(::foundation::linear::is_linear_v<core::Seat<Counted>>);
static_assert(!::foundation::linear::is_linear_v<core::Seat<std::uint64_t>>);

void loop_of_steps(TestRunnerCtx const& test) {
    core::Seat<Counted> seat = core::mint_seat(Counted{1});
    for (std::uint64_t item = 1; item <= 10; ++item) {
        seat.step([item](Counted&& payload) noexcept { return static_cast<Counted&&>(payload).send(item); });
    }
    CRUCIBLE_TEST_ASSERT(test, std::move(seat).take().finish() == 56);
    pass(test, "  loop_of_steps: PASSED\n");
}

void step_moves_the_payload_one_time(TestRunnerCtx const& test) {
    core::Seat<Counted> seat = core::mint_seat(Counted{0});
    std::uint32_t const before = moved;
    seat.step([](Counted&& payload) noexcept { return static_cast<Counted&&>(payload).send(1); });
    // The send builds its result in place, and the step moves it one time,
    // into the storage of the Seat.
    CRUCIBLE_TEST_ASSERT(test, moved == before + 1);
    seat.step([](Counted&& payload) noexcept { return static_cast<Counted&&>(payload).send(1); });
    CRUCIBLE_TEST_ASSERT(test, moved == before + 2);
    CRUCIBLE_TEST_ASSERT(test, std::move(seat).take().finish() == 2);
    pass(test, "  step_moves_the_payload_one_time: PASSED\n");
}

void each_payload_ends_one_time(TestRunnerCtx const& test) {
    std::uint32_t const before = ended;
    {
        core::Seat<Counted> seat = core::mint_seat(Counted{5});
        // The mint ends its moved parameter, and nothing else.
        std::uint32_t const after_mint = ended;
        CRUCIBLE_TEST_ASSERT(test, after_mint == before + 1);
        seat.step([](Counted&& payload) noexcept { return static_cast<Counted&&>(payload).send(2); });
        // A step ends the old payload and the moved result of fn.
        CRUCIBLE_TEST_ASSERT(test, ended == after_mint + 2);
        std::uint32_t const after_step = ended;
        CRUCIBLE_TEST_ASSERT(test, std::move(seat).take().finish() == 7);
        // The taken payload ends at the end of its full expression.
        CRUCIBLE_TEST_ASSERT(test, ended == after_step + 1);
    }
    // The end of the Seat ends the moved payload in its storage: five ends in
    // all, one for each payload that the loop built.
    CRUCIBLE_TEST_ASSERT(test, ended == before + 5);
    pass(test, "  each_payload_ends_one_time: PASSED\n");
}

void plain_payload(TestRunnerCtx const& test) {
    core::Seat<std::uint64_t> seat = core::mint_seat(std::uint64_t{3});
    seat.step([](std::uint64_t&& value) noexcept { return value * 2; });
    seat.step([](std::uint64_t&& value) noexcept { return value + 1; });
    CRUCIBLE_TEST_ASSERT(test, std::move(seat).take() == 7);
    pass(test, "  plain_payload: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    loop_of_steps(test);
    step_moves_the_payload_one_time(test);
    each_payload_ends_one_time(test);
    plain_payload(test);
    pass(test, "test_core_seat: PASSED\n");
    return 0;
}
