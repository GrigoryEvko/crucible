// A file outside Atomic.h writes an explicit specialization of the template
// mint_sole_writer for a brand that it names.  While the key befriends
// mint_sole_writer, the body of the specialization builds the key that only
// the mint makes, so it gives one writer brand two times.  The rule
// specialization-home refuses the specialization in each build, because a
// function template of the tree gets an explicit specialization only in its
// own file.  The body reaches no private member, so the fixture reads only
// the rule.

#include <foundation/core/Atomic.h>

struct HeadRole {};
struct CellBrand {};

template <>
constexpr foundation::core::SoleWriter<HeadRole, CellBrand> foundation::core::mint_sole_writer<HeadRole>() noexcept {
    for (;;) {}
}

int main() { return 0; }
