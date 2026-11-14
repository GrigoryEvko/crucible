// A class keeps the address of its elements in a private View.  The class is
// not plain, so the field walk does not give the private member.

#include <foundation/core/Region.h>
#include <foundation/core/Scalar.h>

namespace {

class Cursor {
public:
    explicit Cursor(::foundation::core::View<int const> elements) noexcept : elements_{elements} {}

private:
    ::foundation::core::View<int const> elements_{};
};

}  // namespace

int main() {
    Cursor const cursor{::foundation::core::View<int const>{}};
    int count = 0;
    ::foundation::core::for_each_field(cursor, [&](auto const&) noexcept { ++count; });
    return count;
}
