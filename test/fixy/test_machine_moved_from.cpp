// A Machine that a move or a transition emptied refuses each read, each
// extract and each transition, in a child process.  A callee empties the
// machine through a reference, the route that the rule use-after-move does
// not see.  A move assignment builds a spent machine again.

#include <fixy/Machine.h>

#include "../foundation/abort_probe.h"

#include <utility>

namespace {

using ::crucible::test::aborts;

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

int check_moved_from_refuses() {
    Holder stepped{};
    CountMachine taken = take_out(stepped.machine);
    if (taken.data().value != 1) return 1;
    if (!aborts([&] { (void)::fixy::transition_to(std::move(stepped.machine), Counting{3}); })) return 2;

    Holder read{};
    CountMachine read_taken = take_out(read.machine);
    if (!aborts([&] { (void)read.machine.data(); })) return 3;

    Holder written{};
    CountMachine written_taken = take_out(written.machine);
    if (!aborts([&] { written.machine.data_mut().value = 5; })) return 4;

    Holder extracted{};
    CountMachine extracted_taken = take_out(extracted.machine);
    if (!aborts([&] { (void)std::move(extracted.machine).extract(); })) return 5;

    return read_taken.data().value + written_taken.data().value + extracted_taken.data().value == 3 ? 0 : 6;
}

int check_transition_empties_its_source() {
    Holder holder{};
    CountMachine next = step_out(holder.machine);
    if (next.data().value != 2) return 10;
    if (!aborts([&] { (void)::fixy::transition_to(std::move(holder.machine), Counting{3}); })) return 11;
    return 0;
}

int check_assignment_builds_again() {
    Holder holder{};
    CountMachine taken = take_out(holder.machine);
    assign_from(holder.machine, taken);
    if (holder.machine.data().value != 1) return 20;
    if (!aborts([&] { (void)taken.data(); })) return 21;

    // A self assignment keeps the flag, so the read after it does not abort.
    assign_from(holder.machine, holder.machine);
    if (aborts([&] { (void)holder.machine.data(); })) return 22;
    return holder.machine.data().value == 1 ? 0 : 23;
}

}  // namespace

int main() {
    if (int rc = check_moved_from_refuses(); rc != 0) return rc;
    if (int rc = check_transition_empties_its_source(); rc != 0) return rc;
    if (int rc = check_assignment_builds_again(); rc != 0) return rc;
    return 0;
}
