// The payload order keeps its answer for one pair in a variable template,
// and each gate reads that answer.  This file tries to make Forged a
// subsort of Honest: it specializes the answer under the spelling
// subsorts_v.  The answer is a cell that only the walk of the order builds,
// so the specialization does not compile, and the pair stays apart.

#include <fixy/session/Subtype.h>
#include <foundation/Quarantine.h>

struct Forged {};
struct Honest {};

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
inline constexpr bool fixy::session::transition::subsorts_v<^^fixy::session::payload_axioms, ^^Forged, ^^Honest> = true;
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

static_assert(fixy::session::is_payload_subsort_v<Forged, Honest>);

int main() { return 0; }
