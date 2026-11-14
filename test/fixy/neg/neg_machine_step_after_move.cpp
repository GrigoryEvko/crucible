// A step of a Machine that a callee moved out of a holder.  The move clears
// the flag of the source, so the transition fails its precondition, and a
// constant evaluation of the step is not a constant expression.

#include <fixy/Machine.h>

#include <utility>

namespace machine_step_after_move {

struct Held {
    ::fixy::Machine<int> machine = ::fixy::mint_machine<int>(1);
};

consteval ::fixy::Machine<int> take_out(::fixy::Machine<int>& held) noexcept { return std::move(held); }

consteval int step_spent() noexcept {
    Held held{};
    ::fixy::Machine<int> taken = take_out(held.machine);
    ::fixy::Machine<int> again = ::fixy::transition_to(std::move(held.machine), 2);
    return taken.data() + again.data();
}

static_assert(step_spent() == 3);

}  // namespace machine_step_after_move

int main() { return 0; }
