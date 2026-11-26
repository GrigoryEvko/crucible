// The claim cell of foundation/effects/ThreadClaim.h.
//
//   - A free cell goes to the first caller, and the holder claims it again.
//   - Another thread gets false from a claim and from a release of a held
//     cell, and the cell does not change.
//   - The release of the holder frees the cell, and the next caller claims it.
//   - Of four threads that claim one free cell at one time, one gets true.
//   - A constinit cell in static storage works as a local one does.  The
//     check file of the header pins the type facts.

#include <foundation/effects/ThreadClaim.h>

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/check/Check.h>
#include <fixy/os/ThreadTasks.h>
#include <foundation/effects/Effect.h>

#include <cstdint>
#include <utility>

namespace {

using ::foundation::effects::ThreadClaimCell;
using ::fixy::TestRunnerCtx;
using ::fixy::check::pass;

// A cell in static storage, which the case of the static cell claims.
constinit ThreadClaimCell static_cell{};

// The value of body, which runs on a second thread that the call joins.
template <class Body>
[[nodiscard]] auto on_other_thread(Body body) {
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto const values = ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(body))
                            .expect("the host starts the thread of the test");
    return values.at(::fixy::index<0>());
}

void test_holder_and_other_thread(TestRunnerCtx const& test) {
    ThreadClaimCell cell{};
    CRUCIBLE_FATAL_INVARIANT(!cell.is_held_by_caller());
    CRUCIBLE_FATAL_INVARIANT(cell.try_claim());
    CRUCIBLE_FATAL_INVARIANT(cell.is_held_by_caller());
    CRUCIBLE_FATAL_INVARIANT(cell.try_claim());

    bool const is_refused =
        on_other_thread([&cell] noexcept { return !cell.try_claim() && !cell.is_held_by_caller() && !cell.release(); });
    CRUCIBLE_FATAL_INVARIANT(is_refused);
    CRUCIBLE_FATAL_INVARIANT(cell.is_held_by_caller());

    CRUCIBLE_FATAL_INVARIANT(cell.release());
    CRUCIBLE_FATAL_INVARIANT(!cell.is_held_by_caller());
    CRUCIBLE_FATAL_INVARIANT(!cell.release());

    bool const is_taken_and_given_back = on_other_thread([&cell] noexcept {
        const bool is_taken = cell.try_claim() && cell.is_held_by_caller();
        return is_taken && cell.release();
    });
    CRUCIBLE_FATAL_INVARIANT(is_taken_and_given_back);
    CRUCIBLE_FATAL_INVARIANT(cell.try_claim());
    CRUCIBLE_FATAL_INVARIANT(cell.release());
    pass(test, "  test_holder_and_other_thread: PASSED\n");
}

// Four threads claim one free cell at one time, and one of them gets true.
// The winner keeps the cell, so no later claim of the other threads takes it,
// and the cell ends with the case.
void test_one_of_four_threads_claims(TestRunnerCtx const& test) {
    ThreadClaimCell cell{};
    ::fixy::Atomic<std::uint32_t> claims{0U};
    const auto claim = [&cell, &claims] noexcept {
        if (cell.try_claim()) {
            static_cast<void>(claims.fetch_add_acq_rel(1U));
            CRUCIBLE_FATAL_INVARIANT(cell.is_held_by_caller());
        }
    };
    ::fixy::BgLoadCtx const bg{::foundation::effects::testing::bg()};
    auto first = claim;
    auto second = claim;
    auto third = claim;
    auto fourth = claim;
    ::fixy::thread::run_joined(bg, ::fixy::thread::ThreadOptions{}, std::move(first), std::move(second),
                               std::move(third), std::move(fourth))
        .expect("the host starts the threads of the test");
    CRUCIBLE_FATAL_INVARIANT(claims.load_acquire() == 1U);
    pass(test, "  test_one_of_four_threads_claims: PASSED\n");
}

void test_static_cell(TestRunnerCtx const& test) {
    CRUCIBLE_FATAL_INVARIANT(static_cell.try_claim());
    CRUCIBLE_FATAL_INVARIANT(on_other_thread([] noexcept { return !static_cell.try_claim(); }));
    CRUCIBLE_FATAL_INVARIANT(static_cell.release());
    pass(test, "  test_static_cell: PASSED\n");
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    test_holder_and_other_thread(test);
    test_one_of_four_threads_claims(test);
    test_static_cell(test);
    return 0;
}
