// A Ref of a temporary dangles at the end of the full expression, so to()
// of an rvalue is deleted.

#include <foundation/core/Ref.h>

int main() {
    auto const dangling = ::foundation::core::to(7);
    return *dangling;
}
