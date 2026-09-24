// W001: hot x a kernel wait, stated as a wait on a descriptor.
//
// epoll_wait parks the caller until a descriptor is ready or the timeout
// expires.  fixy/atoms/Syscall.h files it in the ThreadSync family, and
// that family lifts to Row<Block>.  W001 reads the row of the binding, so
// it refuses the call although the pack states no wait strategy.  CLAUDE.md
// IX lists poll and epoll_wait among the waits that the hot path bans.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// W001 alone.  The row holds Block and no IO, so H003 stands down.

#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

namespace {

struct ready_count_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::epoll_wait>,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<ready_count_proved>>
        refused{};
    return 0;
}
