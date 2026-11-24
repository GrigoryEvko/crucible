// A body of the keyed frame door of a FixedArray captures an object that
// holds a reference to the key.  A function that the body calls could then
// open a second frame of the array.  The walk of the gate reads the parts of
// each capture through references and pointers, and it refuses the body.  A
// Ref of the key does not compile at all (neg_core_ref_of_a_loan_key).

#include <foundation/core/Array.h>

namespace {

template <class KeyRef>
struct Holder {
    KeyRef key;
};

}  // namespace

int main() {
    auto [array, key] = ::foundation::core::mint_keyed_owner(::foundation::core::fixed_array_owner<int, 4>);
    Holder<decltype(key)&> const holder{key};
    key = array.with_mut(std::move(key), [&holder](auto run) noexcept {
        static_cast<void>(holder);
        static_cast<void>(run.size());
    });
    return array.at(::foundation::core::index<0>());
}
