// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class value that holds a pointer prints the object it points to.
// The walk cannot read that object back to its variable, so a class
// value with a pointer, a reference, a member pointer or a reflection
// inside is refused as a template argument of an id.
//
// Expected diagnostic: the refusal text of the stable id, which says the
// value cannot be read back to a declared name.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace stable_id_class_value_fixture {

inline int const answer = 42;

struct Address {
    int const* pointer;
};

template <auto Value>
struct Holds {};

inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^Holds<Address{&answer}>);

}  // namespace stable_id_class_value_fixture

int main() { return static_cast<int>(stable_id_class_value_fixture::refused_id & 1U); }
