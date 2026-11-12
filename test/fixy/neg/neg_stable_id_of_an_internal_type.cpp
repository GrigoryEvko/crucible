// A class with internal linkage takes no stable id.
//
// Each translation unit holds its own class in an unnamed namespace,
// and all of them print one name.  Two different types would share one
// id, so the id refuses the class at compile time.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace {
struct HeldByOneUnit {};
inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^HeldByOneUnit);
}  // namespace

int main() { return refused_id == 0 ? 1 : 0; }
