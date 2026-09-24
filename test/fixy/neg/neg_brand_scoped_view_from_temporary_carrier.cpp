// A view over a temporary carrier outlives it.  The deleted rvalue
// twin of mint_view is the better match for the prvalue, so the call
// names a deleted function.  This is the refusal by value category;
// the sibling fixture neg_brand_scoped_view_cross_brand.cpp is the
// refusal by identity.

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
    auto view = ::fixy::mint_view<Ready>(Carrier{});
    (void)view;
    return 0;
}
