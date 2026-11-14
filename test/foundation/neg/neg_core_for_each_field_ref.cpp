// The address of a Ref is private, so the field walk does not give it.

#include <foundation/core/Ref.h>
#include <foundation/core/Scalar.h>

int main() {
    int target = 7;
    auto const ref = ::foundation::core::to(target);
    int count = 0;
    ::foundation::core::for_each_field(ref, [&](auto const&) noexcept { ++count; });
    return count;
}
