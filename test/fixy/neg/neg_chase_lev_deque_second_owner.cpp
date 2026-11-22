// Negative compile test.  This file must fail to compile.
//
// The OwnerEnd of a work-stealing deque is the one writer of the bottom of
// its deque (the ends of fixy/concurrent/ChaseLevDeque.h).  A copy would be a
// second owner, and two owners that push at the same time lose an item, so
// the copy is deleted.
//
// Expected diagnostic: the copy constructor of the OwnerEnd is deleted.

#include <fixy/concurrent/ChaseLevDeque.h>
#include <foundation/Linear.h>

#include <cstdint>
#include <utility>

int main() {
    ::fixy::concurrent::ChaseLevDeque<std::uint64_t, 8> deque{};
    auto ends = ::fixy::concurrent::mint_chase_lev_ends(deque);
    auto second = ends.owner;
    ::foundation::linear::drop(std::move(second));
    ::foundation::linear::drop(std::move(ends.owner));
    return 0;
}
