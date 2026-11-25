// A body of the member frame door with_mut of a keyed FixedArray gives a value
// that names the brand of its frame.  The value holds no address, but a brand
// outside its frame would prove a fact about a frame that has ended.  The
// result of a frame door names no brand of the frame.

#include <foundation/core/Array.h>

namespace {

template <class Brand>
struct Proof final {};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    auto proof = array.with_mut(key, [](auto run) noexcept { return Proof<typename decltype(run)::brand_type>{}; });
    static_cast<void>(proof);
    return 0;
}
