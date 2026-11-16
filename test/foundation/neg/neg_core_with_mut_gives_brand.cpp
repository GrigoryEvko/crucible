// A body of a frame door gives a value that names the brand of its frame.  The
// value holds no address, but a brand outside its frame would prove a fact
// about a frame that has ended.  The result of a frame door names no brand of
// the frame.

#include <foundation/core/List.h>

#include <utility>

namespace {

template <class Brand>
struct Proof final {};

}  // namespace

int main() {
    ::foundation::core::FixedList<int, 4> list{};
    auto [proof, back] = ::foundation::core::with_mut(
        std::move(list), [](auto out) noexcept { return Proof<typename decltype(out)::brand_type>{}; });
    static_cast<void>(proof);
    static_cast<void>(back);
    return 0;
}
