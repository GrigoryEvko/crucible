// Negative compile test.  This file must fail to compile.
//
// A Lazy holds its value in its own storage, and get_or_init gives a Ref with the
// brand that the class states.  A class that states an arena tag and that code
// outside the arena builds is no object of an arena, so the door refuses it
// (carrier_brand_t of foundation/core/Ref.h).

#include <fixy/handle/Once.h>
#include <foundation/core/BumpArena.h>
#include <foundation/effects/Ctx.h>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Loan {
    using brand_type = Family;
    int value = 0;
};

[[nodiscard]] bool has_loan(::fixy::handle::Lazy<Loan>& lazy) { return lazy.get().is_some(); }
}  // namespace

int main() {
    ::fixy::handle::Lazy<Loan> lazy{};
    return has_loan(lazy) ? 1 : 0;
}
