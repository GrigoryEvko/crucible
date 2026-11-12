// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// GCC prints a NaN template argument without its payload, so two NaNs
// with different payloads name two types and print one name.  The stable
// id refuses a NaN value.
//
// Expected diagnostic: the refusal text of the stable id, which names
// the value that a different value also prints.

#include <foundation/reflect/Hash.h>

#include <bit>
#include <cstdint>

namespace stable_id_nan_fixture {

inline constexpr double payload_nan = std::bit_cast<double>(std::uint64_t{0x7ff8000000001234});

template <auto Value>
struct Holds {};

inline constexpr std::uint64_t refused_id = ::foundation::reflect::stable_type_id(^^Holds<payload_nan>);

}  // namespace stable_id_nan_fixture

int main() { return static_cast<int>(stable_id_nan_fixture::refused_id & 1U); }
