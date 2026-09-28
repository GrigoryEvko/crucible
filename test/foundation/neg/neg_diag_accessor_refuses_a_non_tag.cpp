// The tag accessors read a field of a class derived from tag_base.  A
// plain type reaches the routed static_assert, whose wording names the
// accessor family and the base it asks for.
#include <foundation/diag/Catalog.h>

constexpr auto bogus_name = ::foundation::diag::diagnostic_name_v<int>;

int main() { return static_cast<int>(bogus_name.size()); }
