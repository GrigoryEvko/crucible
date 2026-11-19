// as_const of a temporary gives a reference that dangles at the end of the
// full expression, so the call does not compile.

#include <foundation/core/Values.h>

int main() {
    int const& view = ::foundation::core::as_const(4);
    return view;
}
