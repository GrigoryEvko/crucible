// The bits of a pointer are an address, so a pointer from bytes points to
// no object.  decode refuses a class with a pointer member.

#include <foundation/core/Scalar.h>

#include <cstdint>

struct HoldsAddress {
    int* address = nullptr;
};

int main() { return ::foundation::core::decode<HoldsAddress>(std::uint64_t{64}).is_some() ? 0 : 1; }
