// H003: hot x IO in the row of the binding, stated as a system call.
//
// getpid cannot park the caller, so W001 has nothing to refuse.  It is a
// system call all the same, and fixy/atoms/Syscall.h lifts it to
// Row<IO>.  The Effect grade of the binding stays at the strict pole, and
// H003 reads the row of the binding, which holds the lift.  CLAUDE.md
// VIII bans a system call on the hot path, and this is the fixture for a
// call that does not block.
//
// The cost and refinement atoms are in the pack so that the fixture trips
// H003 alone.  as_public is in the pack so that the corpus entry
// classified_io_without_declassify has no classified value to refuse.

#include <fixy/Fn.h>
#include <fixy/atoms/Syscall.h>

// The tag has external linkage.  An atom whose argument has internal
// linkage has no stable identity, and the gate refuses it at tier 2.
namespace fixture {

struct owner_pid_proved final {};

}  // namespace fixture

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::regime::hot,
                                ::fixy::atom::syscall::per<::fixy::atom::syscall::SyscallId::getpid>,
                                ::fixy::atom::as_public, ::fixy::atom::cost_constant,
                                ::fixy::atom::refined_with<fixture::owner_pid_proved>> refused{};
    return 0;
}
