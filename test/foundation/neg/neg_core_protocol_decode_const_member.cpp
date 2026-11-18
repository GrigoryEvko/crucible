// decode of the Protocol family writes each part of its result in place, so
// it refuses a class with a const member.  encode takes such a class.

#include <foundation/core/Protocol.h>

#include <cstddef>

struct[[= ::foundation::core::derive_protocol{}]] HoldsConst {
    unsigned const word = 0;
};

int main() {
    ::foundation::core::FixedArray<std::byte, 4> const image{};
    return ::foundation::core::decode<HoldsConst>(image.view()).is_ok() ? 0 : 1;
}
