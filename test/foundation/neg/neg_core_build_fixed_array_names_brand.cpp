// The frame of build_fixed_array has the scope brand of its call.  A caller
// that names a third argument binds it to the pack of refused arguments.

#include <foundation/core/Array.h>

namespace {
struct HeldBrand final {};
}  // namespace

int main() {
    auto const built = ::foundation::core::build_fixed_array<int, 4, HeldBrand>([](auto run) noexcept {
        for (int& element : run)
            element = 1;
    });
    return built.at(::foundation::core::index<0>());
}
