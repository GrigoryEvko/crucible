// The body of with_read_view keeps a Ref of its view in a variable that it
// captures by reference.  The view lives in the frame of the door, so the
// kept Ref names a released frame after the door returns, and a read through
// it is a read of the stack of a call that ended.  The store check of the
// door (StoresNoBorrowOutside of foundation/core/CaptureStore.h) refuses the
// body.

#include <foundation/Brand.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>
#include <foundation/effects/Row.h>
#include <foundation/permissions/Permission.h>
#include <foundation/permissions/ReadView.h>

#include <utility>

namespace {

struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

}  // namespace

int main() {
    namespace fp = ::foundation::permissions;
    auto root = fp::mint_permission_root<Region>();
    using RootBrand = ::foundation::brand::brand_of_t<decltype(root)>;
    ::foundation::core::Option<::foundation::core::Ref<fp::ReadView<Region, RootBrand> const>> kept{
        ::foundation::core::none};
    auto back = fp::with_read_view(std::move(root), [&kept](auto const& view) noexcept {
        kept = ::foundation::core::some(::foundation::core::erase_brand(::foundation::core::to(view)));
    });
    static_cast<void>(back);
    return kept.is_some() ? 0 : 1;
}
