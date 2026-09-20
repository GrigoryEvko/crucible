// mint_spawn refuses a callable whose type carries the control-flow atom
// fixy::atom::ctrl::throws.  A noexcept declaration is a promise the
// callable can still break, and a throw out of a child tears through the
// join instead of unwinding it.
//
// The family here is a named one, not the default, which is the case the
// old needle in include/crucible/fixy/ctrl/Throws.h did not match.

#include <fixy/os/Spawn.h>
#include <foundation/permissions/Permission.h>

#include <type_traits>
#include <utility>

namespace eff = foundation::effects;
namespace perm = foundation::permissions;

namespace {
struct Whole {};
struct Left {};
struct Right {};
struct SampleException {};
using BgCtx = eff::ExecCtx<eff::Bg, eff::Row<eff::Effect::Bg, eff::Effect::Alloc>>;

template <typename Marker>
struct MarkedBody {
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
        ctx, std::move(whole), MarkedBody<fixy::atom::ctrl::throws<SampleException>>{},
        [](perm::Permission<Right>, BgCtx const&) noexcept {});
    return 0;
}
