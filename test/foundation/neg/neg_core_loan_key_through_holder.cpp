// A body of the keyed frame door of a FixedArray captures an object that
// holds a Ref of the key.  A function that the body calls could then open a
// second frame of the array.  The walk of the gate reads the parts of each
// capture through references and pointers, and it refuses the body.

#include <foundation/core/Array.h>
#include <foundation/core/Ref.h>

namespace {

template <class KeyRef>
struct Holder {
    KeyRef key;
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_fixed_array<int, 4>();
    Holder<decltype(::foundation::core::to(key))> const holder{::foundation::core::to(key)};
    key = array.with_mut(std::move(key), [&holder](auto run) noexcept {
        static_cast<void>(holder);
        static_cast<void>(run.size());
    });
    return array.at(::foundation::core::index<0>());
}
