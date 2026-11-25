// write_each over two frames of one FixedArray with no key.  The output is the
// elements [1, 64) and the input the elements [0, 63), and the two brands would
// give the loop of restrict pointers, which can read the inputs before the
// writes and then give another result.  An array with no key opens no frame.

#include <foundation/core/Array.h>

#include <utility>

namespace core = ::foundation::core;

namespace {

struct Same final {
    int operator()(int const& value) const noexcept { return value; }
};

// Opens a frame of the array.  The body of the last one writes each element of
// the first 63 of the run of the frame before it into the last 63 elements.
template <class... Outer>
void write_shifted(core::FixedArray<int, 64>& array, Outer&... outer) noexcept {
    array.with_mut([&array, &outer...](auto inner) noexcept {
        if constexpr (sizeof...(Outer) == 0) {
            [&array, &inner]() noexcept { write_shifted(array, inner); }();
        } else {
            auto [first, tail] = core::split_at<1>(std::move(inner));
            auto [head, last] = core::split_at<63>(std::move(outer...[0]));
            static_cast<void>(core::write_each(std::move(tail), Same{}, head).is_ok());
            static_cast<void>(first.size() + last.size());
        }
    });
}

}  // namespace

int main() {
    core::FixedArray<int, 64> array{};
    write_shifted(array);
    return array.at(core::index<63>());
}
