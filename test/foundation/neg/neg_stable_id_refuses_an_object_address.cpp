// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A template argument that points to an object prints the object's
// name.  No query reads the object back to its variable, so a variable
// with internal linkage in two translation units would print one name
// and give two types one id.  The stable id refuses every object
// address, whatever the linkage of the variable.
//
// Expected diagnostic: the refusal text of the stable id, which names
// the object address.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace stable_id_object_address_fixture {

static int counter = 0;

template <auto Value>
struct Holds {};

inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^Holds<&counter>);

}  // namespace stable_id_object_address_fixture

int main() { return static_cast<int>(stable_id_object_address_fixture::refused_id & 1U); }
