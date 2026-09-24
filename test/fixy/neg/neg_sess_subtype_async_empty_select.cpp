// The bounded asynchronous check refuses an empty Select too, at every
// capacity, because it refuses an operand that is not well-formed.

#include <fixy/session/Subtype.h>

#include <cstddef>

namespace {

namespace s = ::fixy::session;

struct PingReq {};
using Empty = s::Select<>;
using One = s::Select<s::Send<PingReq, s::End>>;
struct FourSlots {
    static constexpr std::size_t channel_capacity = 4;
};

}  // namespace

int main() {
    s::assert_subtype_async<Empty, One, FourSlots>();
    return 0;
}
