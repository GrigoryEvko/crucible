// V202: the privileged instruction tier without an Init context.
//
// rdmsr, wrmsr, IN and OUT belong to startup, where they can be ordered
// and audited and where the ring-0 capability is held.  A binding that
// declares the tier and whose effect row carries no Init has claimed the
// instructions without the context that owns them.
//
// This is a rule reading the Effect row, not a lift.  fixy/atoms/Hw.h
// says why the family declares no lift: an instruction class admits a
// set of operations rather than naming one.  The requirement that the
// privileged set be reached only from Init is therefore stated as a
// collision, which is where the old catalog stated it.
//
// The twin is accepted: add atom::with<Effect::Init> and the same tier
// passes.  test/fixy/test_collision.cpp holds that cell.

#include <fixy/Fn.h>

int main() {
    [[maybe_unused]] ::fixy::fn<int, ::fixy::atom::hw::privileged_msr> refused{};
    return 0;
}
