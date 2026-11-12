// A Ref of a temporary dangles at the end of the full expression, so to()
// of an rvalue is deleted.

#include <foundation/core/Ref.h>

int main() {
    ::foundation::core::Ref<int const> const dangling = ::foundation::core::Ref<int const>::to(7);
    return *dangling;
}
