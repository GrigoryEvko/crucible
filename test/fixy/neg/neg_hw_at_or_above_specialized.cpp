// V201, V202 and V203 read the instruction tier of a binding against a
// floor.  The read was once the class template is_hw_at_or_above_.  This
// file tries to put a timestamp read below the floor of NonDeterministicTsc,
// so that a hot binding that reads the counter passes V201: it specializes
// that class template.  The read is a concept, and no class template gives
// it, so the specialization does not compile.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

namespace hw = fixy::atom::hw;

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::collision::detail::is_hw_at_or_above_<hw::HwInstruction::NonDeterministicTsc, hw::non_deterministic_tsc> {
    static constexpr bool value = false;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
