// A view of one instance does not convert to the erased identity.  An
// erased view is one type with each other erased view of its carrier type
// and its state, so an implicit erasure would let a view of one carrier
// stand in for a view of another.  erase_brand(view) is the one door.

#include <fixy/ScopedView.h>

#include <type_traits>

namespace {
struct Carrier {
    bool ready = true;
};
struct Ready {};
constexpr bool view_ok(Carrier const& c, std::type_identity<Ready>) noexcept { return c.ready; }
}  // namespace

int main() {
    Carrier carrier{};
    auto const view = ::fixy::mint_view<Ready>(carrier);
    using Erased = decltype(::fixy::erase_brand(view));
    [[maybe_unused]] Erased const erased = view;
    return 0;
}
