// Negative compile test.  This file must fail to compile.
//
// A raw pointer fits a lock-free cell, but a thief that steals it reads the
// object behind it on another thread, with no transfer check.  So DequeValue
// asks Sendable<T>, and a deque of a raw pointer is refused
// (fixy/concurrent/ChaseLevDeque.h).
//
// Expected diagnostic: the constraint DequeValue of ChaseLevDeque fails for
// int*.

#include <fixy/concurrent/ChaseLevDeque.h>

int main() {
    [[maybe_unused]] ::fixy::concurrent::ChaseLevDeque<int*, 8> deque{};
    return 0;
}
