// W001: hot x a kernel wait, stated as a system call.
//
// A futex call parks the caller until another thread wakes it.
// fixy/atoms/Syscall.h puts futex in the ThreadSync family, and that
// family lifts to Row<Block>.  W001 reads the row that the whole pack
// lifts to.  It refuses the call, although the pack states no wait
// strategy and its Effect grade is empty.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// W001 alone.

#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

namespace {

struct ring_depth_proved final {};

}  // namespace

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::futex>,
                                ::fixy::atom::cost_constant, ::fixy::atom::refined_with<ring_depth_proved>>
        refused{};
    return 0;
}
