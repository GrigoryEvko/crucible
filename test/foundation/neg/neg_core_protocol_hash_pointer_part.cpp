// The hash of the Protocol family refuses a pointer part.  An address
// changes from run to run, so a hash of it breaks the bit-exact replay.  A
// pointer that only the process reads is a local part, which the hash skips.
// The template argument gives the part its pointer type.

#include <foundation/core/Protocol.h>

template <class Part>
struct[[= ::foundation::core::derive_protocol{}]] HoldsAddress {
    Part address{};
    unsigned own = 0;
};

int main() {
    HoldsAddress<int const*> const value{};
    return static_cast<int>(::foundation::core::hash(value).raw() & 1u);
}
