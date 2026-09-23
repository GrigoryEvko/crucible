// mint_spawn refuses a callable that holds the control-flow atom
// fixy::atom::ctrl::throws in a member.  The callable is a plain class,
// so no template argument names the atom.  A walk over the template
// arguments alone does not see it.  The walk over components reads the
// member and finds it.
//
// Sibling of neg_spawn_callable_carries_throws.cpp, where the atom is a
// template argument of the callable.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct Whole {
    using permission_row = ::foundation::effects::Row<>;
};
struct Left {
    using permission_row = ::foundation::effects::Row<>;
};
struct Right {
    using permission_row = ::foundation::effects::Row<>;
};
struct SampleException {};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

template <typename Marker>
struct Marked {};

struct HoldsMarkedMember {
    Marked<fixy::atom::ctrl::throws<SampleException>> marker{};
    void operator()(perm::Permission<Left>, BgCtx const&) const noexcept {}
};
}  // namespace

namespace foundation::permissions {
template <>
struct splits_into_pack<Whole, Left, Right> : std::true_type {};
template <>
struct splits_into_pack_authoring_witness<Whole, Left, Right> : std::true_type {};
}  // namespace foundation::permissions

int main() {
    BgCtx ctx{eff::testing::bg()};
    auto whole = perm::mint_permission_root<Whole>();
    [[maybe_unused]] auto rebuilt = fixy::spawn::mint_spawn<Left, Right>(
        ctx, std::move(whole), HoldsMarkedMember{},
        [](perm::Permission<Right>, BgCtx const&) noexcept {});
    return 0;
}
