// The failure rules and R004 read the payload through its bands.  The read
// was once the class template unbanded_.  This file tries to make a band
// over an Option read as int, so that a binding whose payload can fail
// passes as one that cannot: it specializes that class template.  The read
// is a function, and no class template gives it, so the specialization does
// not compile.

#include <fixy/Collision.h>
#include <foundation/Quarantine.h>

CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture specializes a trait of the base, and the compiler refuses it")
template <>
struct fixy::collision::detail::unbanded_<fixy::DetSafe<fixy::DetSafeTier_v::Pure, foundation::core::Option<int>>> {
    using type = int;
};
CRUCIBLE_END_I_KNOW_WHAT_IM_DOING

int main() { return 0; }
