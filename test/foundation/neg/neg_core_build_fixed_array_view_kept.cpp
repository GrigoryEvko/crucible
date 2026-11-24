// The body of build_fixed_array keeps the View of its ViewMut in a variable
// that it captures by reference.  The View outlives the local of the door.

#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Region.h>

int main() {
    ::foundation::core::Option<::foundation::core::View<int const>> kept{::foundation::core::none};
    auto const built = ::foundation::core::build_fixed_array<int, 4>([&kept](auto run) noexcept {
        kept = ::foundation::core::Option<::foundation::core::View<int const>>::some(run.view());
    });
    return kept.is_some() ? built.at(::foundation::core::index<0>()) : 1;
}
