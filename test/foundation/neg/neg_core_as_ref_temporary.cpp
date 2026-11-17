// as_ref gives a Ref into the payload of an Option.  A temporary Option ends
// at the end of the full expression, so the Ref would dangle, and as_ref of
// a temporary is deleted.

#include <foundation/core/Choice.h>
#include <foundation/core/Ref.h>

int main() {
    auto const borrowed = ::foundation::core::as_ref(::foundation::core::Option<int>::some(4));
    return borrowed.is_some() ? 0 : 1;
}
