// A unit starts objects over the address of some bytes and takes the result
// as a View of the objects.  The start over an address gives the address of
// the first object, as std::start_lifetime_as_array does, and no View: a View
// of the objects comes only from the start over a View of bytes or from the
// owner of the storage.  So the address does not become a View.

#include <foundation/core/Array.h>

int main() {
    alignas(int) foundation::core::FixedArray<unsigned char, 8> bytes{};
    foundation::core::View<int> const objects =
        foundation::lifetime::start_as_array<int>(&*bytes.get(0).expect("the array holds 8 bytes"), std::size_t{2});
    static_cast<void>(objects);
    return 0;
}
