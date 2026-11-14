// An array of aggregates that each hold a proof.  The elements of
// mint_heap_array_written start their lifetime with the bytes of the
// allocation, and a proof has no lifetime that starts that way: only its own
// constructor makes one.  The element concept walks each subobject and
// refuses the type.

#include <foundation/core/Array.h>
#include <foundation/effects/Effect.h>

namespace {
// The shape of a proof: a private user-provided constructor.
class Token {
public:
    Token(Token const&) noexcept {}

private:
    Token() noexcept {}
};
struct HoldsToken {
    Token token;
};
struct WritesNothing {
    template <class Lease>
    void operator()(Lease& lease) const noexcept {
        static_cast<void>(lease);
    }
};
}  // namespace

int main() {
    auto tokens = ::foundation::core::mint_heap_array_written<HoldsToken>(::foundation::effects::testing::test().alloc,
                                                                          0, WritesNothing{});
    return static_cast<int>(tokens.size());
}
