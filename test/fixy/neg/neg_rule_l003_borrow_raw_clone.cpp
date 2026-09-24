// L003: a borrow x a raw clone.
//
// The second mismatch class the rule reads.  A syscall_only child is a
// raw clone that shares the address space, and the structured join does
// not reach it, so a borrow into the caller's frame dangles the same way
// it does under a detached child.  The pack trips L003 alone.

#include <fixy/Fn.h>
#include <fixy/os/Spawn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::borrow,
                                ::fixy::atom::spawn::syscall_only<"bpf loader needs CLONE_VM">>
        refused{};
    return 0;
}
