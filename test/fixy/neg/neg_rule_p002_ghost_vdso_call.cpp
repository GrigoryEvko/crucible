// P002: ghost x an emitting surface.  A ghost binding is erased at
// codegen, and a call through the vDSO is emitted code by definition.
//
// The pack trips P002 alone.  clock_gettime reads the clock through the
// vDSO, so fixy/atoms/Syscall.h lifts it to the empty row.  P010 and the
// corpus entry ghost_runtime_observable read the row of the binding, which
// is empty here, so neither fires.  That is what makes this fixture the
// witness that P002 is not redundant: remove it and this binding is
// admitted.  A stdio write lifts IO and Block, so P010 refuses a ghost
// write too, and a ghost write cannot isolate P002.

#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::ghost,
                                ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::clock_gettime>> refused{};
    return 0;
}
