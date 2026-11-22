// Negative-compile test. This file must fail to compile.
//
// The ctx-bound form of the binary combine takes the same distinctness
// check as the form with no context.  The context here admits the rows of
// the parent and of the child, so the fit concept admits the call, and the
// check in the body refuses the two tokens of one tag.
//
// Expected diagnostic: the static_assert in mint_permission_combine that
// asks for two distinct child tags.

#include <foundation/effects/Ctx.h>
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
    [[maybe_unused]] auto rebuilt = perm::mint_permission_combine<Whole>(::foundation::effects::testing::foreground(),
                                                                         std::move(first), std::move(second));
    return 0;
}
