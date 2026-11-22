// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A StealerEnd of a work-stealing deque takes items from the top, and only
// the OwnerEnd pushes at the bottom (THE ENDS of
// fixy/concurrent/ChaseLevDeque.h).  A thief that pushed would write the
// bottom beside the owner, so a StealerEnd has no push.
//
// Expected diagnostic: the StealerEnd has no member push_bottom.

#include <fixy/concurrent/ChaseLevDeque.h>
#include <foundation/Linear.h>

#include <cstdint>
#include <utility>

int main() {
    ::fixy::concurrent::ChaseLevDeque<std::uint64_t, 8> deque{};
    auto ends = ::fixy::concurrent::mint_chase_lev_ends(deque);
    [[maybe_unused]] bool const is_pushed = ends.stealer.push_bottom(std::uint64_t{1});
    ::foundation::linear::drop(std::move(ends.owner));
    return 0;
}
