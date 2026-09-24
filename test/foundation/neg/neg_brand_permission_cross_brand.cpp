// Two root mints of one tag are two brands.  A callee that asks for
// two tokens of one region names one brand for both, so the pair here
// fails deduction: the second regex is the compiler reporting that the
// one parameter was deduced to two different closure types, which the
// fixture source never spells.

#include <foundation/permissions/Permission.h>

#include <utility>

namespace {
struct Region {
    using permission_row = ::foundation::effects::Row<>;
};

template <class Brand>
constexpr void same_region(::foundation::permissions::Permission<Region, Brand>&&,
                           ::foundation::permissions::Permission<Region, Brand>&&) noexcept {}
}  // namespace

int main() {
    auto first = ::foundation::permissions::mint_permission_root<Region>();
    auto second = ::foundation::permissions::mint_permission_root<Region>();
    same_region(std::move(first), std::move(second));
    return 0;
}
