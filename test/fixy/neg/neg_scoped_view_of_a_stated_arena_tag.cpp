// Negative compile test.  This file must fail to compile.
//
// A view takes the brand that its carrier states.  A carrier that states an arena
// tag and that code outside the arena builds is no object of an arena, so the mint
// refuses it (carrier_brand_t of foundation/core/Ref.h).

#include <fixy/ScopedView.h>
#include <foundation/core/BumpArena.h>

#include <type_traits>

struct[[= ::foundation::brand::arena_family_brand{}]] Family final {};

namespace {
struct Ready {};

struct Carrier {
    using brand_type = Family;
    bool ready = true;
};
constexpr bool view_ok(Carrier const& carrier, std::type_identity<Ready> /*state*/) noexcept { return carrier.ready; }
}  // namespace

int main() {
    Carrier const carrier{};
    auto const view = ::fixy::mint_view<Ready>(carrier);
    return view.carrier().ready ? 0 : 1;
}
