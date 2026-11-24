// A static local of the generic body of a frame door keeps a Ref of the frame.
// One call site of a door gives each of its calls one brand, so the next call
// would read the Ref of an owner that the first call gave back.  The rule
// brand-escape refuses each object of static or thread storage duration that
// holds a scope brand.

#include <foundation/core/Array.h>

#include <utility>

namespace {

namespace core = ::foundation::core;

struct KeepsFirst final {
    template <class Brand, std::size_t Extent>
    void operator()(core::ViewMut<int, Brand, Extent> out) const noexcept {
        static core::Option<core::Ref<int const, Brand>> const kept = out.get(0);
        static_cast<void>(kept.is_some());
    }
};

}  // namespace

int main() {
    core::FixedArray<int, 2> array{1, 2};
    array.with_mut(KeepsFirst{});
    return 0;
}
