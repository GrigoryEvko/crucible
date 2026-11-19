// The payload order once kept its answer for one pair in the variable
// template subsorts_v, and each gate read it.  This file tries to make
// Forged a subsort of Honest: it specializes that variable.  The order
// keeps its answer in a cell that only its walk builds, and no variable
// gives it, so the specialization does not compile.

#include <fixy/session/Subtype.h>
#include <foundation/Quarantine.h>

struct Forged {};
struct Honest {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr bool fixy::session::transition::subsorts_v<^^fixy::session::payload_axioms, ^^Forged, ^^Honest> = true;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
