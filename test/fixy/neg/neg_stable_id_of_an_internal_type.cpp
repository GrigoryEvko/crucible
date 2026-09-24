// A class with internal linkage takes no stable id.
//
// Each translation unit holds its own class in an unnamed namespace,
// and all of them print one name.  Two different types would share one
// id, so the id refuses the class at compile time.

#include <foundation/reflect/Hash.h>

namespace {
struct HeldByOneUnit {};
}  // namespace

int main() { return ::foundation::reflect::stable_type_id<HeldByOneUnit> == 0 ? 1 : 0; }
