// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GCC prints a union template argument as the value of its active
// member, without the member.  Two values of one union that differ only
// in the active member name two types and print one name.  The stable id
// refuses a union value, and a class value that holds one.
//
// Expected diagnostic: the refusal text of the stable id, which names
// the value that a different value also prints.

#include <foundation/reflect/Hash.h>

#include <cstdint>

namespace stable_id_union_fixture {

union Either {
    int first;
    int second;
};

template <auto Value>
struct Holds {};

inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^Holds<Either{.second = 1}>);

}  // namespace stable_id_union_fixture

int main() { return static_cast<int>(stable_id_union_fixture::refused_id & 1U); }
