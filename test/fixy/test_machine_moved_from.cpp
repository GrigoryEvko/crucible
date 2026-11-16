// A Machine that a move or a transition emptied refuses each read, each
// extract and each transition, in a child process through expect_dies of the
// Check family.  A callee empties the machine through a reference, the route
// that the rule use-after-move does not see.  A move assignment builds a spent
// machine again.

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Machine.h>
#include <fixy/check/Check.h>
#include <foundation/effects/Effect.h>

#include <utility>

namespace {

using ::fixy::TestRunnerCtx;
using ::fixy::check::expect_dies;

// The report of a use of an emptied machine.
constexpr ::fixy::TextView kEmptied{"foundation: contract violation: is_live_"};

struct Counting {
    int value = 0;
    constexpr explicit Counting(int start) noexcept : value{start} {}
};

using CountMachine = ::fixy::Machine<Counting>;

struct Holder {
    CountMachine machine = ::fixy::mint_machine<Counting>(1);
};

CountMachine take_out(CountMachine& held) noexcept { return std::move(held); }

CountMachine step_out(CountMachine& held) noexcept { return ::fixy::transition_to(std::move(held), Counting{2}); }

void assign_from(CountMachine& target, CountMachine& source) noexcept { target = std::move(source); }

int check_moved_from_refuses(TestRunnerCtx const& test) {
    Holder stepped{};
    CountMachine taken = take_out(stepped.machine);
    if (taken.data().value != 1) return 1;
    expect_dies(
        test, [&stepped] noexcept { (void)::fixy::transition_to(std::move(stepped.machine), Counting{3}); }, kEmptied);

    Holder read{};
    CountMachine read_taken = take_out(read.machine);
    expect_dies(test, [&read] noexcept { (void)read.machine.data(); }, kEmptied);

    Holder written{};
    CountMachine written_taken = take_out(written.machine);
    expect_dies(test, [&written] noexcept { written.machine.data_mut().value = 5; }, kEmptied);

    Holder extracted{};
    CountMachine extracted_taken = take_out(extracted.machine);
    expect_dies(test, [&extracted] noexcept { (void)std::move(extracted.machine).extract(); }, kEmptied);

    return read_taken.data().value + written_taken.data().value + extracted_taken.data().value == 3 ? 0 : 6;
}

int check_transition_empties_its_source(TestRunnerCtx const& test) {
    Holder holder{};
    CountMachine next = step_out(holder.machine);
    if (next.data().value != 2) return 10;
    expect_dies(
        test, [&holder] noexcept { (void)::fixy::transition_to(std::move(holder.machine), Counting{3}); }, kEmptied);
    return 0;
}

int check_assignment_builds_again(TestRunnerCtx const& test) {
    Holder holder{};
    CountMachine taken = take_out(holder.machine);
    assign_from(holder.machine, taken);
    if (holder.machine.data().value != 1) return 20;
    expect_dies(test, [&taken] noexcept { (void)taken.data(); }, kEmptied);

    // A self assignment keeps the flag, so the read after it does not abort.
    assign_from(holder.machine, holder.machine);
    return holder.machine.data().value == 1 ? 0 : 23;
}

}  // namespace

int main() {
    TestRunnerCtx const test{::foundation::effects::testing::test()};
    if (int rc = check_moved_from_refuses(test); rc != 0) return rc;
    if (int rc = check_transition_empties_its_source(test); rc != 0) return rc;
    if (int rc = check_assignment_builds_again(test); rc != 0) return rc;
    return 0;
}
