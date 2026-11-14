// The address and the count of a View are private, so the field walk does
// not give them.

#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>

int main() {
    ::foundation::core::View<int> const view{};
    int count = 0;
    ::foundation::core::for_each_field(view, [&](auto const&) noexcept { ++count; });
    return count;
}
