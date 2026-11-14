// The address of the object of a Box is the protected state of its owner
// core, so the field walk does not give it.

#include <foundation/core/Ref.h>
#include <foundation/core/Scalar.h>

namespace box_field_walk {

int count_fields(::foundation::core::Box<int> const& box) noexcept;

int count_fields(::foundation::core::Box<int> const& box) noexcept {
    int count = 0;
    ::foundation::core::for_each_field(box, [&](auto const&) noexcept { ++count; });
    return count;
}

}  // namespace box_field_walk

int main() { return 0; }
