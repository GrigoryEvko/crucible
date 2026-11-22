// Negative-compile test. This file must fail to compile.
//
// Folding two tokens of one tag back into a parent would require two
// owners of one region to have existed.  A brand names a mint site, not a
// mint call, so two root mints through one site give two tokens of one tag
// and one brand, and the brand agreement admits them.  The manifest here
// names the same tag twice, with its witness, and the distinctness check
// in the body of the binary combine refuses the call.
//
// Expected diagnostic: the static_assert in mint_permission_combine that
// asks for two distinct child tags.

#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Half {
    using permission_row = ::foundation::effects::Row<>;
};

// One mint site, so both calls return one type.
[[nodiscard]] auto mint_half() noexcept { return ::foundation::permissions::mint_permission_root<Half>(); }
}  // namespace

namespace foundation::permissions {
template <>
struct can_split_into<Whole, Half, Half> : std::true_type {};
template <>
struct has_split_authoring_witness<Whole, Half, Half> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    namespace perm = ::foundation::permissions;
    auto first = mint_half();
    auto second = mint_half();
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine<Whole>(std::move(first), std::move(second));
    return 0;
}
